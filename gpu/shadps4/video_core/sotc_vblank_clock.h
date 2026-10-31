// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: the present thread's clock (VideoOutDriver::PresentThread). Guest vblanks fire by absolute
// deadline, so their count keeps BB_VBLANK_HZ on average whatever a tick costs; with the flip
// pacer the thread also wakes on subticks between two deadlines. Checked offline by
// tests/test_vblank_clock.cpp (ninja -C out/gpu vblank-clock-test).

#pragma once

#include <algorithm>
#include <chrono>
#include <cstdint>

namespace SotcVblankClock {

using Clock = std::chrono::steady_clock;

/// Vblanks due at `now`: 0 before `next_vblank`, else every deadline passed since, and
/// `next_vblank` moves to the first one still ahead.
inline int64_t TakeDue(Clock::time_point now, Clock::time_point& next_vblank,
                       std::chrono::nanoseconds period) {
    if (now < next_vblank) {
        return 0;
    }
    const int64_t due = 1 + (now - next_vblank) / period;
    next_vblank += period * due;
    return due;
}

/// The next wake: `next_vblank`, or with `subticks` > 1 the first subtick of the vblank period
/// ending there that is after `now`. Subticks already passed are skipped, not run back to back.
inline Clock::time_point NextWake(Clock::time_point now, Clock::time_point next_vblank,
                                  std::chrono::nanoseconds period, uint32_t subticks) {
    if (subticks <= 1) {
        return next_vblank;
    }
    const auto period_start = next_vblank - period;
    const int64_t into = std::max<int64_t>(
        0, std::chrono::duration_cast<std::chrono::nanoseconds>(now - period_start).count());
    const int64_t subtick = into * subticks / period.count() + 1;
    if (subtick >= subticks) {
        return next_vblank;
    }
    // Rounded up, so the wake is always after `now`.
    return period_start +
           std::chrono::nanoseconds{(period.count() * subtick + subticks - 1) / subticks};
}

} // namespace SotcVblankClock
