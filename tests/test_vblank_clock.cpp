// SPDX-License-Identifier: GPL-2.0-or-later
// Offline check of the present thread's vblank clock (sotc_vblank_clock.h): no game, Vulkan
// device or window. The loop of VideoOutDriver::PresentThread runs on a virtual clock with slow
// flips, and the guest vblank count must keep BB_VBLANK_HZ in grid (capped) and paced (subtick)
// modes. A model of the old AccurateTimer with a one-subtick catch-up shows the drift it fixes.
#include <algorithm>
#include <cassert>
#include <chrono>
#include <cstdint>
#include <cstdio>
#include <random>
#include "gpu/shadps4/video_core/sotc_vblank_clock.h"

using namespace SotcVblankClock;
using std::chrono::nanoseconds;

namespace {

Clock::time_point At(int64_t ns) {
    return Clock::time_point{nanoseconds{ns}};
}

struct Workload {
    int64_t frame_ns;   ///< the game submits a frame this often (+-20%)
    int64_t slow_every; ///< one slow flip in this many (0: none)
    int64_t slow_min_ns, slow_max_ns;
};

// The game side: frames arrive at its rate; a tick flips one when any is queued.
struct Game {
    std::mt19937_64 rng;
    Workload work;
    int64_t next_frame;
    int queued = 0;

    // The cost of one tick at `now`; `slow` when it was a slow flip.
    int64_t Tick(int64_t now, bool& slow) {
        std::uniform_int_distribution<int64_t> jitter(-work.frame_ns / 5, work.frame_ns / 5);
        while (now >= next_frame) {
            queued = std::min(queued + 1, 3); // the game waits for its flip queue beyond that
            next_frame += work.frame_ns + jitter(rng);
        }
        slow = false;
        if (queued == 0) {
            return std::uniform_int_distribution<int64_t>(20'000, 60'000)(rng);
        }
        --queued;
        slow = work.slow_every > 0 && rng() % work.slow_every == 0;
        return slow ? std::uniform_int_distribution<int64_t>(work.slow_min_ns, work.slow_max_ns)(rng)
                    : std::uniform_int_distribution<int64_t>(500'000, 2'000'000)(rng);
    }
};

struct Result {
    int64_t vblanks = 0;
    int64_t expected = 0; ///< deadlines at or before the last tick
    int64_t wakes = 0;
    int64_t max_wakes_per_vblank = 0;
    double mean_late_ms = 0.0; ///< vblank fire time behind its deadline, ticks without a stall
};

constexpr int64_t kStart = 5'000'000'000;

// The loop of PresentThread: sleep to the wake (woken up to 0.4 ms late), the tick's flip, the
// vblanks due, the next wake.
Result RunNew(int hz, uint32_t subticks, double seconds, const Workload& work, uint64_t seed) {
    const nanoseconds period{1'000'000'000 / hz};
    Game game{std::mt19937_64{seed}, work, kStart, 0};
    std::uniform_int_distribution<int64_t> overshoot(0, 400'000);
    const int64_t end = kStart + int64_t(seconds * 1e9);
    Clock::time_point next_vblank = At(kStart);
    Clock::time_point wake = next_vblank;
    int64_t now = kStart, late_ns = 0, late_count = 0, wakes_since_vblank = 0;
    Result r;
    while (now < end) {
        now = std::max(now, wake.time_since_epoch().count()) + overshoot(game.rng);
        ++r.wakes;
        ++wakes_since_vblank;
        bool slow = false;
        now += game.Tick(now, slow);
        const Clock::time_point first_deadline = next_vblank;
        if (const int64_t due = TakeDue(At(now), next_vblank, period)) {
            assert(next_vblank > At(now) && next_vblank - period <= At(now));
            r.vblanks += due;
            if (!slow) {
                late_ns += (At(now) - first_deadline).count();
                ++late_count;
            }
            r.max_wakes_per_vblank = std::max(r.max_wakes_per_vblank, wakes_since_vblank);
            wakes_since_vblank = 0;
        }
        wake = NextWake(At(now), next_vblank, period, subticks);
        assert(wake > At(now) && wake <= next_vblank);
    }
    r.expected = (now - kStart) / period.count() + 1;
    r.mean_late_ms = late_count ? double(late_ns) / double(late_count) / 1e6 : 0.0;
    return r;
}

// The PR as first written on the fork's AccurateTimer: at most one interval caught up, the
// interval a subtick, the vblank on subtick 0.
Result RunOldClamp(int hz, uint32_t subticks, double seconds, const Workload& work,
                   uint64_t seed) {
    const int64_t interval = 1'000'000'000 / hz / subticks;
    Game game{std::mt19937_64{seed}, work, kStart, 0};
    std::uniform_int_distribution<int64_t> overshoot(0, 400'000);
    const int64_t end = kStart + int64_t(seconds * 1e9);
    int64_t now = kStart, total_wait = 0;
    uint32_t subtick = 0;
    Result r;
    while (now < end) {
        const int64_t begin = now;
        if (total_wait > 0) {
            now += total_wait + overshoot(game.rng);
        }
        const int64_t tick_start = now;
        total_wait -= tick_start - begin;
        ++r.wakes;
        bool slow = false;
        now += game.Tick(now, slow);
        r.vblanks += subtick == 0;
        subtick = (subtick + 1) % subticks;
        total_wait += interval - (now - tick_start);
        total_wait = std::max(total_wait, -interval);
    }
    r.expected = (now - kStart) / (1'000'000'000 / hz) + 1;
    return r;
}

void Units() {
    const nanoseconds period{8'333'333};
    Clock::time_point next = At(1'000);
    assert(TakeDue(At(999), next, period) == 0 && next == At(1'000));
    assert(TakeDue(At(1'000), next, period) == 1 && next == At(1'000) + period);
    next = At(0);
    assert(TakeDue(At(0) + period * 2 + period / 2, next, period) == 3 && next == At(0) + period * 3);
    // A 10 s stall at 120 Hz: every one of its 1200 deadlines plus the first.
    next = At(0);
    assert(TakeDue(At(10'000'000'000), next, period) == 1201);

    // Subticks: the first one after `now`, none past the next deadline.
    const Clock::time_point vblank = At(0) + period;
    assert(NextWake(At(0), vblank, period, 1) == vblank);
    assert(NextWake(At(0), vblank, period, 8) == At(1'041'667));
    assert(NextWake(At(1'041'666), vblank, period, 8) == At(1'041'667));
    assert(NextWake(At(1'041'667), vblank, period, 8) == At(2'083'334));
    assert(NextWake(At(7'291'666), vblank, period, 8) == At(7'291'667));
    assert(NextWake(At(7'291'667), vblank, period, 8) == vblank);
    assert(NextWake(At(8'333'332), vblank, period, 8) == vblank);
}

bool Check(const char* name, const Result& r, uint32_t subticks) {
    const double rate_error = double(r.vblanks - r.expected) / double(r.expected);
    std::printf("%-36s vblanks %6lld of %6lld (%+.4f%%)  wakes/vblank %.2f (max %lld)  "
                "late %.2f ms\n",
                name, static_cast<long long>(r.vblanks), static_cast<long long>(r.expected),
                rate_error * 100.0, double(r.wakes) / double(r.vblanks),
                static_cast<long long>(r.max_wakes_per_vblank), r.mean_late_ms);
    // Every deadline up to the last tick fired, none ahead of it.
    bool ok = r.vblanks == r.expected;
    // At most one wake per subtick: none run back to back after a slow flip.
    ok = ok && r.max_wakes_per_vblank <= int64_t(subticks);
    // Outside stalls a vblank fires within the wake delay and one flip.
    ok = ok && r.mean_late_ms < 2.0;
    return ok;
}

} // namespace

int main() {
    Units();

    const Workload calm{10'000'000, 0, 0, 0};
    const Workload slow_flips{10'000'000, 10, 12'000'000, 60'000'000};
    const Workload hitches{16'000'000, 4, 30'000'000, 250'000'000};
    bool ok = true;
    for (const int hz : {60, 120}) {
        for (const uint32_t subticks : {1u, 8u}) {
            char name[64];
            std::snprintf(name, sizeof(name), "%d Hz, %u subtick(s), calm", hz, subticks);
            ok &= Check(name, RunNew(hz, subticks, 300.0, calm, 1), subticks);
            std::snprintf(name, sizeof(name), "%d Hz, %u subtick(s), slow flips", hz, subticks);
            ok &= Check(name, RunNew(hz, subticks, 300.0, slow_flips, 2), subticks);
            std::snprintf(name, sizeof(name), "%d Hz, %u subtick(s), hitches", hz, subticks);
            ok &= Check(name, RunNew(hz, subticks, 300.0, hitches, 3), subticks);
        }
    }

    // The old clamp loses vblanks under the same slow flips: the checks above would catch it.
    // With one subtick it is the timer before the pacer, shown for comparison.
    for (const uint32_t subticks : {8u, 1u}) {
        const Result old = RunOldClamp(120, subticks, 300.0, slow_flips, 2);
        const double rate = double(old.vblanks) / double(old.expected);
        std::printf("old clamp, 120 Hz, %u subtick(s), slow   vblanks %6lld of %6lld (%.1f%% of "
                    "the rate)\n",
                    subticks, static_cast<long long>(old.vblanks),
                    static_cast<long long>(old.expected), rate * 100.0);
        ok &= subticks == 1 || rate < 0.95;
    }

    std::printf(ok ? "vblank clock: OK\n" : "vblank clock: FAILED\n");
    return ok ? 0 : 1;
}
