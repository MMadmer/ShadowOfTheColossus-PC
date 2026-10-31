# Path B: bbport GPU optimizations in shadPS4 SotC

Original request: bring the bbport GPU optimizations, such as the two-thread draw pipeline and
FSR, into our shadPS4.

## What the tests showed (07–08/10/2026)

The native port first ran with the bbport video core, which brings ~40 optimizations that can be
switched off via `BB_TOGGLE_FILE`, listed in bbport's `gpu/shim/bbport_toggles.h`.

| bbport optimization | Effect on Shadow of the Colossus |
|---|---|
| `PendingFenceWaits` (bit 39) | intermittent black screen at boot (50–75% of runs) |
| `DrawPipeline`, `DrawPreparation` and related (early and deferred preparation) | stale or garbage GPU state during streaming while loading the save: invalid shader programs, `Unpack10_11_11`, device lost |
| relaxed GPU reads (bbport default) | the game hangs; SotC needs precise readbacks (`BB_READBACKS=2`) |

With the fork's video core in place of bbport's (keeping bbport's loader and runtime), the game
became correct: menus, save and open world, 10 min without a crash. These optimizations assume
that the game does not read on the CPU what the GPU has just written. That holds for Bloodborne,
but not for SotC.

## Current measurement (native port, i9-13900K + RTX 2060 SUPER, 1080p)

- Default: locked at 30 FPS, as on the PS4; 22–25 during loads and in the temple.
- `fps60` mode (120 Hz vblank): 38 FPS in the open field, ~25 in the temple.
- Bottleneck: the `GpuCommandProcessor` thread is 72% busy. The time goes to the Vulkan driver
  and to fence waits for the precise reads. The game's job workers stay at 36–43%.
- The fork in shadPS4 ran at 17–18 FPS (consolidated document), so the native port already has
  ~1.7x the performance.

## Conclusion

Copying bbport's two-thread pipeline into the fork as it is would break SotC for the reasons in
the table. FSR brings no gain because the game already renders at native 1080p.

The native port and the fork now use **the same video core** (the files in
`gpu/shadps4/video_core` are the fork's). Therefore any optimization made here applies to both.
The viable path is to optimize this shared core, one piece at a time, with A/B testing:
`tools/sotc_hangtest.py` and the save-loading route in `tools/sotc_play.py`.
Candidates, in order of risk:

1. Pure bbport caches, without deferral: `RegionCache`, `FetchShaderCache`,
   `ImageDescCache`, `FindImageCache`, `TextureBindingMemo`, `RenderStateMemo`.
   They target the cost of the command thread.
2. `ThreadedRecording` (Vulkan recording on another thread), keeping strict ordering with respect
   to the precise reads.
3. Cheaper precise readbacks: copy only the pages that are read and wait for the right fence
   instead of waiting for the GPU to go idle.

Each item is a manual merge between cores that have diverged by ~17 thousand lines. The expected
gain is in 60 FPS mode: the default mode already reaches the PS4's 30.

## Game memory in place: prototype (08/10/2026)

The original bbport (deadinside28, version 0.3) has a "PC memory model" (`BB_GUEST_IN_PLACE`):
the GPU uses the game's memory where it is, and the EOP labels are written when the GPU has
actually finished. This removes the copies to VRAM, the page protection and the readbacks.
Measured on Bloodborne: 95 → 115–124 FPS in heavy scenes. There it uses dma-buf, which exists only
on Linux and works only on AMD.

On Windows the way is `VK_EXT_external_memory_host`. The test `tools/host_import_test.c` maps a
pagefile section the same way `src/runtime_memory.c` does (a "backing" view and a game view over a
placeholder) and imports the "backing" view. Result on the RTX 2060 SUPER:

| Size | Imports | GPU writes → CPU sees | CPU writes → GPU reads | GPU reads the imported memory |
|---|---|---|---|---|
| 256 MiB | yes (50 ms) | yes | yes | 5.4 GB/s |
| 2 GiB | yes (460 ms) | yes | yes | 5.7 GB/s |
| 5 GiB | yes (1.5 s) | yes | yes | 5.5 GB/s |

The memory type is host-coherent, uncached on the CPU, and the minimum alignment is 4 KiB. The
VRAM→VRAM copy ran at 65–184 GB/s. Therefore textures and geometry stay in VRAM; the gain is in
the data that the GPU writes and the CPU reads (which today requires precise readbacks) and in the
labels.

**Proposed integration, in measurable stages:**

1. Import the whole "backing" view at startup as one `VkDeviceMemory`.
2. Make the readbacks copy from VRAM straight into the game's memory, without staging and without
   memcpy. The risk is low and the gain small, but it validates the piece.
3. Move to the imported memory the regions that become "hot readbacks" (those the game rereads
   every frame). The GPU then writes to them directly, without page protection or readback.
4. "Honest" EOP labels (written after completion on the GPU) for these regions. With that, the
   game's own synchronization is enough.
