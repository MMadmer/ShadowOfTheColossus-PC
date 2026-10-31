# PC memory model (option C) — staged plan

User request on 27/10/2026, after measuring the drops when arriving in new areas (DMA 83–127 ms,
submission 70–106 ms, GPU read waits 47–59 ms per 5 s window; VRAM 6.4–7.1 GB of 8 GB; draw
thread 27–36 ms per frame at 30 FPS, Pro mode + Maximum + DLSS).

## The idea

Today the game's memory and the GPU's memory are separate: what the game writes is copied to VRAM
(DMA sync, submissions), and what the GPU writes and the game reads comes back by copy (GPU reads,
with page protection and waiting). In the "PC memory model" the GPU uses the game's memory
**where it is** (RAM imported with `VK_EXT_external_memory_host`), and only what is read all the
time is mirrored in VRAM.

Reference: the Bloodborne port (deadinside28, 0.5–0.6, `docs/NVIDIA_MEMORY_MODEL.ru.md` in
`ref/deadinside28_bloodborne_pc`). There the model became the default on all cards in 0.6
(19/10), but they rewrote the entire GPU core. Ours uses the shadPS4 core: the idea is ported,
the code is not. Their lessons that apply here:

- the VRAM copy **diverging** from the game's memory caused exploding vertices and textures with
  garbage; every CPU write that does not go through the port must be seen (write traps);
- the GPU should write to VRAM only in blocks that the CPU has not touched since allocation;
- importing host memory pins pages in RAM: import in chunks, on demand, not 9 GB.

The prototype `tools/host_import_test.c` (08/10) showed on the RTX 2060 SUPER: importing 256 MiB
to 5 GiB works, coherent in both directions, the GPU reads at 5.5 GB/s.

## Stages (each one measured before the next; everything behind `SOTC_PC_MEMORY`, off by default)

| # | Stage | Deliverable | Measurement | Risk |
|---|---|---|---|---|
| 1 | Import the game's memory into the GPU | extension enabled; 64 MiB chunks of the `backing` view imported on demand as `VkBuffer`; coherence test at startup | log: imported, time, memory type; nothing changes in the game | low |
| 2 | GPU reads straight into the game's memory | `BeginAsyncReadback` copies from VRAM to the imported chunk, without a staging area or `memcpy` | ms/s of reads, FPS in the demo and in the user's save | low |
| 3 | "Honest" end-of-work labels | the label (EOP) the game waits for is written when the GPU finishes, only for the measured cases | FPS, no hangs | medium (already tried on 12/10 as `SOTC_EOP_DEFER`: FPS dropped by half; needs to be selective) |
| 4 | Regions read every frame written in place | visibility culling (XPR bitmask, ~0x1078c10000) and other "hot reads" are written by the GPU directly into the imported RAM: no protection, no readback | read waits → ~0; FPS on the bridge and at the end of the demo | medium/high |
| 5 | Data written by the CPU read in place | constants and dynamic vertices read from the imported RAM instead of synced by DMA; VRAM mirror only of what is read all the time | DMA cost (today ~47 ms/s), drops when changing areas | high |

**Constraint found in stage 1 (27/10):** the readbacks apply the data through `ApplyDownloads`,
which contains the guard for the game's heaps (`BpeHeapGuard`, #146, the 0x49500 crash at the
ending): stale bytes from the GPU must not land in blocks the game has already freed. A GPU copy
straight into the game's memory skips this guard. Therefore stage 2 writes directly **only outside
the guarded heaps**; inside them the current path remains. The same applies to stages 4 and 5.

Cut-off criteria: identical image in the demo captures, no green block (temple of the 6th
colossus), game ending without a crash (POOL_FIX, heap guards and the pool rule untouched).

## Status

- 27/10: plan written.
- 27/10: **stage 1 done** (`SOTC_PC_MEMORY=1`, off by default). `VK_EXT_external_memory_host` is
  requested only with the switch; `renderer_vulkan/sotc_guest_memory.*` imports the `backing` view
  (`runtime_memory_backing_view`) in 64 MiB chunks on demand. The view exists only after the game
  maps memory, so it is looked up on every submission (`Rasterizer::OnSubmit`) until found. On the
  RTX 2060 SUPER: `10176 MiB of game memory, 159 chunks ... chunk 0 in 24.2 ms` (memory type 3),
  demo at 60 FPS, no errors. The device extension list grew from 32 to 48 slots (33 requested).
- 27/10: **stage 2 dropped** (user request: the safest option, protecting the ending). The heap
  guard decides at apply time which blocks the game has just freed (world heaps grow in 1 MiB
  chunks, the area is rebuilt every 250 ms–2 s); a GPU copy straight into memory falls outside
  this control. The gain is also small (~3–5% of the draw thread; the wait remains).
- 27/10, measurement (`SOTC_RB_STATS=1`, user's save, idle gameplay, 60 FPS, Pro + DLSS):
  readbacks per game instruction in ~3 min: `eboot+0x1a6d60` (visibility culling) ~50 thousand in
  `0x109f83…–0x109f8a…`; `eboot+0x101a98` ~22 thousand scattered in `0x13…`;
  `eboot+0x937fd` ~2.6 thousand in `0x1245620000`. The culling buffer **is not fixed** (on 14/10 it
  was at `0x1078c1…`): it is allocated in the **64 KiB page pool** (`0x10…`–`0x123C…`, the one of
  `POOL_FIX`), not in a heap (the guarded heap starts at `0x123c000020`, 150 MB). The pool frees
  and reuses pages: direct writing is safe only with the honest end-of-work signal.
- **New order:** (a) honest EOP only for the culling work (risk: slowdown, no data changes
  place); (b) then, direct writing only to the pages the culling uses, checking every frame that
  they still belong to it. Everything off by default; the game ending tested by the user before
  it becomes the default.
