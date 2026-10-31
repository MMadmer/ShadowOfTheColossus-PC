# Shadow of the Colossus PC — full project review

Status as of 12/10/2026 (branch `sotc`, commit `e5d6233` and later). Public repository:
<https://github.com/AjncSystem/ShadowOfTheColossus-PC> (versions 0.0.8 and 0.0.9 released).

This document gathers in one place what the project is, how it works, everything that has been
done so far, the performance figures, the known issues and the next steps.

---

## 1. What it is

A **native Windows port** of the PS4 *Shadow of the Colossus* (CUSA08809, EU, executable version
1.01). It is not a full emulator like shadPS4: the game code runs **directly on the PC
processor**, and only what is exclusive to the PS4 is recreated:

| PS4 component | How the port handles it |
|---|---|
| CPU (AMD Jaguar, x86-64) | the game code runs **natively** on the PC's x86-64 (same architecture). Only the few instructions Intel lacks (SSE4a) and the use of the *red zone* are fixed at load time. |
| Operating system (Orbis OS) | a dedicated runtime (`src/runtime_*.c`) implements the system functions the game calls: memory, files, threads, saves, audio, controller, dialogs… |
| GPU (AMD GCN "Liverpool") | **emulated**: the game's PM4 commands and GCN shaders are translated to **Vulkan** (shadPS4 video core, with the fixes from our fork). |
| Game libraries (libc, Fios) | the ones from the dump itself are linked natively during preparation. |
| Sound (libSceNgs2 / libSceUlt) | the modules **from the user's own console** are linked (never distributed). |

Base: the *bbport* project (native Bloodborne port, GPL-2.0), adapted to SotC.

## 2. How it works (execution flow)

1. **`SotC.exe`** (launcher, Python/Tkinter frozen with PyInstaller): first-time setup, game
   library, image/controls options, and then it calls `sotc.py`.
2. **`sotc.py`**: prepares the game once (and again only when something changes):
   - `scripts/prepare.py` turns `eboot.bin` (SELF) into a flat image;
   - `link_modules.py` / `link_libc.py` link libc, Fios and the user's sound modules;
   - `content_profile.py`, `sotc_cpu.py` (CPU fixes), `sotc_modes.py` (60 FPS, 16x AF, engine
     cvars);
   - if there is an update (`CUSA08809-UPDATE`/`-patch`) with an `eboot.bin`, its image is used.
3. **`bb-probe.exe`** (the "game"): C loader (`src/probe.c`) + runtime + video core (`gpu/`).
   It loads the image **below 2 GiB** (the game stores 31-bit pointers), creates the game's
   threads/fibers and starts.

### Directories

| Folder | Contents |
|---|---|
| `src/` | loader and runtime (memory, files, saves, audio, pad, services, fibers, SotC) |
| `gpu/shadps4/` | video core (video_core, shader_recompiler, gnmdriver, videoout) |
| `gpu/shim/` | bridge between the runtime and the video core, SDL window, ImGui overlay |
| `gpu/sotc/` | CPU fixes (SSE4a, red zone) ported from the fork |
| `scripts/` | game preparation |
| `launcher/` | `SotC.exe` (sotc_launcher_win.py, sotc_discovery.py, icons) |
| `tools/` | automated tests, benchmark, A/B, profiler |
| `packaging/windows/` | `package_sotc.sh` → `dist/SotC-PC.zip` (refuses `.sprx/.prx`) |
| `docs/sotc/` | this document, the package README, the "Path B" plan |

## 3. Timeline

### 07/10 — the game starts
- Imports matched by NID; SotC functions in a dedicated file (`runtime_sotc.c`).
- Extra memory window `0x700000000–0x800000000` (the game maps at a fixed address).
- The game reaches its own initialization.

### 08/10 — playable
- **Fibers** (`runtime_fiber.c`), **CPU fixes** (SSE4a, red zone) and **image below 2 GiB** →
  first frames (hawk in the canyon).
- GPU fixes from the fork applied to the bbport core → opening without black polygons.
- **Turning point:** the video core of **our shadPS4 fork** in place of the bbport one → logos,
  menus with text, the save loads, open field at 30 FPS, 10 min without a crash.
- Silent crash resolved: the page tracker was protecting the fiber stacks.
- **Real audio** with the user's modules; `sotc.py`/`SotC.bat` launcher; Windows package.
- **Modes**: 16x AF and 60 FPS (patch at load time, `BB_VBLANK_HZ=120`).
- `SotC.exe` (graphical launcher), bbport 0.4 controls (keyboard + controller together,
  remapping), mouse camera, output resolution 720p–4K, PS4 Pro mode, engine effects.

### 08–09/10 — performance
- Fixed benchmark: the **title-screen demo** (always the same route).
- Gains: shader info cache, HLE of the compute pass that fills tables, per-thread memory pointer
  cache, Pink's culling cvars, BPE heap guard, lock-free buffer sync, indirect draws with
  CPU-side arguments turned into direct draws, Vulkan recording on its own thread, image
  description/lookup caches, fewer write faults, time-based periodic submission
  (`SOTC_FLUSH_US=500`).
- Result in the demo (60 FPS, i9-13900K + RTX 2060 SUPER): **average 37–45 → ~67 FPS**, heaviest
  section (canyon bridge) **~17 → 29–34 FPS**.

### 12/10 — launcher, release, saves, textures
- **0.0.8 public**: game library, automatic search, key rebinding, new icon, GPU work
  submission option (AMD/Intel).
- **0.0.9 public**: initial setup screen (asks only for the main folder, like shadPS4), updates
  as in shadPS4 (`-UPDATE`/`-patch` on top of the base game), **DLC** in a folder or as an
  **uninstalled `.pkg`** (the 6 DLCs listed to the game).
- Project gathered into a **single folder** (`SotC-PC`).
- **Saving fixed**: the game never saved ("no space").
- **Texture streaming** (`GET_LOD_STATS`): sharp textures up close, waterfalls with their water
  curtains, rippling water surface.
- **On-screen save list** for loading and saving, with **save deletion**.

## 4. Current features

- **Launcher** (`SotC.exe`): setup on first launch; finds the game, update, DLCs, shadPS4 saves
  and sound modules; library; image options (output resolution, FSR 1, sharpness, 16x AF,
  60 FPS, PS4 Pro, effects), controls (keyboard/controller/mouse, remapping), GPU work
  submission; desktop shortcut with the game icon.
- **Game**: runs from start to finish in normal mode; real audio; saves (save, load, delete);
  DLCs; update 1.01; PS4 Pro mode (Cinematic/Performance in the game menu).

## 5. Main technical fixes

| Problem | Cause | Fix |
|---|---|---|
| Silent crash without a message | fiber stack page protected by the GPU tracker | fiber stacks registered and excluded from tracking |
| Broken game pointers | the game stores 31-bit pointers | image loaded below 2 GiB |
| Black polygons in the opening | fork's GPU fixes missing | the fork's video core |
| Intermittent black screen | bbport optimizations incompatible with SotC | the fork's core; precise GPU readbacks |
| The game never saved | the port reported that each save took 1 GiB | real save sizes |
| Loading went into a loop / each save created a new slot | the list dialog "finished" without a choice | list drawn on screen (ImGui overlay) |
| Blurry textures, black water, waterfalls without curtains | `GET_LOD_STATS` command ignored → the game never loaded the fine mips | statistics emulated from the textures in use; textures re-uploaded when the game maps new memory |
| GPU queue hung with threaded recording | the presenter waited for a tick before it was submitted | wait for the submission before presenting |

## 6. Known issues (by priority)

1. **Memory corruption with texture streaming.** Requesting maximum resolution (mip 0/1) for
   everything makes the game corrupt its own memory within 1–4 minutes; with the current limit
   (mip 2, 512×512) the whole demo runs, but **loading another save without restarting the game
   may close it**. Recommendation: restart the game before switching saves. Tested and
   **ruled out** as the cause (12/10): EOP labels written early (`SOTC_EOP_DEFER=1` writes when
   the GPU finishes: FPS drops by half and the failure persists), the HLE copy marking the whole
   range as GPU-written (fixed to mark only the pieces; the failure persists) and runtime memory
   refusals (there are none). The symptom is always memory reused by the game still being
   read/written with stale data (heap nodes, descriptors and shaders with garbage) at around
   250–270 s into the demo with mip 0/1.
2. **Black shadows**: shadowed areas without ambient light (not worsened by the recent changes).
   Lead (RenderDoc capture of the opening temple, ~178 s): a half-resolution input of the
   lighting passes (960×540, A2B10G10R10, read by ~16 light draws) **is never written in the
   frame and stays at zero** — probably missing ambient occlusion/indirect light. It is still
   unknown which pass should write it (no write as an image or as a buffer was found).
3. **Water**: opaque/flat lake in some places; wave motion not confirmed.
4. **Crash in the title-screen demo** after several minutes (same family as item 1).
5. Old saves copied from shadPS4 appear with the label "Profile" (the label is corrected when
   saving over them).
6. AMD/Intel: the submission optimizations are off by default and barely tested.

## 7. Diagnostics (environment variables)

| Variable | Purpose |
|---|---|
| `SOTC_FPS=1` | FPS and GPU readbacks in the log every 5 s |
| `SOTC_SAVE_TRACE=1` | logs save calls |
| `SOTC_LOD_STATS=0` / `SOTC_LOD_LEVEL=n` / `SOTC_LOD_TRACE=1` | texture streaming: disable, requested level, logging |
| `SOTC_MAP_INVALIDATE=0` | does not re-upload textures when the game maps memory |
| `BB_WRITE_LOG=1` | log of the port's writes to game memory (dumped on a fault, with totals per source) |
| `SOTC_RDOC_TRIGGER` + `SOTC_PLAY_WRAP` | RenderDoc capture of a GPU submission (`renderdoccmd capture`) |
| `SOTC_SHOT_OVERLAY=1` | screenshots with the overlay (save list) |
| `BB_FRAME_DUMP_TRIGGER` | frame screenshot when the file appears |
| `BB_PAD_FILE` / `BB_PAD_RECORD` / `BB_PAD_REPLAY` | controller from a file, record/replay a route |
| `SOTC_PROFILE=<thread>` | sampling profiler |
| `SOTC_FLUSH_US`, `SOTC_RB_AHEAD`, `SOTC_RECORD_THREAD`, `SOTC_INDIRECT_DIRECT`, `SOTC_FILL_HLE` | optimizations (turn on/off for tests) |

Tools: `tools/sotc_play.py` (script: `t:cross`, `t:shot`, `t:rdoc`…),
`tools/sotc_bench_attract.sh` (benchmark), `tools/sotc_intro_ab.sh` (A/B at the opening river),
`tools/profile_sym.py`.

## 8. Build, package, test

```bash
# build (MSYS2 CLANG64 at I:\msys64)
TMP=$(cygpath -w /tmp) PATH=/i/msys64/clang64/bin:/i/msys64/usr/bin:$PATH ninja -C out/gpu bb-probe bb-gpu-capabilities
cp out/gpu/bb-probe.exe out/gpu/bb-gpu-capabilities.exe out/
# package (dist/SotC-PC.zip and dist/SotC-PC/ with saves preserved)
bash packaging/windows/package_sotc.sh
# benchmark (60 FPS, with the real game's culling cvars)
BENCH_CVARS="doIndexBufferCulling=0 doSunShadowIndexBufferCulling=0" bash tools/sotc_bench_attract.sh
```

Project rules: commits as `AjncSystem <AjncSystem@users.noreply.github.com>`; never distribute
Sony firmware (`.sprx`); large dumps on drive I:.

## 9. Next steps

1. **Streaming stability** (item 1 of the issues): EOP labels written when the GPU finishes
   ("Path B", a stage already prototyped with `VK_EXT_external_memory_host`), then unlock the
   maximum texture resolution.
2. **Shadows and ambient light**: capture a scene with RenderDoc and follow the lighting pass.
3. **Water**: surface shader (draw found in the capture) and wave animation.
4. Progress indicator with % on the initial black screen.
5. Tests on AMD and Intel.

### About DirectX 12

The port **is already native Windows x86-64**: the game code runs directly on the processor. The
only "emulated" part is the GPU, which today is translated to **Vulkan**. Switching to DirectX 12
would mean rewriting the entire video core (thousands of files translating commands and GCN
shaders → SPIR-V) for D3D12/DXIL, with no real gain: Vulkan works on every Windows graphics card
(NVIDIA, AMD, Intel) and performance depends on the translation, not on the API. A "complete"
reverse engineering (recreating the game's renderer in DirectX, without GPU translation) would be
a different project, many months long. The path that delivers results is to keep fixing the
current translation.
