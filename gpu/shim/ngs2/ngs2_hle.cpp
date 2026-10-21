// SPDX-License-Identifier: GPL-2.0-or-later
// sotc: libSceNgs2 written for the port, so the game has sound without the user's firmware
// modules (libSceNgs2.sprx / libSceUlt.sprx). Covers what Shadow of the Colossus asks of it:
// systems, sampler / submixer / reverb / mastering racks (and pass-through custom submixers),
// ATRAC9 and PCM sampler voices with pitch, envelopes, ports, matrices, patches and UserFx, and
// the render into the game's output buffers.
//
// PS4 structure layouts and parameter ids were read from the game's own calls (SOTC_NGS2_TRACE),
// checked against shadPS4's ngs2 headers. The rendering model, the default channel map and the
// I3DL2 reverb follow AnyPS5's libSceNgs2.native (GPL-2.0, github.com/boykopovar/AnyPS5), whose
// reverb was matched against the PS4 firmware library. Its PS5 layouts differ from the PS4 ones
// used here (16-byte names, 32-byte waveform blocks, 48-byte sampler state).
//
// Nothing here throws into guest code: unknown parameters are logged once and ignored.
// SOTC_NGS2_HLE=0 renders silence (as before); SOTC_NGS2_TRACE=1 logs the game's calls.

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdarg>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <atomic>
#include <chrono>
#include <mutex>
#include <numbers>
#include <set>
#include <unordered_set>
#include <vector>

extern "C" {
#include "../../../third_party/LibAtrac9/C/src/libatrac9.h"
}

#define NGS2_ABI __attribute__((sysv_abi))

namespace {

// sotc: SOTC_FPS: every 5 s the voices started, the ATRAC9 voices that ran out of data and the
// system renders (sound missing on small CPUs: are voices started, are they fed).
std::atomic<unsigned> stat_plays{0}, stat_dry{0}, stat_renders{0};

using u8 = std::uint8_t;
using u16 = std::uint16_t;
using s16 = std::int16_t;
using u32 = std::uint32_t;
using s32 = std::int32_t;
using u64 = std::uint64_t;
using s64 = std::int64_t;

// --- Constants -----------------------------------------------------------------------------

constexpr s32 OK = 0;
constexpr s32 ERROR_INVALID_SYSTEM_HANDLE = static_cast<s32>(0x804A0230);
constexpr s32 ERROR_INVALID_RACK_HANDLE = static_cast<s32>(0x804A0261);
constexpr s32 ERROR_INVALID_VOICE_HANDLE = static_cast<s32>(0x804A0300);
constexpr s32 ERROR_INVALID_OUT_ADDRESS = static_cast<s32>(0x804A8010);
constexpr s32 ERROR_INVALID_OUT_SIZE = static_cast<s32>(0x804A8011);
constexpr s32 ERROR_INVALID_RACK_ID = static_cast<s32>(0x804A0260);

constexpr u32 RACK_SAMPLER = 0x1000;
constexpr u32 RACK_SUBMIXER = 0x2000;
constexpr u32 RACK_REVERB = 0x2001;
constexpr u32 RACK_MASTERING = 0x3000;
constexpr u32 RACK_CUSTOM_SAMPLER = 0x4001;
constexpr u32 RACK_CUSTOM_SUBMIXER = 0x4002;

constexpr u32 WAVE_PCM_I16L = 0x12;
constexpr u32 WAVE_PCM_F32L = 0x18;
constexpr u32 WAVE_VAG = 0x1c; // Sony PS-ADPCM: 16-byte frames of 28 samples
constexpr u32 WAVE_ATRAC9 = 0x40;

// PS4 voice events (the game sends 0 to start its mixer voices).
constexpr u32 EVENT_PLAY = 0;
constexpr u32 EVENT_STOP = 1;
constexpr u32 EVENT_STOP_IMM = 2;
constexpr u32 EVENT_KILL = 3;
constexpr u32 EVENT_PAUSE = 4;
constexpr u32 EVENT_RESUME = 5;

constexpr u32 STATE_INUSE = 1;
constexpr u32 STATE_PLAYING = 2;
constexpr u32 STATE_PAUSED = 4;
constexpr u32 STATE_STOPPED = 8;
constexpr u32 STATE_EMPTY = 0x20;

constexpr u32 PARAM_MATRIX_LEVELS = 0x1;
constexpr u32 PARAM_PORT_VOLUME = 0x2;
constexpr u32 PARAM_PORT_MATRIX = 0x3;
constexpr u32 PARAM_PORT_DELAY = 0x4;
constexpr u32 PARAM_PATCH = 0x5;
constexpr u32 PARAM_EVENT = 0x6;
constexpr u32 PARAM_CALLBACK = 0x7;
constexpr u32 SAMPLER_SETUP = 0x10000000;
constexpr u32 SAMPLER_ADD_BLOCKS = 0x10000001;
constexpr u32 SAMPLER_EXIT_LOOP = 0x10000004;
constexpr u32 SAMPLER_PITCH = 0x10000005;
constexpr u32 SAMPLER_ENVELOPE = 0x10000006;
constexpr u32 SAMPLER_USER_FX = 0x10000008;
constexpr u32 SAMPLER_FILTER = 0x1000000a;
constexpr u32 SUBMIXER_SETUP = 0x20000000;
constexpr u32 SUBMIXER_USER_FX = 0x20000004;
constexpr u32 SUBMIXER_FILTER = 0x20000006;
constexpr u32 REVERB_SETUP = 0x20010000;
constexpr u32 REVERB_I3DL2 = 0x20010001;
constexpr u32 MASTERING_SETUP = 0x30000000;
constexpr u32 MASTERING_LIMITER = 0x30000003;
constexpr u32 MASTERING_GAIN = 0x30000004;
constexpr u32 MASTERING_OUTPUT = 0x30000005;
constexpr u32 CUSTOM_SAMPLER_SETUP = 0x40010000;
constexpr u32 CUSTOM_SAMPLER_ADD_BLOCKS = 0x40010001;
constexpr u32 CUSTOM_SAMPLER_EXIT_LOOP = 0x40010004;
constexpr u32 CUSTOM_SAMPLER_PITCH = 0x40010005;
constexpr u32 CUSTOM_SUBMIXER_SETUP = 0x40020000;

// Bytes of one channel's 28 samples (a VAG frame) in a waveform type.
constexpr u32 SampleBytesX28(u32 type) {
    return type == WAVE_VAG ? 16 : type == WAVE_PCM_F32L ? 112 : 56;
}

constexpr u32 BLOCKS_CONTINUE = 1;
constexpr u32 BLOCKS_APPEND = 2;
constexpr u32 BLOCKS_RESET = 4;
constexpr u32 BLOCKS_SILENCE = 0x10;

constexpr u32 MAX_CHANNELS = 8;
constexpr u32 MAX_PORTS = 8;
constexpr u32 MAX_MATRICES = 8;

// --- PS4 structures (as the game passes them) ----------------------------------------------

struct SystemOption {
    u64 size;
    char name[16];
    u32 flags;
    u32 maxGrainSamples;
    u32 numGrainSamples;
    u32 sampleRate;
    u32 reserved[6];
};
static_assert(sizeof(SystemOption) == 64);

struct ContextBufferInfo {
    void* hostBuffer;
    u64 hostBufferSize;
    uintptr_t reserved[5];
    uintptr_t userData;
};
static_assert(sizeof(ContextBufferInfo) == 64);

struct RackOption {
    u64 size;
    char name[16];
    u32 flags;
    u32 maxGrainSamples;
    u32 maxVoices;
    u32 maxInputDelayBlocks;
    u32 maxMatrices;
    u32 maxPorts;
    u32 reserved[20];
};
static_assert(sizeof(RackOption) == 128);

struct ParamHead {
    u16 size;
    s16 next;
    u32 id;
};

struct WaveformFormat {
    u32 type;
    u32 channels;
    u32 sampleRate;
    u32 configData;
    u32 frameOffset;
    u32 frameMargin;
};

struct WaveformBlock {
    u32 dataOffset;
    u32 dataSize;
    u32 numRepeats;
    u32 numSkipSamples;
    u32 numSamples;
    u32 reserved;
    uintptr_t userData;
};
static_assert(sizeof(WaveformBlock) == 32);

struct RenderBufferInfo {
    void* buffer;
    u64 size;
    u32 waveformType;
    u32 channels;
};

struct EnvelopePoint {
    u32 curve;
    u32 duration;
    float height;
};

struct SamplerVoiceState {
    u32 stateFlags;
    float envelopeHeight;
    float peakHeight;
    u32 reserved;
    u64 numDecodedSamples;
    u64 decodedDataSize;
    u64 userData;
    const void* waveformData;
};
static_assert(sizeof(SamplerVoiceState) == 48);

struct I3DL2 {
    float wet;
    float dry;
    s32 room;
    s32 roomHf;
    u32 reflectionPattern;
    float decayTime;
    float decayHfRatio;
    s32 reflections;
    float reflectionsDelay;
    s32 reverb;
    float reverbDelay;
    float diffusion;
    float density;
    float hfReference;
    u32 reserved[8];
};
static_assert(sizeof(I3DL2) == 88);

struct UserFxContext {
    float** channelData;
    uintptr_t userData0;
    uintptr_t userData1;
    uintptr_t userData2;
    u32 flags;
    u32 numChannels;
    u32 numGrainSamples;
    u32 sampleRate;
};
static_assert(sizeof(UserFxContext) == 48);
using UserFxHandler = s32 NGS2_ABI (*)(UserFxContext*);

// --- Logging -------------------------------------------------------------------------------

bool Trace() {
    static const bool on = [] {
        const char* v = std::getenv("SOTC_NGS2_TRACE");
        return v && v[0] == '1';
    }();
    return on;
}

// Experiment switch: SOTC_NGS2_STATE0=1 reports every voice as idle (as the silent stub did).
bool StateZero() {
    static const bool on = [] {
        const char* v = std::getenv("SOTC_NGS2_STATE0");
        return v && v[0] == '1';
    }();
    return on;
}

// The game's submixer UserFx callbacks only meter levels here; calling one crashed at the start of
// a new game, so they run only with SOTC_NGS2_USERFX=1.
bool UserFxEnabled() {
    static const bool on = [] {
        const char* v = std::getenv("SOTC_NGS2_USERFX");
        return v && v[0] == '1';
    }();
    return on;
}

bool Enabled() {
    static const bool on = [] {
        const char* v = std::getenv("SOTC_NGS2_HLE");
        return !v || v[0] != '0';
    }();
    return on;
}

void LogOnce(u64 key, const char* fmt, ...) {
    static std::mutex lock;
    static std::set<u64> seen;
    {
        std::scoped_lock l{lock};
        if (!seen.insert(key).second || seen.size() > 512) {
            return;
        }
    }
    std::printf("NGS2: ");
    va_list args;
    va_start(args, fmt);
    std::vprintf(fmt, args);
    va_end(args);
    std::printf("\n");
    std::fflush(stdout);
}

// --- Reverb (I3DL2), after AnyPS5's Reverb.cpp ---------------------------------------------

constexpr u32 TAP_RING_SAMPLES = 14400;
constexpr u32 EARLY_TAPS = 4;
constexpr u32 COMBS = 16;
constexpr u32 ALLPASSES = 4;
constexpr u32 LFE_CHANNEL = 3;

constexpr float TAP_TIMES[8][EARLY_TAPS] = {
    {0, 11, 23, 35}, {0, 14.75f, 26.5499992f, 38.0299988f}, {0, 6, 17, 29}, {0, 8, 20, 53},
    {0, 11, 17, 30}, {0, 16, 30, 55},  {0, 20, 29, 40}, {0, 37, 47, 62},
};
constexpr float TAP_GAINS[8][EARLY_TAPS] = {
    {0.7f, 0.4f, 0.134f, 0.1f},   {0.7f, 0.4f, 0.3f, 0.1f},     {0.7f, 0.45f, -0.6f, 0.2f},
    {0.7f, 0.55f, -0.6f, 0.2f},   {0.7f, -0.45f, 0.6f, 0.3f},   {0.7f, 0.7f, 0.7f, 0.4f},
    {0.7f, 0.7f, -0.4f, 0.3f},    {0.7f, 0.7f, -0.4f, 0.4f},
};
constexpr float COMB_TIMES[COMBS] = {
    22.0100002f, 23.25f,      24.75f,      26.6000004f, 28.25f,      29.6299992f,
    31.0599995f, 32.4399986f, 33.6899986f, 34.9500008f, 36.2900009f, 37.6500015f,
    38.9900017f, 40.3499985f, 41.6699982f, 43.0499992f,
};
constexpr float ALLPASS_TIMES[ALLPASSES] = {11.5799999f, 9.18999958f, 7.0999999f, 4.69000006f};
constexpr s32 ALLPASS_OFFSETS[8][ALLPASSES] = {
    {0, -32, -6, -11}, {-10, -5, 0, -34}, {-8, -3, -33, -5}, {-7, -9, -5, -8},
    {-33, -8, -8, 0},  {-11, -21, -10, -7}, {-9, -10, -27, -3}, {-6, -11, -9, -24},
};
constexpr u16 CHANNEL_COMBS[8] = {0xaaaa, 0x5555, 0xcccc, 0x3333, 0x0ff0, 0x9ab2, 0x3c3c, 0x399c};
constexpr I3DL2 SETUP_REVERB{1.0f,   0.0f,  -10000, 0,      0,     1.0f,    0.5f, -10000,
                             0.02f,  -10000, 0.04f, 100.0f, 100.0f, 5000.0f, {}};

struct ReverbTaps {
    std::array<u32, EARLY_TAPS + 1> delays{};
    std::array<float, EARLY_TAPS + 1> gains{};
};

struct Comb {
    std::vector<float> buffer;
    u32 cursor = 0;
    float feedback = 0, lowpassIn = 0, lowpassBack = 0, direct = 0, filtered = 0;
    float emphasis = 0, emphasisBack = 0, previous = 0, lowpass = 0;
};

struct Allpass {
    std::vector<float> buffer;
    u32 cursor = 0;
};

struct ReverbChannel {
    std::array<Allpass, ALLPASSES> allpasses;
    float y1 = 0, y2 = 0, wet = 0, dry = 0;
};

struct Reverb {
    I3DL2 params{};
    bool reset = true;
    bool changed = true;
    bool bad = false;
    u32 sampleRate = 0;
    std::vector<float> ring;
    u32 ringCursor = 0;
    ReverbTaps taps;
    float earlyGain = 0, lateGain = 0;
    float density = NAN;
    std::array<Comb, COMBS> combs;
    std::array<ReverbChannel, 8> channels;
    bool roomFilter = false;
    float roomB0 = 0, roomA1 = 0, roomA2 = 0;
};

float Millibels(float mb) {
    return mb < -10000.0f ? 0.0f : static_cast<float>(std::pow(10.0, static_cast<double>(mb * 0.0005f)));
}

float ClampF(float v, float lo, float hi) {
    return v < lo ? lo : std::min(hi, v);
}

s32 ClampMb(s32 v, s32 hi) {
    return std::max<s32>(-10000, std::min(v, hi));
}

u32 MsSamples(float ms, float rate) {
    return static_cast<u32>(rate * ms * 0.001f + 0.5f);
}

void Ramp(float* samples, float from, float to, u32 grain, const float* source, bool accumulate) {
    const float step = (to - from) / static_cast<float>(grain);
    float gain = from;
    for (u32 i = 0; i < grain; i++) {
        const float value = source[i] * (from == to ? to : gain);
        samples[i] = accumulate ? samples[i] + value : value;
        gain += step;
    }
}

void RunTaps(const Reverb& r, const ReverbTaps& taps, const float* input, u32 grain, float* early,
             float* late) {
    const auto size = static_cast<u32>(r.ring.size());
    std::fill(early, early + grain, 0.0f);
    std::fill(late, late + grain, 0.0f);
    for (u32 tap = 0; tap <= EARLY_TAPS; tap++) {
        float* out = tap < EARLY_TAPS ? early : late;
        const float gain = taps.gains[tap];
        const u32 delay = taps.delays[tap];
        u32 read = (r.ringCursor + size - delay) % size;
        for (u32 i = 0; i < grain; i++) {
            if (i < delay) {
                out[i] += r.ring[read] * gain;
                read = read + 1 == size ? 0 : read + 1;
            } else {
                out[i] += input[i - delay] * gain;
            }
        }
    }
}

ReverbTaps TapsFor(const I3DL2& p, u32 size, float rate) {
    ReverbTaps taps;
    const u32 pattern = p.reflectionPattern < 8 ? p.reflectionPattern : p.reflectionPattern - 8;
    const auto early = std::min(static_cast<u32>(p.reflectionsDelay * rate * 1000.0f * 0.001f + 0.5f), size);
    const auto late = std::min(
        static_cast<u32>((p.reflectionsDelay + p.reverbDelay) * rate * 1000.0f * 0.001f + 0.5f), size);
    const auto last = static_cast<u32>(rate * TAP_TIMES[pattern][EARLY_TAPS - 1] * 0.001f + 0.5f);
    const u32 limit = p.reflectionPattern > 7 ? size : late;
    const float scale =
        early + last > limit ? static_cast<float>(limit - early) / static_cast<float>(last) : 1.0f;
    taps.delays[0] = early;
    taps.gains[0] = TAP_GAINS[pattern][0];
    for (u32 tap = 1; tap < EARLY_TAPS; tap++) {
        taps.delays[tap] =
            static_cast<u32>(scale * TAP_TIMES[pattern][tap] * rate * 0.001f + 0.5f) + early;
        taps.gains[tap] = taps.delays[tap] == taps.delays[tap - 1] ? 0.0f : TAP_GAINS[pattern][tap];
    }
    taps.delays[EARLY_TAPS] =
        std::min(early + static_cast<u32>(p.reverbDelay * rate * 1000.0f * 0.001f + 0.5f), size);
    taps.gains[EARLY_TAPS] = 1.0f;
    return taps;
}

void SetupComb(Comb& comb, u32 length, const I3DL2& p, float rate, bool clear) {
    if (clear || comb.buffer.size() != length) {
        comb.buffer.assign(length, 0.0f);
        if (comb.cursor >= length) {
            comb.cursor = 0;
        }
    }
    comb.feedback = std::pow(10.0f, static_cast<float>(length) / rate / p.decayTime * -3.0f);
    const float ratio = p.decayHfRatio;
    if (ratio > 1.0f) {
        const float high = std::pow(10.0f, (ratio - 1.0f) * std::log10(comb.feedback));
        comb.lowpassIn = 0.0506209731f;
        comb.lowpassBack = 0.949379027f;
        comb.direct = 1.0f;
        comb.filtered = 1.0f - high;
    } else {
        float shaped = ratio * ratio;
        if (0.8f > ratio) {
            shaped = (0.8f - ratio) * -0.857142925f + 0.8f;
            shaped *= shaped;
        }
        const float back = std::min(
            0.99977994f, std::log10(comb.feedback) * 2.30258512f * 0.25f * (1.0f - 1.0f / shaped));
        comb.lowpassIn = 1.0f - back;
        comb.lowpassBack = back;
        comb.direct = 0.0f;
        comb.filtered = 1.0f;
    }
    const float root = std::sqrt(ratio);
    const float tilt = (1.0f - root) / (root + 1.0f);
    comb.emphasis = tilt / (1.0f - tilt) + 1.0f;
    comb.emphasisBack = -tilt / (1.0f - tilt);
}

void ResetReverb(Reverb& r) {
    r.ring.assign(TAP_RING_SAMPLES, 0.0f);
    r.ringCursor = 0;
    r.taps = {};
    r.earlyGain = 1.0f;
    r.lateGain = 1.0f;
    r.density = NAN;
    r.combs = {};
    r.channels = {};
}

bool ApplyReverbParams(Reverb& r, float rate) {
    const auto& p = r.params;
    r.taps = TapsFor(p, static_cast<u32>(r.ring.size()), rate);
    const float room = std::min(1.0f, Millibels(static_cast<float>(p.room)));
    r.earlyGain = Millibels(static_cast<float>(ClampMb(p.reflections, 1000))) * room;
    r.lateGain = Millibels(static_cast<float>(ClampMb(p.reverb, 2000) - 2400)) * room;
    const bool densityChanged = !(r.density == p.density);
    r.density = p.density;
    const float densityScale = p.density * -3.0f / 100.0f + 4.0f;
    for (u32 i = 0; i < COMBS; i++) {
        auto length = static_cast<u32>(rate * COMB_TIMES[i] / densityScale * 0.001f + 0.5f);
        length += ~length & 1;
        SetupComb(r.combs[i], length, p, rate, densityChanged);
    }
    for (u32 channel = 0; channel < 8; channel++) {
        for (u32 stage = 0; stage < ALLPASSES; stage++) {
            auto& ap = r.channels[channel].allpasses[stage];
            const s32 length =
                static_cast<s32>(MsSamples(ALLPASS_TIMES[stage], rate)) + ALLPASS_OFFSETS[channel][stage];
            if (length < 1) {
                return false;
            }
            if (ap.buffer.size() != static_cast<u32>(length)) {
                ap.buffer.assign(static_cast<u32>(length), 0.0f);
            }
            if (ap.cursor >= static_cast<u32>(length)) {
                ap.cursor = 0;
            }
        }
    }
    return true;
}

void UpdateRoomFilter(Reverb& r, float rate) {
    const auto& p = r.params;
    const float roomHf = Millibels(static_cast<float>(ClampMb(p.roomHf, 0)));
    r.roomFilter = 1.0f > roomHf;
    if (!r.roomFilter) {
        return;
    }
    const float cosine = std::cos(p.hfReference * 6.28318548f / rate);
    const float root = std::sqrt((roomHf + roomHf) * (1.0f - cosine) -
                                 roomHf * roomHf * (1.0f - cosine * cosine));
    const float pole = ((1.0f - roomHf * cosine) - root) / (1.0f - roomHf);
    r.roomB0 = (1.0f - pole) * (1.0f - pole);
    r.roomA1 = pole + pole;
    r.roomA2 = pole * -pole;
}

// sotc: the filter state lives in locals while a grain runs (stores into the float buffer may
// alias the float members, so every sample reloaded and stored them), and the delay line wraps
// once per run instead of a test per sample. Same operations in the same order (~a fifth of the
// sound thread went to the reverb).
void RunComb(Comb& comb, const float* input, float* output, u32 grain) {
    const auto length = static_cast<u32>(comb.buffer.size());
    if (length == 0) {
        return;
    }
    float* const buffer = comb.buffer.data();
    const float emphasis = comb.emphasis, emphasis_back = comb.emphasisBack;
    const float lowpass_in = comb.lowpassIn, lowpass_back = comb.lowpassBack;
    const float direct = comb.direct, filtered = comb.filtered, feedback = comb.feedback;
    float previous = comb.previous, lowpass = comb.lowpass;
    u32 cursor = comb.cursor;
    for (u32 i = 0; i < grain;) {
        const u32 end = i + std::min(grain - i, length - cursor);
        for (; i < end; i++, cursor++) {
            const float emphasized = input[i] * emphasis + previous * emphasis_back;
            previous = input[i];
            const float delayed = buffer[cursor];
            lowpass = delayed * lowpass_in + lowpass * lowpass_back;
            const float back = (delayed * direct + lowpass * filtered) * feedback;
            buffer[cursor] = emphasized + back;
            output[i] = back;
        }
        if (cursor == length) {
            cursor = 0;
        }
    }
    comb.previous = previous;
    comb.lowpass = lowpass;
    comb.cursor = cursor;
}

void RunAllpass(Allpass& ap, float gain, float* samples, u32 grain) {
    const auto length = static_cast<u32>(ap.buffer.size());
    if (length == 0) {
        return;
    }
    float* const buffer = ap.buffer.data();
    u32 cursor = ap.cursor;
    for (u32 i = 0; i < grain;) {
        const u32 end = i + std::min(grain - i, length - cursor);
        for (; i < end; i++, cursor++) {
            const float delayed = buffer[cursor];
            const float stored = delayed * gain + samples[i];
            buffer[cursor] = stored;
            samples[i] = delayed - gain * stored;
        }
        if (cursor == length) {
            cursor = 0;
        }
    }
    ap.cursor = cursor;
}

void MixDownMono(const float* input, u32 channels, u32 grain, float* mono) {
    if (channels == 1) {
        std::copy(input, input + grain, mono);
        return;
    }
    const float gain = channels == 2 ? 0.707000017f : channels == 6 ? 0.316000015f : 0.26699999f;
    for (u32 i = 0; i < grain; i++) {
        mono[i] = input[i] * gain;
    }
    for (u32 c = 1; c < channels; c++) {
        if (c == LFE_CHANNEL) {
            continue;
        }
        const float* ch = input + static_cast<size_t>(c) * grain;
        for (u32 i = 0; i < grain; i++) {
            mono[i] += ch[i] * gain;
        }
    }
}

// Processes samples (channels x grain, in place); false when the output is silent.
bool ProcessReverb(Reverb& r, std::vector<float>& samples, u32 channels, u32 grain, u32 sampleRate) {
    const float rate = static_cast<float>(sampleRate);
    if (r.bad) {
        std::fill(samples.begin(), samples.end(), 0.0f);
        return false;
    }
    if (r.sampleRate != sampleRate) {
        r.sampleRate = sampleRate;
        r.reset = true;
    }
    const bool reset = r.reset;
    if (reset) {
        ResetReverb(r);
        r.changed = true;
    }
    // sotc: scratch buffers kept between calls (every one is written before it is read).
    thread_local std::vector<float> input, mono, early, late, oldEarly, oldLate, combs, tail;
    input.assign(samples.begin(), samples.end());
    mono.resize(grain);
    early.resize(grain);
    late.resize(grain);
    MixDownMono(input.data(), channels, grain, mono.data());
    if (r.changed) {
        oldEarly.resize(grain);
        oldLate.resize(grain);
        RunTaps(r, r.taps, mono.data(), grain, oldEarly.data(), oldLate.data());
        if (!ApplyReverbParams(r, rate)) {
            r.bad = true;
            std::fill(samples.begin(), samples.end(), 0.0f);
            return false;
        }
        RunTaps(r, r.taps, mono.data(), grain, early.data(), late.data());
        Ramp(early.data(), 0.0f, 1.0f, grain, early.data(), false);
        Ramp(early.data(), 1.0f, 0.0f, grain, oldEarly.data(), true);
        Ramp(late.data(), 0.0f, 1.0f, grain, late.data(), false);
        Ramp(late.data(), 1.0f, 0.0f, grain, oldLate.data(), true);
    } else {
        RunTaps(r, r.taps, mono.data(), grain, early.data(), late.data());
    }
    for (u32 i = 0; i < grain; i++) {
        r.ring[r.ringCursor] = mono[i];
        r.ringCursor = r.ringCursor + 1 == r.ring.size() ? 0 : r.ringCursor + 1;
    }
    combs.resize(static_cast<size_t>(COMBS) * grain);
    for (u32 i = 0; i < COMBS; i++) {
        RunComb(r.combs[i], late.data(), combs.data() + static_cast<size_t>(i) * grain, grain);
    }
    UpdateRoomFilter(r, rate);
    const auto& p = r.params;
    bool audible = false;
    tail.resize(grain);
    for (u32 channel = 0; channel < channels && channel < 8; channel++) {
        auto& state = r.channels[channel];
        float* out = samples.data() + static_cast<size_t>(channel) * grain;
        const float* in = input.data() + static_cast<size_t>(channel) * grain;
        if (channel == LFE_CHANNEL) {
            std::copy(in, in + grain, out);
        } else {
            std::fill(tail.begin(), tail.end(), 0.0f);
            u32 selected = 0;
            for (u32 i = 0; i < COMBS; i++) {
                if ((CHANNEL_COMBS[channel] >> i & 1) == 0) {
                    continue;
                }
                const float weight =
                    (selected & 1) != 0
                        ? 1.0f
                        : 1.0f / (static_cast<float>(selected) * 0.5f * 20.0f + 40.0f) *
                                  (p.density + -100.0f) +
                              1.0f;
                const float* comb = combs.data() + static_cast<size_t>(i) * grain;
                for (u32 n = 0; n < grain; n++) {
                    tail[n] += comb[n] * weight;
                }
                selected++;
            }
            for (auto& ap : state.allpasses) {
                RunAllpass(ap, p.diffusion * 0.005f, tail.data(), grain);
            }
            for (u32 n = 0; n < grain; n++) {
                out[n] = early[n] * r.earlyGain + tail[n] * r.lateGain;
            }
            for (u32 n = 0; n < grain; n++) {
                const float y = r.roomFilter ? r.roomB0 * out[n] + r.roomA1 * state.y1 + r.roomA2 * state.y2
                                             : out[n];
                state.y2 = state.y1;
                state.y1 = y;
                out[n] = y;
            }
        }
        const float wet = channel == LFE_CHANNEL ? 0.0f : p.wet;
        Ramp(out, reset ? wet : state.wet, wet, grain, out, false);
        Ramp(out, reset ? p.dry : state.dry, p.dry, grain, in, true);
        state.wet = wet;
        state.dry = p.dry;
        for (u32 n = 0; n < grain && !audible; n++) {
            audible = std::fabs(out[n]) > 1e-9f;
        }
    }
    r.reset = false;
    r.changed = false;
    return audible;
}

void SetReverbParams(Reverb& r, const I3DL2& in) {
    if (std::isnan(in.wet) || std::isnan(in.dry) || std::isnan(in.decayTime) ||
        std::isnan(in.decayHfRatio) || std::isnan(in.reflectionsDelay) || std::isnan(in.reverbDelay) ||
        std::isnan(in.diffusion) || std::isnan(in.density) || std::isnan(in.hfReference)) {
        return;
    }
    auto& p = r.params;
    p = in;
    p.wet = ClampF(in.wet, 0.0f, 32.0f);
    p.dry = ClampF(in.dry, 0.0f, 32.0f);
    p.reflectionPattern = in.reflectionPattern < 16 ? in.reflectionPattern : 0;
    p.decayTime = ClampF(in.decayTime, 0.1f, 20.0f);
    p.decayHfRatio = ClampF(in.decayHfRatio, 0.00999999978f, 2.0f);
    p.reflectionsDelay = ClampF(in.reflectionsDelay, 0.0f, 0.300000012f);
    p.reverbDelay = ClampF(in.reverbDelay, 0.0f, 0.100000001f);
    p.diffusion = ClampF(in.diffusion, 0.0f, 100.0f);
    p.density = ClampF(in.density, 0.0f, 100.0f);
    p.hfReference = ClampF(in.hfReference, 20.0f, 20000.0f);
    r.changed = true;
}

// --- Objects -------------------------------------------------------------------------------

constexpr u32 MAGIC_SYSTEM = 0x5359534e; // NSYS
constexpr u32 MAGIC_RACK = 0x4b43524e;   // NRCK
constexpr u32 MAGIC_VOICE = 0x4349564e;  // NVIC

struct System;
struct Rack;

enum class Play : u32 {
    Empty = 0,
    Playing = STATE_INUSE | STATE_PLAYING,
    Paused = STATE_INUSE | STATE_PAUSED,
    Stopped = STATE_INUSE | STATE_PLAYING | STATE_STOPPED,
};

struct Block {
    const u8* data = nullptr;
    WaveformBlock info{};
    u32 cursor = 0;      // frames played (PCM)
    u32 dataCursor = 0;  // bytes consumed (ATRAC9)
    bool started = false;
};

struct Atrac9 {
    void* decoder = nullptr;
    u8 config[4]{};
    u32 frameSamples = 0;
    u32 framesInSuperframe = 0;
    u32 superframeBytes = 0;
    std::vector<float> window; // decoded interleaved frames
    u32 windowCursor = 0;      // frames already played from window
    std::vector<u8> input;
    std::vector<short> pcm;
    u32 remainingSamples = 0;
    u32 skipSamples = 0;
    bool finishOnDrain = false;
    bool broken = false;
};

// VAG decoding state: the frame being read and the one before it (linear interpolation looks one
// sample ahead, so reads go back across a frame boundary).
struct Vag {
    const u8* data = nullptr;
    s64 frame = -1;
    float hist1[MAX_CHANNELS]{};
    float hist2[MAX_CHANNELS]{};
    float current[MAX_CHANNELS][28]{};
    float previous[MAX_CHANNELS][28]{};
};

struct Port {
    struct Voice* dest = nullptr;
    float volume = 1.0f;
    s32 matrix = -1;
};

struct Envelope {
    std::vector<EnvelopePoint> forward, release;
    enum Phase { None, Forward, Sustain, Release, Done } phase = None;
    u32 segment = 0;
    u32 position = 0;
    float start = 0.0f;
    float height = 1.0f;
};

struct Voice {
    u32 magic = MAGIC_VOICE;
    Rack* rack = nullptr;
    u32 index = 0;
    Play state = Play::Empty;
    u32 stateFlags = 0;
    u32 channels = 0;
    // sampler
    u32 sampleRate = 0;
    u32 waveformType = 0;
    float pitch = 1.0f;
    u64 phase = 0; // 32.32 fixed point
    std::deque<Block> blocks;
    bool acceptsBlocks = true;
    u64 decodedSamples = 0;
    u64 decodedBytes = 0;
    const u8* waveformEnd = nullptr;
    Atrac9 atrac9;
    Vag vag;
    Envelope envelope;
    // routing
    std::array<Port, MAX_PORTS> ports{};
    std::array<std::vector<float>, MAX_MATRICES> matrices{};
    // mixers
    std::unique_ptr<Reverb> reverb;
    UserFxHandler userFx = nullptr;
    std::array<uintptr_t, 3> userFxData{};
    u32 userFxFlags = 0;
    u32 outputId = 0;
    float fbwLevel = 1.0f;
    float lfeLevel = 1.0f;
    bool limiter = false;
    float limiterThreshold = 1.0f;
    // render scratch
    std::vector<float> samples; // channels x grain, planar
    std::vector<std::pair<Voice*, const Port*>> sources;
    bool rendered = false;
    bool rendering = false;
    bool hasSamples = false;
};

struct Rack {
    u32 magic = MAGIC_RACK;
    System* system = nullptr;
    u32 rackId = 0;
    std::vector<std::unique_ptr<Voice>> voices;
};

struct System {
    u32 magic = MAGIC_SYSTEM;
    u32 sampleRate = 48000;
    u32 grain = 256;
    u64 renderCount = 0;
    std::vector<Rack*> racks;
};

std::recursive_mutex& Lock() {
    static std::recursive_mutex m;
    return m;
}

std::unordered_set<uintptr_t>& Objects() {
    static std::unordered_set<uintptr_t> s;
    return s;
}

template <class T>
T* Find(uintptr_t handle, u32 magic) {
    if (!handle || !Objects().count(handle)) {
        return nullptr;
    }
    auto* obj = reinterpret_cast<T*>(handle);
    return obj->magic == magic ? obj : nullptr;
}

bool IsSampler(const Voice& v) {
    return v.rack->rackId == RACK_SAMPLER || v.rack->rackId == RACK_CUSTOM_SAMPLER;
}

// --- ATRAC9 --------------------------------------------------------------------------------

void ReleaseAtrac9(Atrac9& a) {
    if (a.decoder) {
        Atrac9ReleaseHandle(a.decoder);
        a.decoder = nullptr;
    }
}

bool RestartAtrac9(Voice& v) {
    auto& a = v.atrac9;
    if (!a.decoder) {
        a.decoder = Atrac9GetHandle();
    }
    u8 config[4];
    std::memcpy(config, a.config, 4);
    a.broken = !a.decoder || Atrac9InitDecoder(a.decoder, config) != 0;
    a.window.clear();
    a.windowCursor = 0;
    a.input.clear();
    a.remainingSamples = 0;
    a.skipSamples = 0;
    a.finishOnDrain = false;
    return !a.broken;
}

bool SetupAtrac9(Voice& v, const WaveformFormat& f) {
    auto& a = v.atrac9;
    for (int i = 0; i < 4; i++) {
        a.config[i] = static_cast<u8>(f.configData >> (24 - 8 * i));
    }
    if (!RestartAtrac9(v)) {
        LogOnce(0xa9000000ull | f.configData, "ATRAC9 config %08x rejected", f.configData);
        return false;
    }
    Atrac9CodecInfo info{};
    if (Atrac9GetCodecInfo(a.decoder, &info) != 0 || info.channels <= 0 ||
        info.framesInSuperframe <= 0 || info.frameSamples <= 0) {
        a.broken = true;
        return false;
    }
    if (static_cast<u32>(info.channels) != v.channels) {
        LogOnce(0xa9100000ull | f.configData, "ATRAC9 config %08x has %d channels, voice %u",
                f.configData, info.channels, v.channels);
        v.channels = std::min<u32>(static_cast<u32>(info.channels), MAX_CHANNELS);
    }
    a.frameSamples = static_cast<u32>(info.frameSamples);
    a.framesInSuperframe = static_cast<u32>(info.framesInSuperframe);
    a.superframeBytes = static_cast<u32>(info.superframeSize);
    a.pcm.resize(static_cast<size_t>(a.frameSamples) * info.channels);
    return true;
}

u64 Atrac9BlockBytes(const Voice& v, const WaveformBlock& b) {
    const u64 samples = static_cast<u64>(b.numSkipSamples) + b.numSamples;
    const u64 sfSamples = static_cast<u64>(v.atrac9.frameSamples) * v.atrac9.framesInSuperframe;
    if (!sfSamples) {
        return 0;
    }
    return (samples + sfSamples - 1) / sfSamples * v.atrac9.superframeBytes;
}

// --- Sampler playback ----------------------------------------------------------------------

void SetEvent(Voice& v, u32 event);

// The front block is done (or repeats); false when playback should stop for this grain.
bool FinishBlock(Voice& v) {
    auto& b = v.blocks.front();
    if (b.info.numRepeats != 0) {
        if (b.info.numRepeats != UINT32_MAX) {
            b.info.numRepeats--;
        }
        b.cursor = 0;
        b.started = false;
        if (v.waveformType == WAVE_ATRAC9) {
            b.dataCursor = 0;
            RestartAtrac9(v);
        }
        if (v.waveformType == WAVE_VAG) {
            v.vag = Vag{};
        }
        return true;
    }
    v.waveformEnd = v.waveformType == WAVE_ATRAC9 ? b.data + b.dataCursor
                    : b.data ? b.data + std::min<u64>(b.info.dataSize,
                                                      (static_cast<u64>(b.info.numSkipSamples) + b.info.numSamples) *
                                                          v.channels * SampleBytesX28(v.waveformType) / 28)
                             : nullptr;
    if (v.waveformType == WAVE_VAG) {
        v.vag = Vag{};
    }
    v.blocks.pop_front();
    return true;
}

bool GatherSuperframe(Voice& v) {
    auto& a = v.atrac9;
    while (a.input.size() < a.superframeBytes) {
        if (v.blocks.empty()) {
            return false;
        }
        auto& b = v.blocks.front();
        if (!b.started) {
            if (b.info.numSamples != 0) {
                a.input.clear();
                a.remainingSamples = b.info.numSamples;
                a.skipSamples = b.info.numSkipSamples;
            } else if (a.remainingSamples == 0) {
                // Data-only block without a waveform to continue: drop it.
                v.blocks.pop_front();
                continue;
            }
            b.started = true;
        }
        const u32 avail = b.info.dataSize > b.dataCursor ? b.info.dataSize - b.dataCursor : 0;
        const auto count = std::min<size_t>(a.superframeBytes - a.input.size(), avail);
        if (count) {
            a.input.insert(a.input.end(), b.data + b.dataCursor, b.data + b.dataCursor + count);
            b.dataCursor += static_cast<u32>(count);
            v.decodedBytes += count;
        }
        if (a.input.size() == a.superframeBytes) {
            return true;
        }
        // Block data ran out mid-superframe: a streamed continuation must follow.
        if (b.info.numSamples != 0 && a.remainingSamples != 0 && v.acceptsBlocks && v.blocks.size() == 1) {
            // Keep the partial input; the voice waits (silence) for appended data.
            v.waveformEnd = b.data + b.dataCursor;
            v.blocks.pop_front();
            return false;
        }
        FinishBlock(v);
    }
    return true;
}

bool DecodeSuperframe(Voice& v) {
    auto& a = v.atrac9;
    if (a.broken || !GatherSuperframe(v)) {
        return false;
    }
    if (a.windowCursor != 0) {
        a.window.erase(a.window.begin(),
                       a.window.begin() + static_cast<std::ptrdiff_t>(a.windowCursor) * v.channels);
        a.windowCursor = 0;
    }
    const u32 sfSamples = a.frameSamples * a.framesInSuperframe;
    const size_t end = a.window.size();
    a.window.resize(end + static_cast<size_t>(sfSamples) * v.channels);
    size_t consumed = 0;
    for (u32 frame = 0; frame < a.framesInSuperframe; ++frame) {
        int used = 0;
        const int status = Atrac9Decode(a.decoder, a.input.data() + consumed, a.pcm.data(), &used);
        if (status != 0 || used <= 0 || consumed + static_cast<size_t>(used) > a.superframeBytes) {
            LogOnce(0xa9200000ull | static_cast<u32>(status), "ATRAC9 decode error %d", status);
            a.broken = true;
            a.window.resize(end);
            a.input.clear();
            return false;
        }
        consumed += static_cast<size_t>(used);
        float* dst = a.window.data() + end + static_cast<size_t>(frame) * a.frameSamples * v.channels;
        const size_t n = static_cast<size_t>(a.frameSamples) * v.channels;
        for (size_t i = 0; i < n; i++) {
            dst[i] = a.pcm[i] * (1.0f / 32768.0f);
        }
    }
    a.input.clear();
    const u32 skipped = std::min(a.skipSamples, sfSamples);
    const u32 samples = std::min(a.remainingSamples, sfSamples - skipped);
    a.skipSamples -= skipped;
    a.remainingSamples -= samples;
    std::memmove(a.window.data() + end, a.window.data() + end + static_cast<size_t>(skipped) * v.channels,
                 static_cast<size_t>(samples) * v.channels * sizeof(float));
    a.window.resize(end + static_cast<size_t>(samples) * v.channels);
    if (a.remainingSamples == 0) {
        a.finishOnDrain = true;
    } else if (!v.blocks.empty() && v.blocks.front().dataCursor >= v.blocks.front().info.dataSize) {
        // This block's data is used up; the rest of the waveform comes from the next blocks.
        auto& b = v.blocks.front();
        if (b.info.numRepeats == 0) {
            v.waveformEnd = b.data + b.dataCursor;
            v.blocks.pop_front();
        }
    }
    return true;
}

size_t BufferedFrames(const Voice& v) {
    return v.channels ? v.atrac9.window.size() / v.channels - v.atrac9.windowCursor : 0;
}

bool EnsureFrames(Voice& v, size_t count) {
    auto& a = v.atrac9;
    while (v.state == Play::Playing && BufferedFrames(v) < count) {
        if (a.finishOnDrain) {
            if (BufferedFrames(v) != 0) {
                return false;
            }
            a.finishOnDrain = false;
            if (!v.blocks.empty()) {
                FinishBlock(v);
            }
            continue;
        }
        if (!DecodeSuperframe(v)) {
            return false;
        }
    }
    return v.state == Play::Playing && BufferedFrames(v) >= count;
}

void AdvanceAtrac9(Voice& v) {
    while ((v.phase >> 32) != 0 && v.state == Play::Playing) {
        if (!EnsureFrames(v, 1)) {
            // Out of data: drop the whole-frame part so a stream does not race ahead.
            stat_dry.fetch_add(1, std::memory_order_relaxed);
            v.phase &= 0xffffffffull;
            return;
        }
        const u64 count = std::min<u64>(v.phase >> 32, BufferedFrames(v));
        v.atrac9.windowCursor += static_cast<u32>(count);
        v.decodedSamples += count;
        v.phase -= count << 32;
    }
}

float EnvelopeStep(Voice& v);

void RenderAtrac9(Voice& v, u32 grain, u32 systemRate) {
    const u64 step = static_cast<u64>(
        std::llround(v.sampleRate * static_cast<double>(v.pitch) / systemRate * 4294967296.0));
    u32 written = 0;
    for (; written < grain; ++written) {
        AdvanceAtrac9(v);
        if (v.state != Play::Playing || !EnsureFrames(v, 1)) {
            break;
        }
        const float fraction = static_cast<float>(static_cast<double>(v.phase & 0xffffffffull) / 4294967296.0);
        if (fraction != 0.0f) {
            EnsureFrames(v, 2);
        }
        const float* cur = v.atrac9.window.data() + static_cast<size_t>(v.atrac9.windowCursor) * v.channels;
        const float* nxt = BufferedFrames(v) > 1 ? cur + v.channels : cur;
        const float env = EnvelopeStep(v);
        for (u32 c = 0; c < v.channels; ++c) {
            v.samples[static_cast<size_t>(c) * grain + written] = (cur[c] + (nxt[c] - cur[c]) * fraction) * env;
        }
        v.phase += step;
    }
    v.hasSamples = written != 0;
    AdvanceAtrac9(v);
}

constexpr float VAG_FILTERS[5][2] = {
    {0.0f, 0.0f}, {60.0f / 64.0f, 0.0f}, {115.0f / 64.0f, -52.0f / 64.0f}, {98.0f / 64.0f, -55.0f / 64.0f},
    {122.0f / 64.0f, -60.0f / 64.0f}};

void DecodeVagFrame(Voice& v, const Block& b, s64 frame) {
    auto& g = v.vag;
    for (u32 c = 0; c < v.channels; c++) {
        std::memcpy(g.previous[c], g.current[c], sizeof(g.current[c]));
        const u64 offset = (static_cast<u64>(frame) * v.channels + c) * 16;
        if (offset + 16 > b.info.dataSize) {
            std::fill(std::begin(g.current[c]), std::end(g.current[c]), 0.0f);
            continue;
        }
        const u8* f = b.data + offset;
        const u32 shift = f[0] & 0xf;
        const u32 predictor = std::min<u32>((f[0] >> 4) & 0xf, 4);
        const float k0 = VAG_FILTERS[predictor][0];
        const float k1 = VAG_FILTERS[predictor][1];
        for (u32 i = 0; i < 28; i++) {
            const u8 d = f[2 + i / 2];
            const s32 nibble = (i & 1) ? d >> 4 : d & 0xf;
            const s32 raw = static_cast<s16>(static_cast<u16>(nibble << 12)) >> std::min<u32>(shift, 12);
            float sample = static_cast<float>(raw) + g.hist1[c] * k0 + g.hist2[c] * k1;
            sample = std::clamp(sample, -32768.0f, 32767.0f);
            g.hist2[c] = g.hist1[c];
            g.hist1[c] = sample;
            g.current[c][i] = sample * (1.0f / 32768.0f);
        }
    }
    g.frame = frame;
}

float ReadVag(Voice& v, const Block& b, u32 frameIndex, u32 channel) {
    auto& g = v.vag;
    const u64 s = static_cast<u64>(b.info.numSkipSamples) + frameIndex;
    const s64 frame = static_cast<s64>(s / 28);
    const u32 i = static_cast<u32>(s % 28);
    if (g.data != b.data || frame < g.frame - 1) {
        g = Vag{};
        g.data = b.data;
    }
    if (frame == g.frame - 1) {
        return g.previous[channel][i];
    }
    while (g.frame < frame) {
        DecodeVagFrame(v, b, g.frame + 1);
    }
    return g.current[channel][i];
}

float ReadPcm(Voice& v, const Block& b, u32 frame, u32 channel) {
    if (!b.data) {
        return 0.0f;
    }
    if (v.waveformType == WAVE_VAG) {
        return ReadVag(v, b, frame, channel);
    }
    const u64 index = (static_cast<u64>(b.info.numSkipSamples) + frame) * v.channels + channel;
    if (v.waveformType == WAVE_PCM_F32L) {
        if ((index + 1) * 4 > b.info.dataSize) {
            return 0.0f;
        }
        float s;
        std::memcpy(&s, b.data + index * 4, 4);
        return s;
    }
    if ((index + 1) * 2 > b.info.dataSize) {
        return 0.0f;
    }
    s16 s;
    std::memcpy(&s, b.data + index * 2, 2);
    return s * (1.0f / 32768.0f);
}

void RenderPcm(Voice& v, u32 grain, u32 systemRate) {
    const bool known = v.waveformType == WAVE_PCM_I16L || v.waveformType == WAVE_PCM_F32L ||
                       v.waveformType == WAVE_VAG;
    const u64 step = static_cast<u64>(
        std::llround(v.sampleRate * static_cast<double>(v.pitch) / systemRate * 4294967296.0));
    u32 written = 0;
    for (; written < grain && v.state == Play::Playing && !v.blocks.empty(); written++) {
        auto& b = v.blocks.front();
        const float fraction = static_cast<float>(static_cast<double>(v.phase & 0xffffffffull) / 4294967296.0);
        const float env = EnvelopeStep(v);
        for (u32 c = 0; c < v.channels; c++) {
            float s = 0.0f;
            if (known) {
                const float cur = ReadPcm(v, b, b.cursor, c);
                float nxt = cur;
                if (b.cursor + 1 < b.info.numSamples) {
                    nxt = ReadPcm(v, b, b.cursor + 1, c);
                }
                s = cur + (nxt - cur) * fraction;
            }
            v.samples[static_cast<size_t>(c) * grain + written] = s * env;
        }
        v.phase += step;
        u64 frames = v.phase >> 32;
        v.phase &= 0xffffffffull;
        while (frames && !v.blocks.empty()) {
            auto& front = v.blocks.front();
            const u64 left = front.info.numSamples - front.cursor;
            const u64 take = std::min(frames, left);
            front.cursor += static_cast<u32>(take);
            v.decodedSamples += take;
            v.decodedBytes += take * v.channels * SampleBytesX28(v.waveformType) / 28;
            frames -= take;
            if (front.cursor >= front.info.numSamples) {
                FinishBlock(v);
            }
        }
    }
    v.hasSamples = written != 0;
}

// --- Envelope ------------------------------------------------------------------------------

float EnvelopeStep(Voice& v) {
    auto& e = v.envelope;
    switch (e.phase) {
    case Envelope::None:
    case Envelope::Sustain:
        return e.height;
    case Envelope::Done:
        return 0.0f;
    case Envelope::Forward:
    case Envelope::Release: {
        auto& points = e.phase == Envelope::Forward ? e.forward : e.release;
        while (e.segment < points.size() && e.position >= points[e.segment].duration) {
            e.start = points[e.segment].height;
            e.height = e.start;
            e.segment++;
            e.position = 0;
        }
        if (e.segment >= points.size()) {
            if (e.phase == Envelope::Forward) {
                e.phase = Envelope::Sustain;
                return e.height;
            }
            e.phase = Envelope::Done;
            e.height = 0.0f;
            return 0.0f;
        }
        const auto& p = points[e.segment];
        const float t = static_cast<float>(e.position) / static_cast<float>(p.duration);
        e.height = e.start + (p.height - e.start) * t;
        e.position++;
        return e.height;
    }
    }
    return 1.0f;
}

void StartEnvelope(Voice& v) {
    auto& e = v.envelope;
    e.segment = 0;
    e.position = 0;
    if (e.forward.empty()) {
        e.phase = Envelope::None;
        e.height = 1.0f;
        return;
    }
    e.phase = Envelope::Forward;
    e.start = 0.0f;
    e.height = 0.0f;
}

// --- Voice control -------------------------------------------------------------------------

void SetEvent(Voice& v, u32 event) {
    switch (event) {
    case EVENT_PLAY:
        if (v.state == Play::Empty || v.state == Play::Stopped) {
            stat_plays.fetch_add(1, std::memory_order_relaxed);
            v.state = Play::Playing;
            if (IsSampler(v)) {
                StartEnvelope(v);
            }
            v.userFxFlags = 1;
        }
        break;
    case EVENT_STOP:
        if (v.state == Play::Playing || v.state == Play::Paused) {
            if (IsSampler(v) && !v.envelope.release.empty() && v.state == Play::Playing) {
                auto& e = v.envelope;
                e.phase = Envelope::Release;
                e.segment = 0;
                e.position = 0;
                e.start = e.height;
                v.state = Play::Stopped; // keeps rendering until the release ends
            } else {
                v.state = Play::Stopped;
                v.envelope.phase = Envelope::Done;
            }
        }
        break;
    case EVENT_STOP_IMM:
    case EVENT_KILL:
        v.state = Play::Empty;
        if (v.reverb) {
            v.reverb->reset = true;
        }
        break;
    case EVENT_PAUSE:
        if (v.state == Play::Playing) {
            v.state = Play::Paused;
        }
        break;
    case EVENT_RESUME:
        if (v.state == Play::Paused) {
            v.state = Play::Playing;
        }
        break;
    default:
        LogOnce(0xe0000000ull | event, "unknown voice event %u", event);
        break;
    }
    v.stateFlags = static_cast<u32>(v.state);
}

void ResetSampler(Voice& v) {
    v.state = Play::Empty;
    v.stateFlags = 0;
    v.ports = {};
    for (auto& m : v.matrices) {
        m.clear();
    }
    v.channels = 0;
    v.sampleRate = 0;
    v.waveformType = 0;
    v.pitch = 1.0f;
    v.phase = 0;
    v.blocks.clear();
    v.acceptsBlocks = true;
    v.decodedSamples = 0;
    v.decodedBytes = 0;
    v.waveformEnd = nullptr;
    v.envelope = {};
    ReleaseAtrac9(v.atrac9);
    v.atrac9 = {};
    v.vag = Vag{};
    // The game sets a UserFx after the setup, only on its positional voices.
    v.userFx = nullptr;
    v.userFxData = {};
    v.userFxFlags = 0;
}

void SetupSampler(Voice& v, const WaveformFormat& f) {
    ResetSampler(v);
    if (f.type == 0 && f.channels == 0 && f.sampleRate == 0) {
        return;
    }
    v.channels = std::clamp<u32>(f.channels, 1, MAX_CHANNELS);
    v.sampleRate = f.sampleRate ? f.sampleRate : 48000;
    v.waveformType = f.type;
    if (f.type == WAVE_ATRAC9) {
        SetupAtrac9(v, f);
    } else if (f.type != WAVE_PCM_I16L && f.type != WAVE_PCM_F32L && f.type != WAVE_VAG) {
        LogOnce(0xf0000000ull | f.type, "waveform type 0x%x plays as silence", f.type);
    }
}

void AddBlocks(Voice& v, const u8* p, u32 size) {
    if (size < 32) {
        return;
    }
    const u8* data;
    std::memcpy(&data, p + 8, 8);
    u32 flags, count;
    std::memcpy(&flags, p + 16, 4);
    std::memcpy(&count, p + 20, 4);
    const WaveformBlock* blocks;
    std::memcpy(&blocks, p + 24, 8);
    if (flags & BLOCKS_RESET) {
        v.blocks.clear();
        v.phase = 0;
        v.waveformEnd = nullptr;
        if (v.waveformType == WAVE_ATRAC9) {
            RestartAtrac9(v);
        }
    }
    v.acceptsBlocks = (flags & BLOCKS_CONTINUE) != 0;
    if (!count || !blocks || !v.channels) {
        return;
    }
    for (u32 i = 0; i < count; i++) {
        WaveformBlock b = blocks[i];
        if (flags & BLOCKS_APPEND) {
            // More data for the waveform already playing (streamed music and voices).
            if (b.dataSize == 0) {
                continue;
            }
            b.numSamples = 0;
            b.numSkipSamples = 0;
            b.numRepeats = 0;
        } else if (b.numSamples == 0 && b.dataSize == 0) {
            continue;
        }
        Block block;
        block.data = data ? data + b.dataOffset : nullptr;
        block.info = b;
        if (!block.data && !(flags & BLOCKS_SILENCE)) {
            continue;
        }
        if (Trace()) {
            static std::map<u32, int> shown;
            if (shown[v.waveformType]++ < 3) {
                std::printf("NGS2 block type 0x%x ch %u: flags 0x%x size %u samples %u skip %u repeats %u:",
                            v.waveformType, v.channels, flags, b.dataSize, b.numSamples, b.numSkipSamples,
                            b.numRepeats);
                for (u32 k = 0; block.data && k < std::min<u32>(48, b.dataSize); k++) {
                    std::printf(" %02x", block.data[k]);
                }
                std::printf("\n");
                std::fflush(stdout);
            }
        }
        if (v.waveformType != WAVE_ATRAC9 && (flags & BLOCKS_APPEND)) {
            // PCM stream piece: plays its own frames.
            block.info.numSamples = static_cast<u32>(u64{b.dataSize} * 28 / (u64{v.channels} * SampleBytesX28(v.waveformType)));
        }
        v.blocks.push_back(block);
    }
}

void SetMatrix(Voice& v, const u8* p, u32 size) {
    if (size < 24) {
        return;
    }
    u32 id, n;
    const float* levels;
    std::memcpy(&id, p + 8, 4);
    std::memcpy(&n, p + 12, 4);
    std::memcpy(&levels, p + 16, 8);
    if (id >= MAX_MATRICES) {
        return;
    }
    auto& m = v.matrices[id];
    if (!n || !levels) {
        m.clear();
        return;
    }
    n = std::min<u32>(n, MAX_CHANNELS * MAX_CHANNELS);
    m.resize(n);
    for (u32 i = 0; i < n; i++) {
        m[i] = std::isnan(levels[i]) ? 0.0f : std::clamp(levels[i], -4.0f, 4.0f);
    }
}

void Patch(Voice& v, const u8* p, u32 size) {
    if (size < 24) {
        return;
    }
    u32 port, input;
    uintptr_t dest;
    std::memcpy(&port, p + 8, 4);
    std::memcpy(&input, p + 12, 4);
    std::memcpy(&dest, p + 16, 8);
    if (port >= MAX_PORTS) {
        return;
    }
    Voice* target = Find<Voice>(dest, MAGIC_VOICE);
    if (!target) {
        if (auto* rack = Find<Rack>(dest, MAGIC_RACK)) {
            if (input < rack->voices.size()) {
                target = rack->voices[input].get();
            } else if (!rack->voices.empty()) {
                target = rack->voices.front().get();
            }
        }
    }
    if (target && (IsSampler(*target) || target == &v)) {
        target = nullptr;
    }
    if (dest && !target) {
        LogOnce(0xd0000000ull | (dest & 0xfffffff), "patch to unknown handle %p", reinterpret_cast<void*>(dest));
    }
    v.ports[port].dest = target;
}

void SetEnvelope(Voice& v, const u8* p, u32 size) {
    if (size < 24) {
        return;
    }
    u32 nf, nr;
    const EnvelopePoint* points;
    std::memcpy(&nf, p + 8, 4);
    std::memcpy(&nr, p + 12, 4);
    std::memcpy(&points, p + 16, 8);
    auto& e = v.envelope;
    e.forward.clear();
    e.release.clear();
    if (points) {
        nf = std::min<u32>(nf, 16);
        nr = std::min<u32>(nr, 16);
        e.forward.assign(points, points + nf);
        e.release.assign(points + nf, points + nf + nr);
    }
    if (Trace()) {
        static int shown;
        if (shown++ < 6) {
            std::printf("NGS2 envelope %u+%u:", nf, nr);
            for (u32 i = 0; points && i < nf + nr; i++) {
                std::printf(" (curve %u, %u, %.3f)", points[i].curve, points[i].duration, points[i].height);
            }
            std::printf("\n");
            std::fflush(stdout);
        }
    }
    if (v.state == Play::Playing && e.phase == Envelope::None) {
        StartEnvelope(v);
    }
}

void ApplyParam(Voice& v, const ParamHead& head) {
    const u8* p = reinterpret_cast<const u8*>(&head);
    const u32 size = head.size;
    auto u32At = [&](u32 off) {
        u32 x = 0;
        if (off + 4 <= size) {
            std::memcpy(&x, p + off, 4);
        }
        return x;
    };
    auto f32At = [&](u32 off) {
        float x = 0;
        if (off + 4 <= size) {
            std::memcpy(&x, p + off, 4);
        }
        return x;
    };
    switch (head.id) {
    case PARAM_MATRIX_LEVELS:
        SetMatrix(v, p, size);
        return;
    case PARAM_PORT_VOLUME:
        if (u32At(8) < MAX_PORTS) {
            v.ports[u32At(8)].volume = f32At(12);
        }
        return;
    case PARAM_PORT_MATRIX: {
        const u32 port = u32At(8);
        const s32 m = static_cast<s32>(u32At(12));
        if (port < MAX_PORTS && m >= -1 && m < static_cast<s32>(MAX_MATRICES)) {
            v.ports[port].matrix = m;
        }
        return;
    }
    case PARAM_PORT_DELAY:
        return; // delays are not modelled
    case PARAM_PATCH:
        Patch(v, p, size);
        return;
    case PARAM_EVENT:
        if (Trace()) {
            LogOnce(0xe1000000ull | (u64{v.rack->rackId} << 8) | u32At(8), "event %u on rack 0x%x", u32At(8),
                    v.rack->rackId);
        }
        SetEvent(v, u32At(8));
        return;
    case PARAM_CALLBACK:
        LogOnce(0xc0000000ull, "voice callbacks are not called");
        return;
    default:
        break;
    }
    switch (head.id) {
    case SAMPLER_SETUP:
    case CUSTOM_SAMPLER_SETUP:
        if (size >= 32) {
            WaveformFormat f;
            std::memcpy(&f, p + 8, sizeof(f));
            SetupSampler(v, f);
            if (Trace()) {
                LogOnce(0x50000000ull | (u64{f.type} << 8) | f.channels, "sampler format 0x%x, %u ch, %u Hz",
                        f.type, f.channels, f.sampleRate);
            }
        }
        return;
    case SAMPLER_ADD_BLOCKS:
    case CUSTOM_SAMPLER_ADD_BLOCKS:
        AddBlocks(v, p, size);
        return;
    case SAMPLER_EXIT_LOOP:
    case CUSTOM_SAMPLER_EXIT_LOOP:
        if (!v.blocks.empty()) {
            v.blocks.front().info.numRepeats = 0;
        }
        return;
    case SAMPLER_PITCH:
    case CUSTOM_SAMPLER_PITCH: {
        const float r = f32At(8);
        if (std::isfinite(r) && r > 0.0f) {
            v.pitch = std::min(r, 16.0f);
        }
        return;
    }
    case SAMPLER_ENVELOPE:
        SetEnvelope(v, p, size);
        return;
    case SAMPLER_FILTER:
    case SUBMIXER_FILTER:
        if (Trace()) {
            LogOnce(0xf1000000ull | (u64{u32At(16)} << 8) | u32At(12), "filter type %u location %u ignored",
                    u32At(16), u32At(12));
        }
        return; // equalizer filters are not modelled
    case SUBMIXER_SETUP:
    case MASTERING_SETUP:
        v.channels = std::clamp<u32>(u32At(8), 1, MAX_CHANNELS);
        return;
    case CUSTOM_SUBMIXER_SETUP:
        v.channels = std::clamp<u32>(u32At(12) ? u32At(12) : u32At(8), 1, MAX_CHANNELS);
        return;
    case SAMPLER_USER_FX:
    case SUBMIXER_USER_FX:
        if (size >= 40) {
            std::memcpy(&v.userFx, p + 8, 8);
            std::memcpy(v.userFxData.data(), p + 16, 24);
            v.userFxFlags = 1;
        }
        return;
    case REVERB_SETUP:
        v.channels = std::clamp<u32>(u32At(12) ? u32At(12) : u32At(8), 1, MAX_CHANNELS);
        v.reverb = std::make_unique<Reverb>();
        SetReverbParams(*v.reverb, SETUP_REVERB);
        return;
    case REVERB_I3DL2:
        if (size >= 8 + sizeof(I3DL2)) {
            if (!v.reverb) {
                v.reverb = std::make_unique<Reverb>();
            }
            I3DL2 params;
            std::memcpy(&params, p + 8, sizeof(params));
            SetReverbParams(*v.reverb, params);
        }
        return;
    case MASTERING_LIMITER:
        v.limiter = u32At(8) != 0;
        v.limiterThreshold = f32At(12) > 0.0f ? f32At(12) : 1.0f;
        return;
    case MASTERING_GAIN:
        v.fbwLevel = f32At(8);
        v.lfeLevel = f32At(12);
        return;
    case MASTERING_OUTPUT:
        v.outputId = u32At(8);
        return;
    default:
        break;
    }
    if ((head.id & 0xffff0000u) == 0x40000000u) {
        return; // custom rack module parameters: the modules pass audio through
    }
    LogOnce(0xa0000000ull | head.id, "voice param 0x%x (size %u, rack 0x%x) ignored", head.id, size,
            v.rack->rackId);
}

// --- Render --------------------------------------------------------------------------------

constexpr float HALF = std::numbers::sqrt2_v<float> / 2.0f;
constexpr float QUARTER = std::numbers::sqrt2_v<float> / 4.0f;

struct Row {
    float mono;
    float stereo[2];
    float s51[6];
    float s71[8];
};
constexpr Row MONO_CENTER{1.0f, {HALF, HALF}, {0, 0, 1, 0, 0, 0}, {0, 0, 1, 0, 0, 0, 0, 0}};
constexpr Row FRONT_LEFT{HALF, {1, 0}, {1, 0, 0, 0, 0, 0}, {1, 0, 0, 0, 0, 0, 0, 0}};
constexpr Row FRONT_RIGHT{HALF, {0, 1}, {0, 1, 0, 0, 0, 0}, {0, 1, 0, 0, 0, 0, 0, 0}};
constexpr Row LFE_ROW{0.0f, {0, 0}, {0, 0, 0, 1, 0, 0}, {0, 0, 0, 1, 0, 0, 0, 0}};
constexpr Row QUAD_LEFT{0.5f, {HALF, 0}, {0, 0, 0, 0, 1, 0}, {0, 0, 0, 0, 1, 0, 0, 0}};
constexpr Row QUAD_RIGHT{0.5f, {0, HALF}, {0, 0, 0, 0, 0, 1}, {0, 0, 0, 0, 0, 1, 0, 0}};
constexpr Row FIVE_SL{0.5f, {0, 0}, {0, 0, 0, 0, 1, 0}, {0, 0, 0, 0, 1, 0, 0, 0}};
constexpr Row FIVE_SR{0.5f, {0, 0}, {0, 0, 0, 0, 0, 1}, {0, 0, 0, 0, 0, 1, 0, 0}};
constexpr Row SL{0.5f, {0.5f, 0}, {0, 0, 0, 0, 1, 0}, {0, 0, 0, 0, 1, 0, 0, 0}};
constexpr Row SR{0.5f, {0, 0.5f}, {0, 0, 0, 0, 0, 1}, {0, 0, 0, 0, 0, 1, 0, 0}};
constexpr Row BC{QUARTER, {QUARTER, QUARTER}, {0, 0, 0, 0, HALF, HALF}, {0, 0, 0, 0, 0, 0, HALF, HALF}};
constexpr Row BL{0.5f, {0.5f, 0}, {0, 0, 0, 0, 1, 0}, {0, 0, 0, 0, 0, 0, 1, 0}};
constexpr Row BR{0.5f, {0, 0.5f}, {0, 0, 0, 0, 0, 1}, {0, 0, 0, 0, 0, 0, 0, 1}};
constexpr Row DEFAULT_MAP[MAX_CHANNELS][MAX_CHANNELS] = {
    {MONO_CENTER},
    {FRONT_LEFT, FRONT_RIGHT},
    {FRONT_LEFT, FRONT_RIGHT, LFE_ROW},
    {FRONT_LEFT, FRONT_RIGHT, QUAD_LEFT, QUAD_RIGHT},
    {FRONT_LEFT, FRONT_RIGHT, MONO_CENTER, FIVE_SL, FIVE_SR},
    {FRONT_LEFT, FRONT_RIGHT, MONO_CENTER, LFE_ROW, SL, SR},
    {FRONT_LEFT, FRONT_RIGHT, MONO_CENTER, LFE_ROW, SL, SR, BC},
    {FRONT_LEFT, FRONT_RIGHT, MONO_CENTER, LFE_ROW, SL, SR, BL, BR},
};

float DefaultLevel(u32 srcChannels, u32 src, u32 dstChannels, u32 dst) {
    const Row& row = DEFAULT_MAP[srcChannels - 1][src];
    switch (dstChannels) {
    case 1:
        return row.mono;
    case 2:
        return row.stereo[dst];
    case 6:
        return row.s51[dst];
    case 8:
        return row.s71[dst];
    default:
        return src == dst ? 1.0f : 0.0f;
    }
}

void RenderVoice(Voice& v, u32 grain, u32 rate);

void MixPort(Voice& dest, const Voice& src, const Port& port, u32 grain) {
    const auto* matrix = port.matrix < 0 || src.matrices[port.matrix].empty() ? nullptr : &src.matrices[port.matrix];
    const u32 outputs = matrix ? std::min<u32>(dest.channels, static_cast<u32>(matrix->size() / src.channels))
                               : dest.channels;
    for (u32 d = 0; d < outputs; d++) {
        for (u32 s = 0; s < src.channels; s++) {
            const float level =
                port.volume * (matrix ? (*matrix)[d * src.channels + s] : DefaultLevel(src.channels, s, dest.channels, d));
            if (level == 0.0f) {
                continue;
            }
            float* out = dest.samples.data() + static_cast<size_t>(d) * grain;
            const float* in = src.samples.data() + static_cast<size_t>(s) * grain;
            for (u32 i = 0; i < grain; i++) {
                out[i] += in[i] * level;
            }
        }
    }
}

// The voice's UserFx callback (guest code) over its planar grain; flags 1 on the first grain after
// a start.
void CallUserFx(Voice& v, u32 grain, u32 rate) {
    float* channels[MAX_CHANNELS];
    for (u32 c = 0; c < v.channels; c++) {
        channels[c] = v.samples.data() + static_cast<size_t>(c) * grain;
    }
    UserFxContext ctx{channels, v.userFxData[0], v.userFxData[1], v.userFxData[2],
                      v.userFxFlags, v.channels, grain, rate};
    v.userFx(&ctx);
    v.userFxFlags = 0;
}

void RenderVoice(Voice& v, u32 grain, u32 rate) {
    if (v.rendered || v.rendering) {
        return;
    }
    v.rendering = true;
    v.hasSamples = false;
    const bool active = (v.state == Play::Playing || v.state == Play::Stopped) && v.channels != 0;
    if (active) {
        v.samples.assign(static_cast<size_t>(grain) * v.channels, 0.0f);
        if (IsSampler(v)) {
            if (v.state == Play::Stopped && v.envelope.phase != Envelope::Release) {
                // stopped without a release: silent, ends after this render
            } else {
                const Play before = v.state;
                if (v.state == Play::Stopped) {
                    v.state = Play::Playing; // play the release
                }
                if (v.waveformType == WAVE_ATRAC9) {
                    RenderAtrac9(v, grain, rate);
                } else {
                    RenderPcm(v, grain, rate);
                }
                if (v.state == Play::Playing) {
                    v.state = before;
                }
            }
            // The game's sampler UserFx hands each grain of its positional voices to Audio3d
            // objects (sndx: sceAudio3dObjectSetAttributes with the PCM, position and gain).
            if (v.userFx) {
                CallUserFx(v, grain, rate);
            }
        } else {
            for (auto& [src, port] : v.sources) {
                RenderVoice(*src, grain, rate);
                if (!src->hasSamples || port->volume == 0.0f) {
                    continue;
                }
                MixPort(v, *src, *port, grain);
                v.hasSamples = true;
            }
            if (v.reverb) {
                v.hasSamples = ProcessReverb(*v.reverb, v.samples, v.channels, grain, rate);
            }
            if (v.userFx && v.hasSamples && UserFxEnabled()) {
                CallUserFx(v, grain, rate);
            }
        }
    }
    v.rendering = false;
    v.rendered = true;
}

void WriteOutput(const RenderBufferInfo& out, const std::vector<float>& mix) {
    if (out.waveformType == WAVE_PCM_F32L) {
        std::memcpy(out.buffer, mix.data(), mix.size() * sizeof(float));
        return;
    }
    auto* pcm = static_cast<s16*>(out.buffer);
    for (size_t i = 0; i < mix.size(); i++) {
        pcm[i] = static_cast<s16>(std::clamp(std::lrint(mix[i] * 32768.0f), -32768L, 32767L));
    }
}

void RenderSystem(System& sys, const RenderBufferInfo* buffers, u32 count) {
    const u32 grain = sys.grain;
    static const bool stats = std::getenv("SOTC_FPS") != nullptr;
    if (stats) {
        stat_renders.fetch_add(1, std::memory_order_relaxed);
        static auto last = std::chrono::steady_clock::now();
        if (const auto now = std::chrono::steady_clock::now(); now - last >= std::chrono::seconds(5)) {
            last = now;
            std::fprintf(stderr, "SOTC NGS2: %u renders, %u voices started, %u ATRAC9 out of data (5 s)\n",
                         stat_renders.exchange(0), stat_plays.exchange(0), stat_dry.exchange(0));
        }
    }
    // sotc: kept between calls (their allocations showed in the sound thread's profile).
    thread_local std::vector<Voice*> voices;
    voices.clear();
    for (auto* rack : sys.racks) {
        for (auto& v : rack->voices) {
            v->rendered = false;
            v->sources.clear();
            voices.push_back(v.get());
        }
    }
    for (auto* v : voices) {
        if (v->state == Play::Empty || v->state == Play::Paused) {
            continue;
        }
        for (const auto& port : v->ports) {
            if (port.dest) {
                port.dest->sources.emplace_back(v, &port);
            }
        }
    }
    thread_local std::vector<std::vector<float>> mixes;
    mixes.resize(std::max<size_t>(mixes.size(), count));
    for (u32 i = 0; i < count; i++) {
        mixes[i].clear();
    }
    for (auto* v : voices) {
        if (v->rack->rackId != RACK_MASTERING) {
            continue;
        }
        RenderVoice(*v, grain, sys.sampleRate);
        if (!v->hasSamples || v->outputId >= count) {
            continue;
        }
        const auto& out = buffers[v->outputId];
        const size_t bytes = out.waveformType == WAVE_PCM_F32L ? 4 : 2;
        if (!out.buffer || !out.channels || out.size < static_cast<u64>(grain) * out.channels * bytes) {
            continue;
        }
        auto& mix = mixes[v->outputId];
        if (mix.empty()) {
            mix.assign(static_cast<size_t>(grain) * out.channels, 0.0f);
        }
        for (u32 c = 0; c < v->channels; c++) {
            const bool lfe = c == 3 && (v->channels == 6 || v->channels == 8);
            float level = lfe ? v->lfeLevel : v->fbwLevel;
            const float* in = v->samples.data() + static_cast<size_t>(c) * grain;
            if (out.channels == v->channels || (v->channels == 2 && out.channels > 2)) {
                for (u32 i = 0; i < grain; i++) {
                    mix[static_cast<size_t>(i) * out.channels + c] += in[i] * level;
                }
            } else {
                // Fold into fewer channels (the 1-channel pad speaker output).
                for (u32 d = 0; d < out.channels; d++) {
                    const float l = level * DefaultLevel(v->channels, c, out.channels == 1 ? 1 : out.channels, d);
                    if (l == 0.0f) {
                        continue;
                    }
                    for (u32 i = 0; i < grain; i++) {
                        mix[static_cast<size_t>(i) * out.channels + d] += in[i] * l;
                    }
                }
            }
        }
        if (v->limiter) {
            const float t = v->limiterThreshold;
            for (auto& s : mix) {
                s = std::clamp(s, -t, t);
            }
        }
    }
    for (u32 i = 0; i < count; i++) {
        const auto& out = buffers[i];
        if (!out.buffer) {
            continue;
        }
        const size_t bytes = out.waveformType == WAVE_PCM_F32L ? 4 : 2;
        const u64 need = static_cast<u64>(grain) * out.channels * bytes;
        if (!mixes[i].empty()) {
            WriteOutput(out, mixes[i]);
        } else {
            std::memset(out.buffer, 0, std::min<u64>(need, out.size));
        }
    }
    // Voices nothing reached still play on (they advance only when rendered): render them now so
    // their waveforms progress and finish as the game expects. The game's positional voices are
    // such voices: their UserFx hands each grain to an Audio3d object.
    for (auto* v : voices) {
        if (!v->rendered && IsSampler(*v) && (v->state == Play::Playing || v->state == Play::Stopped)) {
            RenderVoice(*v, grain, sys.sampleRate);
        }
    }
    for (auto* v : voices) {
        if (IsSampler(*v)) {
            if (v->state == Play::Stopped && v->envelope.phase != Envelope::Release) {
                v->state = Play::Empty;
            }
            if (v->state == Play::Playing && v->blocks.empty() && !v->acceptsBlocks &&
                (v->waveformType != WAVE_ATRAC9 || BufferedFrames(*v) == 0)) {
                v->state = Play::Empty;
            }
            if (v->envelope.phase == Envelope::Done && v->state != Play::Empty) {
                v->state = Play::Empty;
            }
        } else if (v->state == Play::Stopped) {
            v->state = Play::Empty;
        }
        v->stateFlags = static_cast<u32>(v->state);
        if (v->state == Play::Playing && IsSampler(*v) && v->blocks.empty() && v->acceptsBlocks &&
            (v->waveformType != WAVE_ATRAC9 || BufferedFrames(*v) == 0)) {
            v->stateFlags |= STATE_EMPTY;
        }
    }
    sys.renderCount++;
}

const void* WaveformData(const Voice& v) {
    if (v.blocks.empty()) {
        return v.waveformEnd;
    }
    const auto& b = v.blocks.front();
    if (v.waveformType == WAVE_ATRAC9) {
        return b.data + b.dataCursor;
    }
    return b.data ? b.data + (static_cast<u64>(b.info.numSkipSamples) + b.cursor) * v.channels *
                                 SampleBytesX28(v.waveformType) / 28
                  : nullptr;
}

void DestroyRack(Rack* rack) {
    for (auto& v : rack->voices) {
        ReleaseAtrac9(v->atrac9);
        Objects().erase(reinterpret_cast<uintptr_t>(v.get()));
    }
    if (rack->system) {
        auto& racks = rack->system->racks;
        racks.erase(std::remove(racks.begin(), racks.end(), rack), racks.end());
        // Ports of other racks pointing into this one.
        for (auto* other : racks) {
            for (auto& v : other->voices) {
                for (auto& port : v->ports) {
                    if (port.dest && port.dest->rack == rack) {
                        port.dest = nullptr;
                    }
                }
            }
        }
    }
    Objects().erase(reinterpret_cast<uintptr_t>(rack));
    delete rack;
}

s32 CreateRack(uintptr_t sysHandle, u32 rackId, const RackOption* option, uintptr_t* out) {
    if (!out) {
        return ERROR_INVALID_OUT_ADDRESS;
    }
    auto* sys = Find<System>(sysHandle, MAGIC_SYSTEM);
    if (!sys) {
        return ERROR_INVALID_SYSTEM_HANDLE;
    }
    switch (rackId) {
    case RACK_SAMPLER:
    case RACK_SUBMIXER:
    case RACK_REVERB:
    case RACK_MASTERING:
    case RACK_CUSTOM_SAMPLER:
    case RACK_CUSTOM_SUBMIXER:
        break;
    default:
        LogOnce(0xb0000000ull | rackId, "rack id 0x%x is not known", rackId);
        return ERROR_INVALID_RACK_ID;
    }
    u32 count = option && option->size >= 40 ? option->maxVoices : 0;
    if (!count) {
        count = rackId == RACK_SAMPLER || rackId == RACK_CUSTOM_SAMPLER ? 16 : 1;
    }
    count = std::min<u32>(count, 1024);
    auto* rack = new Rack;
    rack->system = sys;
    rack->rackId = rackId;
    for (u32 i = 0; i < count; i++) {
        auto v = std::make_unique<Voice>();
        v->rack = rack;
        v->index = i;
        if (rackId == RACK_MASTERING || rackId == RACK_SUBMIXER || rackId == RACK_REVERB ||
            rackId == RACK_CUSTOM_SUBMIXER) {
            v->channels = 2;
        }
        Objects().insert(reinterpret_cast<uintptr_t>(v.get()));
        rack->voices.push_back(std::move(v));
    }
    Objects().insert(reinterpret_cast<uintptr_t>(rack));
    sys->racks.push_back(rack);
    *out = reinterpret_cast<uintptr_t>(rack);
    if (Trace()) {
        std::printf("NGS2: rack 0x%x with %u voices at %p\n", rackId, count, static_cast<void*>(rack));
        std::fflush(stdout);
    }
    return OK;
}

} // namespace

// --- Exports (System V ABI: called from guest code) ----------------------------------------

extern "C" {

NGS2_ABI s32 sotc_ngs2_system_create(const SystemOption* option, const void* allocator, uintptr_t* out) {
    (void)allocator;
    if (!out) {
        return ERROR_INVALID_OUT_ADDRESS;
    }
    std::scoped_lock l{Lock()};
    auto* sys = new System;
    if (option && option->size >= 40) {
        if (option->sampleRate) {
            sys->sampleRate = option->sampleRate;
        }
        if (option->numGrainSamples) {
            sys->grain = std::clamp<u32>(option->numGrainSamples, 64, 4096);
        }
    }
    Objects().insert(reinterpret_cast<uintptr_t>(sys));
    *out = reinterpret_cast<uintptr_t>(sys);
    std::printf("Runtime: Ngs2 system created (%s, %u Hz, %u-sample grains)\n",
                Enabled() ? "port's own sound library" : "silent audio", sys->sampleRate, sys->grain);
    std::fflush(stdout);
    return OK;
}

NGS2_ABI s32 sotc_ngs2_system_destroy(uintptr_t handle, ContextBufferInfo* outInfo) {
    std::scoped_lock l{Lock()};
    auto* sys = Find<System>(handle, MAGIC_SYSTEM);
    if (!sys) {
        return ERROR_INVALID_SYSTEM_HANDLE;
    }
    while (!sys->racks.empty()) {
        DestroyRack(sys->racks.back());
    }
    Objects().erase(handle);
    delete sys;
    if (outInfo) {
        std::memset(outInfo, 0, sizeof(*outInfo));
    }
    return OK;
}

NGS2_ABI s32 sotc_ngs2_system_render(uintptr_t handle, const RenderBufferInfo* buffers, u32 count) {
    std::scoped_lock l{Lock()};
    auto* sys = Find<System>(handle, MAGIC_SYSTEM);
    if (!sys) {
        return ERROR_INVALID_SYSTEM_HANDLE;
    }
    if (!Enabled()) {
        for (u32 i = 0; buffers && i < count; i++) {
            if (buffers[i].buffer) {
                std::memset(buffers[i].buffer, 0, buffers[i].size);
            }
        }
        return OK;
    }
    if (buffers && count) {
        RenderSystem(*sys, buffers, count);
    }
    return OK;
}

NGS2_ABI s32 sotc_ngs2_rack_query_buffer_size(u32 rackId, const RackOption* option, ContextBufferInfo* info) {
    (void)rackId;
    (void)option;
    if (!info) {
        return ERROR_INVALID_OUT_ADDRESS;
    }
    info->hostBufferSize = 0x1000;
    return OK;
}

NGS2_ABI s32 sotc_ngs2_rack_create(uintptr_t sys, u32 rackId, const RackOption* option, const void* buffer,
                                   uintptr_t* out) {
    (void)buffer;
    std::scoped_lock l{Lock()};
    return CreateRack(sys, rackId, option, out);
}

NGS2_ABI s32 sotc_ngs2_rack_destroy(uintptr_t handle, ContextBufferInfo* outInfo) {
    std::scoped_lock l{Lock()};
    auto* rack = Find<Rack>(handle, MAGIC_RACK);
    if (!rack) {
        return ERROR_INVALID_RACK_HANDLE;
    }
    DestroyRack(rack);
    if (outInfo) {
        std::memset(outInfo, 0, sizeof(*outInfo));
    }
    return OK;
}

NGS2_ABI s32 sotc_ngs2_rack_get_voice_handle(uintptr_t handle, u32 index, uintptr_t* out) {
    if (!out) {
        return ERROR_INVALID_OUT_ADDRESS;
    }
    std::scoped_lock l{Lock()};
    auto* rack = Find<Rack>(handle, MAGIC_RACK);
    if (!rack) {
        return ERROR_INVALID_RACK_HANDLE;
    }
    if (index >= rack->voices.size()) {
        return ERROR_INVALID_VOICE_HANDLE;
    }
    *out = reinterpret_cast<uintptr_t>(rack->voices[index].get());
    return OK;
}

NGS2_ABI s32 sotc_ngs2_voice_control(uintptr_t handle, const ParamHead* list) {
    std::scoped_lock l{Lock()};
    auto* v = Find<Voice>(handle, MAGIC_VOICE);
    if (!v) {
        return ERROR_INVALID_VOICE_HANDLE;
    }
    for (int guard = 0; list && guard < 256; guard++) {
        ApplyParam(*v, *list);
        if (list->next == 0) {
            break;
        }
        list = reinterpret_cast<const ParamHead*>(reinterpret_cast<const u8*>(list) + list->next);
    }
    return OK;
}

NGS2_ABI s32 sotc_ngs2_voice_get_state(uintptr_t handle, void* state, u64 size) {
    if (!state) {
        return ERROR_INVALID_OUT_ADDRESS;
    }
    std::scoped_lock l{Lock()};
    auto* v = Find<Voice>(handle, MAGIC_VOICE);
    if (!v) {
        return ERROR_INVALID_VOICE_HANDLE;
    }
    std::memset(state, 0, size);
    if (StateZero()) {
        return OK;
    }
    if (IsSampler(*v) && size >= sizeof(SamplerVoiceState)) {
        auto& s = *static_cast<SamplerVoiceState*>(state);
        s.stateFlags = v->stateFlags;
        s.envelopeHeight = v->envelope.height;
        s.numDecodedSamples = v->decodedSamples;
        s.decodedDataSize = v->decodedBytes;
        s.userData = v->blocks.empty() ? 0 : v->blocks.front().info.userData;
        s.waveformData = WaveformData(*v);
        return OK;
    }
    if (size >= 4) {
        *static_cast<u32*>(state) = v->stateFlags;
        return OK;
    }
    return ERROR_INVALID_OUT_SIZE;
}

NGS2_ABI s32 sotc_ngs2_voice_get_state_flags(uintptr_t handle, u32* flags) {
    if (!flags) {
        return ERROR_INVALID_OUT_ADDRESS;
    }
    std::scoped_lock l{Lock()};
    auto* v = Find<Voice>(handle, MAGIC_VOICE);
    if (!v) {
        return ERROR_INVALID_VOICE_HANDLE;
    }
    *flags = StateZero() ? 0 : v->stateFlags;
    return OK;
}

} // extern "C"
