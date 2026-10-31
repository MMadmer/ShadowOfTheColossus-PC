# Draw thread: a plan for a locked 30 FPS during area transitions

User request on 29 Oct 2026, after T48 (local commit `7fde988`): the big stutters are gone (worst
frame 534 ms → 58–75 ms), but every area transition still drops 1–6 frames. The previous session
concluded that the limit is structural: the draw thread (`shadPS4:GpuDraw`) shows as busy for
28–32 ms of every 33 ms frame.

**This document is only the plan.** Nothing has been implemented. Before any code, the user
approves the order and step 0.

## Order agreed with the user (29 Oct)

Outside area transitions, the game already runs at a locked 30 FPS: the typical frame is 33.3 ms
and the 5 s windows show 30.0 FPS in all logs from 29 Oct. The agreed order is:

1. **Phase 1: polish the 30 FPS.** Remove the 1–6 frames dropped in area transitions. These are
   steps 0–4 of section 4, measured at 30 FPS.
2. **Phase 2: fix the open failures.** The list comes from the ROADMAP and the user picks which
   ones go in. New since T48: a crash at `guest offset 0x49800` (0xc0000005, read of
   `ffffffffffffffff`) 24 min into the 10:00–10:24 session on 29 Oct, with the `7fde988` exe.
   It is the BPE heap allocator, next to the 0x497f0 that the heap guard protects. In the last
   window before the crash, free blocks fell from 5870 to 3432 and the guard "excused" 30 writes.
   Log and dump in `E:\...\SotC-PC\user\crashes\2026-10-29_10-24-5*`.
3. **Phase 3: publish a new version on the site** (GitHub release, without deleting the old ones),
   with the user's approval at publishing time.
4. **Phase 4: get close to 60 FPS.** At 60 FPS each frame has 16.7 ms. According to the 28 Oct
   profile in a heavy area, the draw thread spends ~22 ms of CPU per frame and the command
   processor ~17 ms (15.6 ms just reading commands). Step 0 is repeated in 60 mode. Steps 5–6
   (binds prepared off the thread, recording on several threads) become the main path, together
   with a new item: relieving the command processor.

Reference configuration (the user's real launcher, `user/launcher_settings.json` on 29 Oct):
30 FPS (`fps_mode 30`), PS4 Pro mode, Maximum textures, DLSS Ultra (1600x900 → 1920x1080), Fifo,
maximum VRAM, recording thread on, `SOTC_LOD_FEEDBACK=1 SOTC_MIP_TAIL=0`.
i9-13900K, RTX 2060 SUPER (8 GB).

## 1. What the numbers already show (and a measurement problem)

### 1.1 "draw busy" does not measure work

The line `SOTC threads per frame: draw busy` (`core/libraries/videoout/driver.cpp:777`) is
`(window time − idle time) / flips`. Idle time only counts when the draw thread's queue is
**empty** (`Liverpool::BackLoop`, `liverpool.cpp:130`). Everything the thread does *inside* an
operation counts as busy, including **waiting**:

- the flip runs on the draw thread (`SubmitFlip` → `SendOrderedCommand` →
  `SubmitFlipInternal` → `Presenter::PrepareFrame` → `GetRenderFrame`, which waits for a free
  frame and for the present fence with Fifo);
- GPU readbacks (`BeginAsyncReadback`/`FinishAsyncReadback`) and flushes requested by other
  threads (`SendCommand<true>`) run there, between two operations;
- `scheduler.Finish()`, `Scheduler::Wait`, `waitIdle` and the memory lock waits do too.

Proof in the user's log from 29 Oct (`E:\...\SotC-PC\user\last_run.log`, 293 windows of 5 s):

| windows | commands per frame | average "draw busy" |
|---|---|---|
| 4 | fewer than 500 | 28.8 ms |
| 188 | 3000 or more (up to 13,711) | 28.3 ms |

With 25 times more commands, the number does not change. In other words, it measures the frame
pace (30 FPS, locked), not how much the thread works. **"The draw thread is saturated" is not yet
proven.**

### 1.2 Real CPU time is lower, but not small

The all-threads profile from 28 Oct (`out/prof_all_trans.run.txt`, `SOTC_PROFILE=*`, ~10 s in a
heavy area, 6000 commands per frame) has the CPU time of each thread (`GetThreadTimes`):

| thread | user + kernel | per 33 ms frame (≈) |
|---|---|---|
| `shadPS4:GpuRecorder` | 8,938 + 859 ms | ~32 ms (almost all busy-waiting) |
| `shadPS4:GpuDraw` | 5,391 + 1,172 ms | ~22 ms |
| `shadPS4:GpuCommandProcessor` | 4,391 + 734 ms | ~17 ms (`command parse 15,6 ms`) |
| 4 × `BPE JobWorkerTh` (game) | ~4.2–4.5 s each | ~14 ms each |

Even those ~22 ms include busy-waiting: the draw thread spins 4096 `pause` before sleeping, and
the recorder spins too (`RECORDER_PAUSE`). In the GpuDraw samples, ~22% are inside a kernel wait
(`KERNELBASE+0x22a58`, `ntdll+0x1659c4`). Small-function names in the LTO build are not reliable
(ICF merges identical functions), so the address of a small function is not enough to blame
anyone.

Conclusion: the draw thread has real headroom only if its work is well below 22 ms; today neither
one thing nor the other can be stated.

### 1.3 What weighs on the dropped frames

The `SOTC FRAMES` line of the windows with a frame above 40 ms in the same log (the worst frame of
each window, with its cause). The timers (`SotcStats::FrameTimer`) count on any thread:

| worst frame | largest cause | others |
|---|---|---|
| 816 ms | GPU readback 599 ms | shader 90, idle 90 |
| 384 ms | GPU readback 112 | shader 60, pipeline 59, idle 77 |
| 250 ms | GPU readback 133 | idle 85, shader 25 |
| 200 ms | GPU readback 74 | idle 61, shader 36 |
| 200 ms | idle 94 | pipeline 37, readback 34 |
| 200 ms | idle 67 | buffer 23, io 17, upload 13 |
| 87–92 ms | new image 27–57 | — |
| 68–73 ms | new image 52–62 | — |
| 74–108 ms | texture upload 53–61 | alloc 7–8 |
| 159–192 ms | idle 48–136 | DMA 44, buffer 16–20, io 19 |
| 58–67 ms (the most common) | idle 10–78 | nothing above 10 ms |

"Idle" (`FrameIdle`) is the command processor waiting for the game to submit work: in those frames
the one running late was the **game**, often waiting for a GPU readback, a file (io) or the BPE
workers themselves. In several long frames, drawing was not on the critical path.

**Summary:** the dropped frames have three origins that mix: (a) long GPU readback waits by the
game, (b) one-off work on the draw thread when entering a new area (shader, pipeline, new image,
texture upload, DMA) and (c) the continuous load of 5–10 thousand commands per frame. Splitting the
draw thread helps (b) and (c), but not (a). That is why step 0 measures before changing anything.

## 2. Step 0 — measure for real (mandatory before choosing)

New switch `SOTC_DRAW_SPLIT=1` (off by default; counters only, does not change behavior).
New log line every 5 s:

```
SOTC DRAW SPLIT per frame: cpu 14.2 ms (cycles), draw 6.1, indirect 3.0, dispatch 1.2,
  bind 4.4 [buffers 1.9, textures 2.1, dma 0.4], flip 0.3 (waited 9.8), readback 0.6
  (waited 2.1), commands 0.4, other waits 1.7; recorder cpu 4.0 (spin 20.1); cp→draw drains 4
```

How to measure each part:

1. **Real CPU per thread:** `QueryThreadCycleTime` of GpuDraw, the command processor and the
   recorder, on every flip, minus the time spent in the busy-wait loops (a dedicated counter
   around `CpuRelax`/`RECORDER_PAUSE`).
2. **Time per operation type:** in `ExecuteBackOp`, accumulate per `BackOp::Kind` (Draw,
   DrawIndirect, Dispatch, Func) and, inside `Rasterizer::Draw`, the `BindResources` blocks
   (separating `BindBuffers`, `BindTextures`, `SynchronizeDmaBuffers`), `BeginRendering` and the
   recording.
3. **Waiting inside the operation:** time separately whatever blocks: `GetRenderFrame`, the
   `Scheduler::Wait`/`Finish`, the memory lock waits seen by GpuDraw, `waitIdle`. This is taken
   out of "busy".
4. **Who was on the critical path in frames > 40 ms:** store, together with the worst frame, the
   GpuDraw CPU, the command processor CPU, the draw thread queue (`back_head − back_tail`) and how
   long the game waited for GPU readbacks.

Cost: one `QueryThreadCycleTime` and `NowNs` per block, only with the switch on (like `SOTC_FPS`).

**Implemented on 29 Oct (no commit), installed on E: at 11:04.** Lines
`SOTC DRAW SPLIT per frame`, `worst frame` and `frames > 40 ms` every 5 s (`sotc_stats.h`/`.cpp`,
`driver.cpp`). The game's GPU readback waits are summed across all game threads, so they can exceed
33 ms. First test, in a light scene (~290 commands per frame): "draw busy" 28.9 ms, but the real
CPU of the draw thread is **6.8 ms**. The thread spends **25.3 ms per frame stalled on the flip**
(`GetRenderFrame`, waiting for a free presentation frame), and during that time the GPU readbacks
the game is waiting for are stalled too ("finish" phase of 28–70 ms summed across threads). This
still has to be measured in the heavy areas.

### Step 0 result — the user's route (29 Oct, 11:06–11:14)

User's launcher (30 FPS, Pro, Maximum, DLSS), shrine → load the save → two area transitions →
colossus; 86 windows of 5 s. Values per frame (ms):

| section | commands | draw CPU (spin) | command proc. CPU | binds | flip wait | game readbacks (sum) |
|---|---|---|---|---|---|---|
| shrine / menus | 150–1,500 | 6–11 (1–2) | 5–10 | 2–5 | 19–25 | 4–17 |
| field, medium areas | 1,200–3,000 | 15–17 (2.5) | 7–10 | 5–6 | 13–16 | 28–32 |
| heavy areas, 30 FPS | 4,000–7,400 | 20–24 (2) | 15–24 | 8–13 | 3–10 | 45–55 |
| frames > 40 ms in transitions (21–22 per transition) | 6,000–7,000 | 33–36 (4) | 24–39 | 13–22 | 0 | 60–70 |

Reading:

1. **In heavy areas the draw thread has 3–10 ms of headroom, not zero.** The real work is
   18–22 ms; the rest of the old "draw busy" is flip wait.
2. **On entering each new area (~1 s, 21–22 frames) it goes over the limit**: 29–32 ms of real
   work, flip wait 0, frames of 40–50 ms. Binds are the largest part (13–22 ms per frame, half of
   the draw operations); the command processor also reaches 24–39 ms in those frames.
3. **The few 100 ms frames are texture uploads inside `BindTextures`**: 108 ms (t=356) with
   textures 59 ms and "upload" 55 ms; 99 ms (t=401) with textures 64 ms and upload 56 ms, for only
   19–28 MB copied (~0.5 GB/s). One of 108 ms (t=110) was shader (59 ms) and one of 84 ms (t=85)
   was the game waiting for a file (io 172 ms).
4. GPU, submission and recorder barely wait (≤ 1.5 ms); the draw thread queue is full (512) in
   every heavy section.

### Step 1 — texture uploads (29 Oct, installed at 11:35)

Second route (11:23, upload phases measured): in the 90–175 ms frames, the upload time is almost
entirely in the **copy** from game memory to the staging area: 41–58 ms for 12–20 MB. Allocating
the staging area, detiling and recording add up to less than 9 ms. In those windows the game maps
memory for the new area (`batch_map2` holds the table lock for 330–440 ms every 5 s), and the copy
(`CopySparseSerial`) queried the table twice per contiguous chunk (`runtime_memory_region` and
`runtime_memory_backing_pointer`). Every query that the region cache could not answer waited for
the whole batch. Moving the upload to another thread would not help: it would wait just the same.

Fix: `SOTC_COPY_LOCK_ONCE=1` (`runtime_memory_copy_out`, `src/runtime_memory.c`). Chunks that the
region cache already knows are copied without a lock; the rest, under a single read lock for the
whole copy. The result is the same as before (through the `backing` view; reserved or unmapped
memory becomes zero). It also applies to buffer uploads (the "buffer" spikes of 50–90 ms). Enabled
only in the user's launcher; when off, the behavior is that of `7fde988`.

**Result (11:35 and 11:45 routes): no gain.** The new copy almost never waited for the lock
(`runtime_memory_copy_out` ~0 ms in the waits), but the 11:35 route still had two copies of
41–45 ms for 14–19 MB. The lock was not the cause. The 11:45 route (130 s, shorter) also measured
page faults: it had no slow copy; the slowest were 12.5 ms (24.5 MB) and 17.4 ms (88.5 MB), with
~5,500 process page faults in the frame, against ~200–1,500 in the others. In the menu, 25.8 MB
were copied in 0.9 ms. Provisional conclusion: the slow copy is rare (2–3 frames per long route)
and coincides with many page faults, but the cause is not proven. The averages in the heavy areas
were the same in all three routes (draw 21–22 ms, binds 9–10 ms). The ~20 frames of 40–50 ms in
each transition, which drop more frames in total, come from the binds and the command processor.

### Step 2 — draw thread profile (11:56 route, 30 s–2 min of play)

`SOTC_PROFILE=GpuDraw` (54,938 samples; data in `out/prof_draw_rota_29-10.txt`, symbolized with
`llvm-symbolizer` on the `out/gpu/bb-probe.exe` of the same build). Thread time:

| part | samples |
|---|---|
| waiting for a free frame on the flip (`GetRenderFrame`, Fifo) | 28% |
| no work (queue wait) | 9–10% |
| waiting for the GPU (`SubmitAheadAndWait`, `Semaphore::Wait`), memory lock, `yield` | ~8% |
| compute dispatches (`DispatchDirect`/`Indirect`) | 15% |
| indirect draws (`DrawIndirect`) | 8% |
| inside them: `BindBuffers` 6%, `BindTextures` 4%, `FindTexture` 3%, `SynchronizeDmaBuffers` 3% | |
| page protection (`ZwProtectVirtualMemory` via `protect_fast`/`PageManager::Protect`) | 4% |
| heap guard when applying readbacks (`BpeHeapGuard`, `FinishAsyncReadback`) | 4–6% |
| copies from game memory (`CopySparseSerial`) | 3.5% |

There is no hot spot in the binds: the work is spread across a dozen functions of 3–6% each. The
heap guard must not change its behavior. The ~20 slow frames of each transition come from the sum
of these costs on the draw thread and the command processor (which also reaches 24–39 ms in those
frames), not from a single function.

**Phase 1 conclusion (29 Oct):** what could be removed with a small, safe fix was already removed
in T48. What remains (1–6 frames per area transition, ~20 frames of 40–50 ms over ~1 s) only goes
away with the structural change of options A/C: preparing binds in parallel and recording on
several threads. It is the same work that phase 4 (60 FPS) requires, where it pays off twice. Left
off and unused: `SOTC_COPY_LOCK_ONCE` (no gain). Kept: the `SOTC_DRAW_SPLIT` measurement (counters
only).

### All-threads profile (12:14 route, 30 s–2 min of play)

`SOTC_PROFILE=*` (data in `out/prof_all_rota_29-10.txt`). Average over the whole 90 s window, with
one area transition:

| thread | waiting | main work |
|---|---|---|
| draw (`GpuDraw`) | 44% on the flip (`PrepareFrame`), ~9% no work, ~5% GPU | operations 19% (dispatch 11, indirect 10, binds 6+4), game readbacks (`FinishAsyncReadback`, heap guard) ~6%, page protection ~3% |
| command processor | 63% waiting for the game to submit, ~7% waiting for the draw thread | command parsing 21%, JIT code 20%, pipeline lookup 6% |
| game workers (`BPE JobWorkerTh`, 4) | game mutex 33%, events ~25% | **24% of the time inside GPU readback page faults** (`vectored_fault_impl` → `ReadMemory`) |

On average neither of the two GPU threads is full. The dropped frames sit in the ~1 s bursts of
each area transition, when both reach 33–40 ms at the same time (`SOTC DRAW SPLIT` counters), and
the sampling profile does not separate those bursts from the rest.

### Structural options, with honest cost and risk

| | what it is | expected gain in the bursts | size | risk |
|---|---|---|---|---|
| A1 | the command processor (63% idle on average) already reads the sharps: it starts decoding T#/V#/S#, `ImageInfo`/`ImageViewInfo` and the mapped size of the buffers together with each operation; the draw thread only looks up the caches and records | 3–6 ms per burst frame (the decoding part of the binds) | 3–5 days | medium: sharps read ahead of time (same rule as `WaitBackWrites`); in the bursts the command processor is also full, so part of the gain disappears |
| A2 | cache lookups (`FindTexture`, `ObtainBuffer`) done in batches by a preparation thread | +5–8 ms | 1–2 weeks | high: the texture and buffer caches are not thread-safe (T48 lesson: a misplaced lock created the worst stutters) |
| C | record on 2–4 threads (separate passes, secondary command buffers) | the only one that removes 10–15 ms | weeks | high: the entire rasterizer state (barriers, layouts, HUD without UI, DLSS that observes the draws) built for a single thread |
| D' | GPU readback page faults on the game threads (24% of their time) | the game submits work earlier (frames with high "idle") | large (it is the "PC memory model") | high: it runs right alongside the heap guard, which must not change its behavior |

No single option eliminates the losses: in the bursts, the draw thread and the command processor
both exceed 33 ms. A1 is the first step of A2 and of C, and the only medium-risk one.

**Decision (table above: real CPU 15–25 ms):** B first, starting with the texture uploads that
cause the 100 ms frames; then take work off the binds (option A), which is what overflows in the
~20 frames of each transition. The flip wait in front of the game's readbacks (option D) is left
for later: in the heavy sections it drops to 0–7 ms.

**Step 0 measurement:** the user plays the usual route with their launcher (30 FPS, Pro, Maximum,
DLSS), with `SOTC_FPS=1 SOTC_DRAW_SPLIT=1` in `extra_env`: shrine → ride out on horseback → two
area transitions → one colossus. I only read `user/last_run.log` afterwards. If an address-level
profile is wanted,
`SOTC_PROFILE=* SOTC_PROFILE_DELAY=<s> SOTC_PROFILE_SECONDS=10 SOTC_PROFILE_OUT=...`
in the same run, symbolized with `llvm-symbolizer` on the **unstripped** `bb-probe.exe` (only large
functions are reliable).

**Decision after step 0:**

| if the real CPU of GpuDraw in the heavy areas is... | then |
|---|---|
| less than ~15 ms | the thread is not full: go straight to options B and D (spikes and readbacks); option A is kept in reserve |
| 15–25 ms | B and D first, then A |
| more than ~25 ms | A comes in early, B and D in parallel |

## 3. Options for taking work off the draw thread

### Option A — prepare the binds off the thread (two-phase pipeline)

Today each draw does, in sequence on GpuDraw: apply registers → `BindResources`
(`BindBuffers` + `BindTextures`: look up buffers and images in the caches, synchronize, build
`set_writes`/`push_data`) → `BeginRendering` → record. `FASTBIND` already skips ~58–63% of the
repeated binds.

Proposal (`SOTC_BIND_PREP=1`): the command processor already resolves the pipeline
(`CaptureBackOp`). A "preparer" thread between it and GpuDraw computes what is **read-only**: the
FASTBIND hash, `MakeUserData`, reading the V#/T#/S# (sharps) from game memory, the image/buffer
lookup keys. GpuDraw receives the op with those keys already in place and does only the lookups
that modify the caches, plus the recording.

- Expected gain: the sharp-reading and hashing part (to be measured in step 0; estimate 20–35% of
  the bind).
- Risks:
  - the sharps are in game memory, which changes; reading them early requires the same rule as
    `WaitBackWrites` (wait for the last op that writes memory), otherwise the draw uses stale
    resources (wrong texture, flickering);
  - the texture cache and the buffer cache are **not thread-safe**; the preparer can only read
    immutable structures or ones with a generation counter (like `BindGeneration`);
  - one more queue increases the latency of the GPU readbacks the game waits for.
- What does not change: heap guard, `POOL_FIX`, pool rule (none of that is in the bind).

### Option B — take the one-off new-area work off the thread

In the dropped frames, the "new image" (27–62 ms), "upload" (53–61 ms), "buffer"/"dma"
(16–45 ms) and "hash" spikes happen **on GpuDraw**, in the middle of the frame, when a draw runs
into something that does not exist on the GPU yet.

Proposal, in three independent parts, each with its own switch:

1. `SOTC_IMAGE_AHEAD=1` — create images ahead of time: when the command processor reads a new T#
   (it already reads the sharps to find the pipeline), it requests creation of the `VkImage` + VMA
   memory on a worker thread. When the draw arrives, the image already exists; it only registers
   and uploads. Image creation is what most often shows up on its own in the 68–92 ms frames.
2. `SOTC_SYNC_WORKER=1` — the copies from game memory to the staging area (DMA, buffer uploads,
   texture `memcpy`, content hash) go to the 4 copy threads that already exist
   (`bbport_copy.cpp`). GpuDraw only records the `vkCmdCopy` after the copy has finished, as is
   already done with the upload budget (`SOTC_UPLOAD_BUDGET_MB`).
3. Shader and pipeline: `ASYNC TRANSLATE`/`ASYNC COMPUTE` already exist, but the shader spikes
   (16–90 ms) and pipeline spikes (28–59 ms) still show up on area entry. Measure which stages
   still compile on the thread (a counter per reason) and cover those cases.

- Expected gain: remove the 50–60 ms spikes that remain in transitions without a long readback;
  this is what most resembles "a frame dropped because of one big job".
- Risks:
  - **an image created for a T# that the game changes before the draw**: only memory and time are
    wasted (the image is discarded); limit how many per frame;
  - VRAM: creating ahead raises peak usage (today 5.9–6.9 GB of a 6.5 GB budget); respect the
    budget and the emergency collection;
  - a copy made before a game write: it needs the same page protection that DMA uses today (the
    write after the copy marks the page and the draw redoes it); **without it, green/garbage
    blocks appear**, and they must not come back;
  - the upload budget stays: a deferred texture stays black for one frame, as today.

### Option C — record on several threads

The most radical one: split the draws of a frame (by render pass, for example shadow / G-buffer /
post) among 2–4 bind+record threads, each with its own secondary command buffer, and the recorder
joins everything in order.

- Gain: it truly scales with 5–10 thousand commands per frame, if the bind CPU is the limit.
- Risks (high):
  - the rasterizer state (`render_state`, barriers, image layouts, `fast_bind`,
    `needs_barrier`, the HUD without UI, the UI shadow, the DLSS that observes the draws) is all
    single-threaded; splitting it requires per-thread state and a barrier merge phase;
  - the texture and buffer caches would need fine-grained locks, which may cost more than they
    gain (T48 lesson: misplaced locks created the worst stutters);
  - order of writes to game memory and GPU readbacks: a draw that writes and another that reads in
    the same pass cannot go to different threads;
  - weeks of work, and many places where the heap guard and the readback meet.
- Only worth it if, after A and B, the real CPU of GpuDraw still exceeds ~25 ms in the heavy
  areas.

### Option D (not the draw thread, but it weighs more) — shorter GPU readbacks

In the worst frames (200–816 ms) the largest cause is the game waiting for a GPU readback. The
path (`BufferCache::ReadMemory` with `SOTC_RB_ASYNC`): the game thread requests the copy on
GpuDraw (`BeginAsyncReadback`), waits for the command buffer to be submitted (`WaitSubmitted`),
waits for the GPU, and asks GpuDraw again to apply it (`FinishAsyncReadback`, where the heap guard
is).

Proposal (`SOTC_RB_PRIORITY=1`): when a readback is waiting, GpuDraw submits the command buffer
right away (today it may wait for the next periodic flush) and the final apply runs before the
next op. Measure first: in step 0, separate how much of the wait is the GpuDraw queue, how much is
submission and how much is GPU.

- Risks: more submissions per frame (driver cost); **the apply still goes through
  `ApplyDownloads` with the heap guard, without changing anything in it.**

## 4. Proposed order

Each step sits behind its own switch, off until the user approves it; each one is measured before
the next. I build, check that the game starts (without running anything while the user is
playing), install directly into `E:\GAMES\Shadow Of The Colossus\SotC-PC` with a backup of the exe
next to it and announce "installed at HH:MM".

| # | Step | Switch | Measurement (user playing, their launcher) | Done when |
|---|---|---|---|---|
| 0 | Split measurement of the draw thread | `SOTC_DRAW_SPLIT=1` | standard route (shrine → 2 area transitions → colossus); read `SOTC DRAW SPLIT` and `SOTC FRAMES` | we know the real CPU of GpuDraw and who is on the critical path of frames > 40 ms |
| 1 | Images created ahead | `SOTC_IMAGE_AHEAD=1` | same route; compare "image" in frames > 50 ms and VRAM | "image" disappears from the dropped frames, VRAM within budget, no wrong textures |
| 2 | GPU readbacks with priority | `SOTC_RB_PRIORITY=1` | same route + the end of the game (save with 15 defeated, slot 4) through the credits | worst frames drop; ending without a crash; heap guard protecting as before |
| 3 | Copies and hash on the copy threads | `SOTC_SYNC_WORKER=1` | same route; "buffer", "dma", "upload", "hash" and GpuDraw CPU | lower GpuDraw CPU in the heavy areas; no green blocks |
| 4 | Shader/pipeline still compiling on the thread | (extends `SOTC_ASYNC_TRANSLATE`) | first visit to an area without cache | "shader"/"pipeline" ~0 in the dropped frames |
| 5 | Bind preparation | `SOTC_BIND_PREP=1` | same route; GpuDraw CPU and "late" in the `SOTC FRAMES` line | only if step 0 shows GpuDraw > 15 ms after steps 1–4 |
| 6 | Recording on several threads | `SOTC_MT_RECORD=1` | — | only if it still exceeds ~25 ms after step 5; its own plan first |

Steps 1, 2 and 3 do not depend on one another and can swap order depending on what step 0 shows
(the one with the most weight in the dropped frames goes first).

**Final criterion (locked 30 FPS):** on the standard route, with the user's launcher,
`late 0 of 150` and no frame above 50 ms in the area-transition windows, in two consecutive runs;
and the end of the game through the credits without a crash.

Automated tests (harness, 60 FPS with `BB_VBLANK_HZ=120`, Maximum + Pro) only with the user out of
the game and warned beforehand, checking `tasklist | grep bb-probe`. They serve to see the
thread's headroom (at 60 FPS the 16.7 ms limit shows up without the 30 FPS pace hiding it), not to
approve the step.

## 5. Constraints that apply to all steps

- Do not touch the semantics of the BPE heap guard (`BpeHeapGuard`, `ApplyDownloads`), the
  `POOL_FIX` or the pool rule: the end of the game depends on them.
- No green blocks or garbage textures: any copy or read done earlier needs the same page
  protection as today.
- Maximum quality stays: PS4 Pro, Maximum textures, DLSS. No gaining FPS by lowering quality.
- Every change behind an environment switch, and with the switch off the behavior is identical to
  `7fde988`.
- After each step, update T48 in `ROADMAP.md` and show it to the user.
