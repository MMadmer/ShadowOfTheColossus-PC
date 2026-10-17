// SPDX-FileCopyrightText: Copyright 2026 shadPS4 Emulator Project
// SPDX-License-Identifier: GPL-2.0-or-later

#include <algorithm>
#include <array>
#include <bit>
#include <cstring>

#include "core/memory.h"
#include "video_core/buffer_cache/bpe_heap_guard.h"

namespace VideoCore {

namespace {
// Block header: [0] previous physical block, [8] size | flags (bits 0-2). Free blocks carry tree links after
// the header: left, right, equal-size list (two links), parent | color
constexpr u64 HeaderSize = 0x10;
constexpr u64 FreeNodeSize = 0x38;
constexpr u64 SizeMask = ~u64{7};
// The heap hands out GPU-mapped direct memory above 4 GiB (seen up to ~0x4f6 GiB); anything outside user
// space is garbage. Reads through the backing reject unmapped addresses
constexpr VAddr HeapPointerLo = 0x100000000ULL;
constexpr VAddr HeapPointerHi = 0x10000000000ULL;
constexpr u64 MaxBlockSize = 0x100000000ULL;
constexpr size_t MaxTreeNodes = 1 << 16;
constexpr size_t MaxListNodes = 1 << 10;
constexpr size_t MaxChainSteps = 1 << 12;
} // namespace

BpeHeapGuard::BpeHeapGuard(Core::MemoryManager* memory_, VAddr heap_)
    : memory{memory_}, heap{heap_} {
    u64 heap_size = 0;
    if (!Read(heap + 0x30, &heap_size, sizeof(heap_size)) || heap_size < 0x1000000 ||
        heap_size > MaxBlockSize) {
        return;
    }
    valid = true;
    CollectFreeTree();
}

bool BpeHeapGuard::Read(VAddr addr, void* out, u64 size) const {
    auto it = std::upper_bound(runs.begin(), runs.end(), addr,
                               [](VAddr value, const Run& run) { return value < run.start; });
    if (it == runs.begin() || addr - std::prev(it)->start >= std::prev(it)->size) {
        Run run{};
        run.ptr = memory->GetBackingRun(addr, &run.start, &run.size);
        if (!run.ptr) {
            // sotc: the heap object itself is a global of the eboot, outside the guest memory
            // the runtime maps (no backing view): its fields are read in place.
            if (addr >= heap && addr + size <= heap + 0x1000) {
                std::memcpy(out, std::bit_cast<const void*>(addr), size);
                return true;
            }
            return false;
        }
        it = std::next(runs.insert(it, run));
    }
    const Run& run = *std::prev(it);
    if (addr - run.start + size > run.size) {
        // Crosses into the next physical area: take the general path
        return memory->TryReadBacking(addr, out, size);
    }
    std::memcpy(out, run.ptr + (addr - run.start), size);
    return true;
}

bool BpeHeapGuard::IsHeapPointer(VAddr addr) const {
    return addr >= HeapPointerLo && addr < HeapPointerHi && (addr & 7) == 0;
}

void BpeHeapGuard::AddFreeBlock(VAddr block) {
    free_blocks.push_back(block);
}

void BpeHeapGuard::CollectFreeTree() {
    const VAddr sentinel = heap + 0x80;
    VAddr root = 0;
    Read(sentinel, &root, sizeof(root));
    VAddr pending = 0;
    if (Read(heap + 0xc0, &pending, sizeof(pending)) && IsHeapPointer(pending)) {
        AddFreeBlock(pending);
        free_spans.emplace_back(pending, pending + FreeNodeSize);
    }
    std::vector<VAddr> stack{root};
    size_t visited = 0;
    while (!stack.empty() && visited < MaxTreeNodes) {
        const VAddr node = stack.back();
        stack.pop_back();
        if (node == sentinel || !IsHeapPointer(node)) {
            continue;
        }
        std::array<u64, 7> words{};
        if (!Read(node - HeaderSize, words.data(), sizeof(words))) {
            continue;
        }
        ++visited;
        const u64 key = words[1] & SizeMask;
        if (key == 0 || key >= MaxBlockSize) {
            // Already corrupted: still keep what is left of it, but do not follow its links
            AddFreeBlock(node - HeaderSize);
            continue;
        }
        AddFreeBlock(node - HeaderSize);
        // Blocks of equal size hang off the tree node in a ring through the list links
        VAddr member = words[4];
        for (size_t i = 0; i < MaxListNodes && IsHeapPointer(member) && member != node; ++i) {
            AddFreeBlock(member - HeaderSize);
            VAddr next = 0;
            if (!Read(member + 0x10, &next, sizeof(next))) {
                break;
            }
            member = next;
        }
        stack.push_back(words[2]);
        stack.push_back(words[3]);
    }
    std::sort(free_blocks.begin(), free_blocks.end());
    free_blocks.erase(std::unique(free_blocks.begin(), free_blocks.end()), free_blocks.end());
}

bool BpeHeapGuard::NextBlock(VAddr block, VAddr& next, bool* in_use) const {
    u64 size_flags = 0;
    if (!Read(block + 8, &size_flags, sizeof(size_flags))) {
        return false;
    }
    const u64 size = size_flags & SizeMask;
    if (size == 0 || size >= MaxBlockSize) {
        return false;
    }
    next = block + size + HeaderSize;
    if (in_use) {
        *in_use = (size_flags & 1) != 0;
    }
    // The next block must point back: this rejects anything that only looks like a header
    VAddr prev = 0;
    return IsHeapPointer(next) && Read(next, &prev, sizeof(prev)) && prev == block;
}

void BpeHeapGuard::ChainStarts(VAddr lo, VAddr hi, std::vector<VAddr>& starts) const {
    // Readbacks usually start at a binding base, i.e. the user pointer of an allocation: start from its
    // header (accepted only if the next block points back to it). Else the closest block of the free tree
    // at or below the range, and every one inside it. The tree may be older than the heap: each start is
    // checked the same way.
    VAddr next = 0;
    if (lo > HeaderSize && NextBlock(lo - HeaderSize, next)) {
        starts.push_back(lo - HeaderSize);
    }
    auto it = std::upper_bound(free_blocks.begin(), free_blocks.end(), lo);
    for (auto below = it; below != free_blocks.begin();) {
        --below;
        if (NextBlock(*below, next)) {
            starts.push_back(*below);
            break;
        }
    }
    for (; it != free_blocks.end() && *it < hi; ++it) {
        if (NextBlock(*it, next)) {
            starts.push_back(*it);
        }
    }
    std::sort(starts.begin(), starts.end());
}

bool BpeHeapGuard::Covers(VAddr lo, VAddr hi) const {
    std::vector<VAddr> starts;
    ChainStarts(lo, hi, starts);
    return !starts.empty() && starts.front() <= lo;
}

void BpeHeapGuard::CollectHeaders(VAddr lo, VAddr hi, std::vector<Span>& spans) const {
    // Follow the physical chain through the range from each start. A free block (size bit 0 clear, as the
    // game's free function reads it) keeps its tree links after the header too: read from the chain itself,
    // blocks freed after the tree walk are covered as well.
    std::vector<VAddr> starts;
    ChainStarts(lo, hi, starts);
    VAddr covered_until = 0;
    for (VAddr block : starts) {
        if (block < covered_until) {
            continue;
        }
        for (size_t step = 0; step < MaxChainSteps && block < hi; ++step) {
            VAddr next = 0;
            bool in_use = true;
            const bool valid = NextBlock(block, next, &in_use);
            const VAddr span_end = block + (valid && !in_use ? FreeNodeSize : HeaderSize);
            if (span_end > lo) {
                spans.emplace_back(block, span_end);
            }
            if (!valid) {
                break;
            }
            block = next;
        }
        covered_until = block;
    }
}

std::vector<BpeHeapGuard::Span> BpeHeapGuard::CollectSpans(VAddr addr, VAddr end) {
    std::vector<Span> spans;
    // The pending block (heap+0xc0) is free but out of the tree, maybe out of the chain walk too.
    for (const auto& span : free_spans) {
        if (span.first < end && span.second > addr) {
            spans.push_back(span);
        }
    }
    CollectHeaders(addr, end, spans);
    std::sort(spans.begin(), spans.end());
    return spans;
}

u64 BpeHeapGuard::Write(VAddr addr, const u8* data, u64 size) {
    const VAddr end = addr + size;
    const std::vector<Span> spans = CollectSpans(addr, end);

    u64 differing = 0;
    VAddr cursor = addr;
    const auto write = [&](VAddr lo, VAddr hi) {
        if (hi > lo) {
            memory->TryWriteBacking(std::bit_cast<void*>(lo), data + (lo - addr), hi - lo);
        }
    };
    for (const auto& [span_lo, span_hi] : spans) {
        const VAddr lo = std::max(span_lo, addr);
        const VAddr hi = std::min(span_hi, end);
        if (hi <= lo || hi <= cursor) {
            continue;
        }
        const VAddr keep_lo = std::max(lo, cursor);
        write(cursor, keep_lo);
        std::array<u8, FreeNodeSize> current{};
        const u64 len = hi - keep_lo;
        if (Read(keep_lo, current.data(), len) &&
            std::memcmp(current.data(), data + (keep_lo - addr), len) != 0) {
            differing += len;
        }
        cursor = hi;
    }
    write(cursor, end);
    return differing;
}

} // namespace VideoCore
