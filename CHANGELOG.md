# Changelog

[English](#english) · [Русский](#русский)

## English

### 1.2.0 — 2026-10-31 (fork Madmer)
Brings into the fork version 0.70 of the original project and versions 0.60.6-C1 to C3 (below), parts
of its open pull requests and fixes for issues that are still open. Thanks to **tabnul,
Lucas-blip-png, HiddenUserHere, Lukto02, Oussama268, InkAurora, NathanMBR, Gabriel-TwoBits and
AjncSystem**. The shader cache is rebuilt on the first start.

**New**
- **US version (CUSA08034)**, from the original project: the executable is the same as the European
  1.01; all modes apply, with saves, shader cache and DLCs kept separate per region.
- **NVIDIA DLSS** now uses the original project's implementation (tabnul): it also works in PS4 Pro
  mode, at any aspect ratio and in 4K, with the interface drawn at the output resolution. The game
  renders at the output resolution divided by the mode's factor (in PS4 Pro mode, at the Pro's own
  sizes); DLAA is the factor-1 mode. Without an RTX card, FSR 1 upscales the same image. With HDR
  output, FSR 1 is used instead of DLSS.
- **DLSS-G multiplier up to x6 and Dynamic mode** (GeForce RTX 50, depending on the driver), in the
  launcher and in the in-game menu (PR #314).
- **Grass:** shadows, distance, height, detail and fade of grass and brush in the launcher
  (Graphics > Grass) and in the in-game menu, from the original project (tabnul).
- **Sun shadows** (Graphics > Effects and in-game menu): disabling them increases FPS when
  performance is limited by the CPU (#294).
- **Frame pacing** with unlimited FPS (PR #351, tabnul): each frame is presented at the game's own
  pace, without alternating between 8 and 17 ms; in-game menu (F1) or `SOTC_FLIP_PACER=0`.
- **Debug page** in the launcher: Bluepoint debug menu (F6 or Start+Select), fast GPU readbacks
  (experimental), texture memory watchdog and DLSS/FSR 1 comparison (F9).
- **Maximum textures in PS4 Pro mode** (experimental), with the fix for the game's page allocator
  (tabnul, `0x1a371`).
- Automatic update notes are shown in the launcher's language when the release provides them.

**Fixes**
- **Memory usage:** the frames of nested command tasks were never freed (about 44 MB per minute,
  2.6 GB per hour), part of the reported high RAM consumption (Lucas-blip-png). The log shows the
  process memory every minute (`SOTC RAM`).
- **Game memory corrupted when splitting a flexible mapping:** the right part pointed to the pages
  of the left part, and freeing it zeroed pages in use (Lucas-blip-png). Likely cause of crashes
  due to invalid pointer reads (`0x49476`, `0x48ac5` and others).
- **GPU readbacks during fights** (PR #349, tabnul): an asynchronous readback dirtied by the GPU is
  partially completed and redone without stalling the command processor (stutter in the colossus
  fights). Two bugs that could write stale data into game memory were fixed.
- **Ultrawide (21:9, 32:9) and 16:10 displays:** the 3D scene has the correct aspect ratio again
  (0.70 stretched it, #327, #331, #341).
- **Black opening logos, save notice and menu text** with the texture upload budget of 0.70 (#341,
  #353): a texture waits at most 2 frames, and the budget restarts after 100 ms without a frame.
- **Shader cache:** a file newer than the `cache.sotcpack` package now takes precedence over the
  package's copy (pipelines rejected on every launch); small textures modified by the CPU are
  checked in full (colored blotches) (PR #339, HiddenUserHere).
- **Intel GPUs:** the driver crashed when writing the pipeline cache while pipelines were being
  created (#199, #211, #217); at most 4 simultaneous compilations; full water reflections are off
  by default, as on AMD (stripes in shadows and water).
- **AMD GPUs:** running out of video memory with budget still free (RX 9070 XT, #293) and invalid
  descriptor formats no longer close the game; an invalid MSAA texture no longer aborts the shader
  translator (#255). `SOTC_GCN_NV=1` (test for RX 400/500) uses the NVIDIA shader options.
- **FSR 3 frame generation now runs on AMD's official runtime** (`amd_fidelityfx_vk.dll` of the
  FidelityFX SDK 1.1.4, FSR 3.1, included in the package), with its own frame generation swap chain
  and frame pacing, as in other games; the FSR-Vulkan module is no longer used. **A dark rectangle
  trailing the camera and a hazy picture while it turns** (it also looked like a flickering window):
  near the screen edges, where one of the two game frames has the point off screen, FSR 3 fills a
  band about half a frame's motion wide with a blurred estimate. The port rebuilds that band from
  the frame that has the point, moved half way along the game's motion vectors, and blends it
  smoothly into FSR 3's picture. F8 now turns frame generation on and off in the mode chosen at
  start: FSR 3 and DLSS-G use different swap chains, so changing between them requires a restart.
  With FSR 3 the NVIDIA Streamline runtime is not loaded. Fast objects close to the camera (Agro
  running past it) may still show thin outlines in generated frames, as with FSR 3 in other games
  at 30 FPS.
- **The game could stop while loading a save** (behind the loading screen, about one load in six
  in tests): the draw thread looped in a memory fault while comparing a table the game fills every
  frame. The table's source is now read through the backing memory view, as the table itself.
- **4-thread processors (i5-3470 and similar): the game never left the first loading screen
  without frame generation** (upstream #267). The thread that presents the frames ran at normal
  priority below the raised GPU, loading and sound threads; it is raised too on 4 threads or fewer.
- **Skip intro:** a mouse click during the loading (to give the window the focus) no longer
  cancels skipping the opening movie.
- **Taskbar over the game in borderless fullscreen:** the game window takes the foreground once
  the shader loading screen closes.
- **HDR:** with FSR 1 the image was too dark; reference white at 200 nits (`SOTC_HDR_WHITE`).
- **Low volume** on stereo outputs (#214): the 8-channel audio is downmixed to stereo by the port,
  with the front channels at full volume. If you raised the volume above 100% to compensate, lower
  it.
- **Automatic update:** HTTPS certificates are verified against the Windows certificate store
  (`CERTIFICATE_VERIFY_FAILED`, #359).
- **`sotc.ini`:** the release ships `sotc.ini.example`, copied to `sotc.ini` only when it does not
  exist; extracting a new release no longer erases the settings (#136).
- **Damaged game files:** the check includes the files in the update folder and the US version
  (#112). DLCs of type 0x1c (additional licenses) are recognized (#137).
- **Silent crashes at launch:** the log shows the actual exit code (previously `0xffffffff`) and
  the Windows error event; the hang watchdog also records the thread stacks before the title
  screen. `SotC.bat` also pauses on these codes.
- **Frame generation:** F8 switches the mode at most once every 1.5 s (NVIDIA driver crash); the
  video memory of FSR 1 and FSR 3 is freed while they are not in use (PR #308, Lukto02).
- The FPS hint in the launcher mentions the known cases: above 30 FPS, the fight against Avion;
  above 60 FPS, the opening video may close the game (#100, #331).

**New diagnostic options:** `SOTC_POOL_FIX=0` (without the page allocator fix),
`SOTC_EXTRA_CVARS` and cvars in the `NOME(ARGS)` format (PR #345), `SOTC_SWAP_IMAGES` (swapchain
depth, PR #352), `SOTC_DRIVER_CACHE_LOCK`, `SOTC_SLOW_BUILD_MS`, `SOTC_WIDE_ASPECT=0`,
`SOTC_THREAD_TIMES`.

### 1.1.3 — 2026-10-23 (fork Madmer)
Brings into the fork versions 0.60.5 and 0.60.6 of the original project (below) and its open pull
requests that were still missing, with fixes for issues that are still open. Thanks to
**Oussama268, JihedMrouki, Lucas-blip-png, Anthonypc23 and AjncSystem**. The shader cache is
rebuilt on the first start.

**New**
- **Field of view** (Graphics > General): 100% (original), 110%, 120% or 130%. Uses the
  "FOV Adjustment" factor of the game's own camera, so culling and shadows follow the wider view;
  it also applies in cutscenes (#179).
- **DLAA** in NVIDIA DLSS: DLSS anti-aliasing at native resolution (#34).
- **HDR (experimental)** (Graphics > Display): on an HDR10 monitor the game offers its own HDR mode
  (#37).
- **Quit game** in the in-game menu (F1) (#210).
- The launcher warns when the game files are incomplete or damaged (size of the `.psarc` files of
  CUSA08809), instead of the game closing at launch (#30, #56).

**Fixes**
- **Game permanently stuck on the loading screen** (seen once on a 4-thread CPU, when leaving the
  title screen): a worker thread stopped at the first read of a task and the game waited for it.
  Likely cause, now fixed: a page marked as GPU-written with no GPU bytes behind it (a range without
  bound memory leaves the list, while the page mark remained) stayed protected, and the game's read
  or write hit the same page fault forever. The page is now released after the read. The hang
  watchdog records the thread stacks again at 45 s, and the log shows when such a page is released.
- **Memory binding refused three times by the driver** (2 GB cards): bindings without new memory
  (an arena change, releases) were still counted as done, and shaders read zeros there; they are
  now kept and resubmitted with the next submission, before the others.
  `SOTC_TEST_BIND_REFUSE_RETRY=1` simulates this case for testing.
- **Maximum textures:** the 1.1.1 protection against video memory over budget (textures one or two
  levels coarser above 150% / 190%) applied only to High; it now also applies to Maximum, which
  disables only the adjustment based on the game's page pool.
- **Skip intro:** the game's 3D sound (Audio3d) is also muted under the loading screen, and the
  launcher volume (`SOTC_VOLUME`) now applies to it.
- **Automatic update:** a second SotC.exe opened during the update deleted the backup of the
  replaced files; it now leaves the `update` folder untouched, and an interrupted update is rolled
  back on the next start.
- **Intel HD/UHD Graphics with the old driver** (6th to 11th generation, versions 101.2xxx or
  earlier): vertex input dynamic state is disabled, because this driver drew blue and pink garbage
  (#20). Without it, pipelines are compiled on the command processor (stutter the first time an
  effect appears) and the shader cache is kept in a separate folder (`_novid`). Iris Xe, Arc and
  Core Ultra use the new driver and are unchanged. `SOTC_VERTEX_INPUT_DYNAMIC=0/1` forces it.
- **F7 and the in-game menu FPS setting** change the game's FPS target also when the game starts at
  30 FPS (#44, #49).
- **File read-ahead (HDD)** uses at most a quarter of the RAM, from the smallest files to the
  largest; files that do not fit are left out and are listed in the log (#264).
- **4-thread CPUs:** the game's worker threads run below normal priority and the file reading
  threads above it, as in the fix in the God of War III port for the same processors (#267;
  `SOTC_JOB_PRIORITY=0` disables it).
- **AMD GCN (RX 400/500):** converting shaders to 32 lanes is now optional (`SOTC_AMD_WAVE32=1`):
  these cards only run 64 lanes and the driver could crash.
- **Returning cold buffers** (PR #238, Oussama268): the command processor no longer waits for the
  readback copy; the return completes in a later frame, once the GPU has caught up.
- **PR #258** (JihedMrouki): on cards without native barycentrics (GTX 10xx) vertex position is
  invariant, so the depth pre-pass and the color pass match; the command processor skips empty
  queues without locking them.
- **Exclusive fullscreen:** with the desktop at a refresh rate that is not a multiple of 60 Hz
  (75 Hz, 144 Hz…), the monitor's highest multiple of 60 Hz at the same resolution is used, and
  30 FPS no longer alternates between 2 and 3 refreshes (PR #243, Anthonypc23); `BB_FULLSCREEN_HZ`
  forces a different rate.
- The in-game menu (F1) and the event log, including the loader messages, are translated into
  Spanish, Russian, Chinese, Japanese and Korean.

### 1.1.2 — 2026-10-22 (fork Madmer)
- **Skip intro** (*Пропуск интро*) is now a choice: *Off* (default), *Splash screens only* (the
  engine's `skipSplash=1`: the startup logos) or *Splash screens and opening video*. In 1.1.1 the
  option used `+nointro`, which also skipped the title screen and the profile: a new game started
  immediately and the menu only offered *New game* (saves were not touched). The engine has no
  option for the opening video alone: with an existing profile, the port's loading screen covers
  the game and the sound stays muted until the title screen, while the video is skipped beneath it.
  The "Press any button" screen and the menu with *Continue* remain.

### 1.1.1 — 2026-10-22 (fork Madmer)
- **Endless loading on 2 GB cards** (GTX 1050), at startup with high-quality textures disabled and
  mid-game in the open world:
  - the texture cache collector freed nothing on small cards: GPU-written images, which it cannot
    free, stayed at the front of the queue and used up every pass ("GC freed 0"); video memory
    reached 3.4 GB on a 2 GB card. They now go to the end of the queue;
  - a memory binding refused by the driver remained counted as done, and shaders read zeros there
    forever: the game waited for data that never arrived. It is now undone and redone on the next
    use, and cold buffer blocks are returned to free up space;
  - with high-quality textures, video memory over budget (150%/190%) also makes textures request
    coarser mips, not only the game's page pool.
- The log (`user/last_run.log`) records video memory, images, the collector and memory failures
  every minute.
- **Launcher:** *Skip opening splash screens* option (*Пропуск интро*) (the engine's own
  `+nointro`, off by default) and a scrollbar on the main page (the quick settings were cut off on
  small screens).

### 1.1.0 — 2026-10-22 (fork Madmer)
Brings into the fork everything from 0.58 to 0.60.4 of the original project and its open pull
requests, with fixes for issues that are still open, NVIDIA DLSS and optimizations. Thanks to
**Lucas-blip-png, Oussama268, DUDUYG, jownatan and AjncSystem**, whose commits are included under
each author's name.

**Low-end GPUs, memory and other fixes**
- **Missing sounds on 4-thread CPUs** (i5-7300HQ and similar): the game's sound system thread,
  which starts the effects (Agro's hooves, colossi), was scheduled behind the game's worker threads
  and the GPU threads, and effects and sometimes music were lost. The sound threads now have
  priority over them.
- **720p render resolution** covered the scene with graphical garbage on all cards, also in DLSS
  Quality: render widths from 1153 to 1280 give the render targets a row pitch that the port does
  not emulate correctly. The mode now renders at 1296 × 729, with the same image.
- **Game closing after a few minutes with low virtual memory** (`0x1a371`; laptop with
  GTX 1050): with a small page file, Windows refused the game's 9152 MiB of memory and the port fell
  straight back to the console's 5056 MiB, which the port exhausts. The game now receives the
  largest amount of memory that Windows still grants, leaving 2.5 GB for the rest of the process.
  The launcher warns when free virtual memory is insufficient (recommended: a 16 GB page file).
  `BB_DMEM_COMMIT_MB=<MiB>` simulates a lower limit for testing.
- **Game freezing permanently on 2 GB cards** (GTX 1050, 2nd colossus): out of video and system
  memory, the driver refused the memory binding of the buffers, and the semaphore that the next
  submission waited on was never signaled. The binding is now retried after the previous ones and,
  if it fails, the semaphore is signaled by the CPU: the game continues (ranges without memory read
  as zeros).
- A texture evicted from full video memory writes back to game memory only the bytes that the game
  has not changed since the request.
- **Light square around button icons** (Xbox, keyboard and mouse) in prompts: the game blends the
  interface with premultiplied alpha, and the gray border outside the round icons was added to the
  image. The icons are now written with the color premultiplied by coverage.
- **Package (.pkg) extraction progress** in the launcher: it jumped to 100% in the first second and
  stayed there until the end (PkgTool creates each file at its final size). It now follows the
  bytes that PkgTool has actually written.
- **Hangs:** when no game frame arrives for 60 s, the log (`user/last_run.log`) receives the stacks
  of all threads, to help find the cause.

**Original project and pull requests**
- **0.58 to 0.60.4 of AjncSystem/ShadowOfTheColossus-PC** (draw thread, shader translation and
  compute pipelines on auxiliary threads, DLSS-G and FSR 3 frame generation, in-game menu, textures
  on Pascal, video memory, volume, automatic update and the rest of the notes below), combined with
  what is specific to the fork (button icons for the device in use, NGS2/Audio3d, window modes,
  launcher languages, installer). The launcher checks for updates in this fork's releases.
- **Shaders after a driver update:** shader cache preloading builds the pipelines on auxiliary
  threads (PR #215, Oussama268: 107 s → 6.5 s of loading screen on an RTX 3060).
- **AMD GCN (RX 470–590, Vega):** shared memory barriers (PR #205) and conversion to 32 lanes by
  default, which ended the white/green flickering on an RX 570 (#152, #187, #201;
  `SOTC_AMD_WAVE32=0` disables it), by Lucas-blip-png.
- **CPUs with 4 to 8 threads:** the command recording thread no longer busy-waits (PR #198,
  Lucas-blip-png).
- **CPUs without BMI1/LZCNT/MOVBE** (Ivy Bridge and earlier, #38, #40, #70, #88): the instructions
  are emulated instead of the game failing to start (Oussama268, Lucas-blip-png). This fork's
  `bb-probe.exe` is compiled for x86-64-v3 (AVX2), which these CPUs lack: the package also includes
  `bb-probe-compat.exe` (x86-64-v2), which `sotc.py` launches automatically on them
  (`SOTC_PROBE_COMPAT=1` forces it).
- GPU readbacks that fall into a window already being read wait for it (PR #69, DUDUYG;
  `SOTC_RB_JOIN=0` disables it); high-resolution timer in frame pacing; exact key in the image
  format cache; separate shader cache without the wave64 uniform branches (PR #93);
  `tools/SotC-Shaders.ps1` updated (PR #81); clear message for files that are still encrypted
  (PR #212); the launcher survives a refused standard output (PR #207).

**Stutter**
- **Shaders retranslated on every launch:** shader binaries were stored only by the program hash;
  binaries from another translator version (an earlier release or an old shader package) occupied
  the names, and those of this version were never stored, so they were retranslated mid-game in
  every session. The cache now records the binary version, and a cache from another version is set
  aside once.
- **Shader package from another profile** was copied again on every launch (19 s) and then
  discarded, together with the cache built during play; it is now copied only once.
- **Driver cache** (VkPipelineCache) stored with the shader cache: pipelines compiled by the driver
  come back ready on the next launch, even when the NVIDIA driver cache is full.

**NVIDIA DLSS**
- **DLSS Super Resolution** (GeForce RTX cards): in render resolution modes below 1080p, the game
  scene is upscaled by DLSS instead of the game's own bilinear stretching, using the game's depth,
  motion vectors and camera; the interface is still drawn by the game at 1080p. Quality (720p),
  Balanced (630p), Performance (540p) and Ultra Performance (360p) modes (`SOTC_DLSS`). It can be
  turned on and off in the in-game menu. The official NVIDIA Streamline 2.14.16 runtime is included
  in the package.
- **Sharp interface with frame generation:** generated frames receive the scene without the
  interface, with the interface of the real frame on top (FSR 3.1); text and bars no longer smear
  with motion, and loading screens and the title logo come out clean, also with outputs above
  1080p (FSR 1). The interface is composited using the transparency of the game's own interface
  layer. DLSS-G receives the image without the interface (`SOTC_FG_HUDLESS=0` disables it). DLSS-G
  x3 and x4 frame multiplier on GeForce RTX 50.
- **Frame generation only where it works:** DLSS-G is offered on RTX 40/50; FSR 3 on other cards.
  A DLSS-G setting saved on a card without it switches to automatic (FSR 3).

**Launcher, menu and controls**
- **In-game menu** (Insert / F1 / L3+R3) and the port's on-screen text in the launcher's 7
  languages; Chinese, Japanese and Korean use a font that ships with Windows. Options that had no
  effect in this port were removed (upscaler choice, preset, output resolution and sharpness). DLSS
  and fullscreen (F11) change immediately; the game's effects (vignette, fog, SSAO, full water
  reflections) are saved in the launcher settings and take effect the next time the game starts.
- **Controller vibration** from 0 to 100% (#210).
- **Mouse buttons** can also be rebound on the key binding screen: click the action and press the
  button (#192).
- **Game update** is also found in `update` or `patch` inside the game folder or next to it, when
  the `param.sfo` belongs to a game update (#66).

**Fixes**
- **Video memory exhausted** (4–6 GB cards, #12, #62, #137): texture collection goes three levels
  deeper (without blocking the thread that holds the texture cache) and the request is split into
  smaller blocks; with no memory at all, the range stays without memory until the next attempt,
  instead of closing the game.
- **Game closing at launch if Windows refuses 9152 MB** (#194) and **frame generation on PCs with
  AMD and NVIDIA** (#194), by Lucas-blip-png; **FSR 1 without synchronization on AMD** (#209).
- **Shader cache with duplicate keys** closed the game on every launch until it was deleted (#88).
- **Interface bars full or black at 2560x1080** (#202).
- **Game clock:** the TSC frequency is measured with paired samples over 100 ms (audio and video
  out of sync in long cutscenes, #91).
- **No sound when the output device does not yet exist at launch** (Bluetooth headphones, #214):
  the port retries every second.
- **Saving in the in-game menu erased from `sotc.ini`** the key bindings, controller remappings and
  `sotc.py` settings; now only the menu's lines are rewritten, and `sotc.ini` and the launcher
  settings are written to a new file that replaces the old one (an interrupted write loses
  nothing).
- **CPU patches on Intel:** with the original project's BMI emulation, the game's function table
  was lost while reading those of the modules, and short SSE4a instructions and functions that use
  the area below the stack were left unpatched; these patches are back. An instruction patched
  during gameplay no longer blocks the exception handler.
- **GTX 10 (Pascal):** shaders read from the cache lacked the pixel shader inputs, and pipelines
  built later from them lacked the barycentrics geometry shader (wrong image); the cache now stores
  this information.
- **Ending of the game (#146):** the protection of the free lists of heaps created by the world
  again tracks every change to them.
- **Video memory exhausted:** after a total failure, the game no longer repeats texture collection
  with a GPU wait on every draw (which froze it) and retries after 250 ms.
- **DLSS:** if evaluation fails, the scene appears stretched as in the original game instead of an
  undefined image, and after 8 consecutive failures DLSS turns itself off (the menu shows the
  reason); camera cuts clear the DLSS history again.
- **Driver cache** is also saved when the game window is closed.
- **PCs with GPUs from different vendors** (Intel + NVIDIA laptop, APU + GeForce) or with
  `BB_GPU_ID`: frame generation and DLSS follow the GPU that the game will use.
- **Fullscreen from the menu** could freeze the game; F11 works with the menu open; the mouse
  camera is turned on and off immediately from the menu.

**Performance**
- **GTX 10 (Pascal):** the geometry shader that delivers the barycentrics passes the vertices
  through without copying them (VK_NV_geometry_shader_passthrough), building on the implementation
  from PR #169 (`SOTC_GS_PASSTHROUGH=0` disables it).
- The tables that the game replicates every frame are compared on the copy threads.
- The executable is compiled with profile-guided optimization (PGO), trained on the title screen
  demo.

The shader cache is rebuilt on the first start.

### 0.70 — 2026-10-29
> ⚠️ **Important: install the game on an SSD (preferably NVMe).** Each new area reads hundreds of
> MB of textures and models from the game folder; on an HDD, area transitions stutter much more and
> can freeze for seconds. Place the game folder (the dump) and, if possible, the port folder on an
> SSD.

- **Area transitions without FPS dropping to near zero.** Previously, each area transition dropped
  the FPS and sometimes forced 1–3 s loading screens; the worst frame exceeded half a second
  (534 ms). The worst frame is now 58–75 ms, with no forced loading screen, and the ending of the
  game plays through to the credits. Each transition still loses 1–6 frames: this is the next item
  of work. The causes were the lock on the game's memory table, memory copies remapping gigabytes,
  the GPU region lookup being redone on every write, the game memory protection walking the same
  range thousands of times, and many new textures in a single frame. Now at most 32 MB of large new
  textures are uploaded per frame, and those deferred to the next frame appear black for an
  instant. Each fix has a switch to disable it (`SOTC_LOCK_WAKE=0`, `SOTC_VIEW_CHUNK_MB=0`,
  `SOTC_REGION_LOG=0`, `SOTC_REGION_FAST=0`, `SOTC_PROTECT_FAST=0`, `SOTC_WRITE_FAST=0`,
  `SOTC_BATCH_SPLIT=0`, `SOTC_GUARD_DEDUP=0`, `SOTC_UPLOAD_BUDGET_MB=0`).
- **Rare crash in the game's allocator after a load** (`0x49800`): two shortcuts in the game memory
  protection could let the GPU write into a block the game had just freed. They are now disabled
  by default (`SOTC_GUARD_STALE=1` and `SOTC_GUARD_USER_POINTER=1` enable them). A 36-minute
  session with 6 loads ran without a crash; if a crash occurs, please send the log.
- **DLSS in PS4 Pro mode at 30 FPS without image flicker:** the repeated frame showed the image
  without DLSS.
- **Frame generation (F8) with FSR 1: the HUD stays steady.**
- **LOD feedback** (`SOTC_LOD_FEEDBACK=1`, experimental) no longer costs ~7 FPS.
- Diagnostics: `SOTC_DRAW_SPLIT=1` (with the on-screen FPS counter) logs where the draw thread's
  time is spent.

### 0.60.6-C3 — 2026-10-27
- **Stamina bar with DLSS (fix to C2):** the bar area now uses the width of a 1080-line screen at
  the output aspect ratio (1080 × W/H) instead of the render width. With DLSS the render resolution
  is smaller (1704x720 at 2560x1080), and the yellow bar again stopped far from the circle; at 16:9
  with DLSS outside PS4 Pro mode, C2 broke it as well.
- **30 FPS limit:** "30" (F1 menu or F7) holds 30 FPS even when the game requests no limit (PS4 Pro
  mode); previously it ran at ~42.

### 0.60.6-C2 — 2026-10-26
- **Widescreen (21:9, 32:9): correct health and stamina bars (#301, #202).** The health bar had an
  extra grey segment, and the yellow stamina bar stopped far from the circle and filled in reverse.
  The aspect ratio of the game's screen object stays at 16:9 (the game draws the health bar with
  it), and two 1920-wide interface rectangles now use the screen width.
- **Controller vibration can be disabled (#292, Lucas-blip-png):** in the launcher (Controls) and
  in the F1 menu (Controller section), taking effect immediately.
- **Higher FPS in the Shrine (Lucas-blip-png):** the barrier for GENERAL storage images is issued
  once per fence instead of before every draw (each one ended the render pass, ~20,000 per second),
  and the depth buffer read while the stencil is written stays in a single layout.
  `SOTC_GENERAL_SYNC=0` / `SOTC_DEPTH_RO=0` disable this.
- **Frame generation without the interface (option by Lucas-blip-png, experimental):** Image and
  FPS → "Frame generation without the interface". Text and bars no longer flicker with FSR 3 (this
  also works in PS4 Pro mode). May fade the title screen logo.
- **DLSS Ultra Performance (3×) (#306)**; DLSS falls back to 2× upscaling + FSR 1 only when no mode
  accepts the scale (Pro mode at 4K).
- Testing: `SOTC_RENDER_KEEP`, `SOTC_RENDER_EXTRA` and `SOTC_WIDE_ASPECT` (for finding the game's
  screen size values).

### 0.60.6-C1 — 2026-10-26
Community release: fixes and features from tabnul, Oussama268, NathanMBR and JihedMrouki, tested
with **Maximum textures + PS4 Pro mode + 60 FPS** (RTX 2060 SUPER).

- **Fix to the game's page allocator (tabnul, PR #288 / fork #112):** the game's free page counter
  drifted out of sync with the actual map, and the game read before the start of the map (crash
  `0x1a371`). With the fix and the texture memory reserve set to 0 (also in PS4 Pro mode), the
  streamer no longer drops all textures to level 3 every second. Result: **one hour at Maximum +
  Pro without a crash**, and nearly all the coloured garbage after loads is gone.
- **Maximum textures in PS4 Pro mode (experimental):** available again (the launcher used to
  replace it with High). `SOTC_MAX_IN_PRO=0` restores the old behaviour.
- **NVIDIA DLSS (RTX 20 or newer), by tabnul (fork PR #81):** the game renders at a lower
  resolution and DLSS upscales it; the interface is drawn at the output resolution, the game's TAA
  is disabled, and the vignette is applied after upscaling. New here: **works in PS4 Pro mode**
  (the Pro's final composition has the scene and the interface in a different order and size) and
  **at 4K** (DLSS upscales 2x and FSR 1 completes the rest; previously the screen went black). In
  the F1 menu the upscaler is shown as "NVIDIA DLSS" and can only be changed in the launcher
  (previously it overwrote the setting with "fsr1"). The files `sl.dlss.dll` and `nvngx_dlss.dll`
  (NVIDIA Streamline 2.14.16) are included.
- **Grass (tabnul):** grass and brush distance, height and detail change immediately in the F1
  menu; grass shadows and fade are in the launcher; grass buffers are 4x larger (distant grass no
  longer disappears in strips).
- **Higher FPS: fast GPU readbacks (experimental),** a new option in Quick settings, with the final
  check and per-page locking by Oussama268 (`SOTC_RB_HOT=1`). Disabled by default.
- **Bind reuse** is enabled by default again (Oussama268; the sand ground fix has been verified).
  Also by Oussama268: the command processor does not wait on empty queues, and cold arena memory is
  returned without waiting for the GPU.
- **US version CUSA08034 (NathanMBR, PR #270)** — details below.
- **Low-end CPUs and GTX 10xx (JihedMrouki, PR #258):** invariant position in pipelines with
  barycentric emulation (texture/depth mismatch on the ground), and the shader cache no longer
  attempts to read non-existent files from disk. The shader cache is rebuilt on the first start.
- **F7** cycles through **30 → 60 → unlimited** (previously it only toggled the 30 FPS limit).
- **Debug tab in the launcher:** Bluepoint's debug menu (**F6 or Start+Select** on the controller;
  Select alone still opens the map), **skip the opening logos**, **go straight to the game**
  (`+nointro`, also skips the Start screen), texture memory monitor, F5 (capture of the frame's
  textures, limited to 1 GB) and F9 (reloads textures; with "Compare DLSS" enabled, switches between
  DLSS and FSR 1).
- F1 menu: grass shadows and fade show the game's value when they have not been saved yet.

#### US version (CUSA08034), NathanMBR
- **The US dump CUSA08034 (1.00, the only US version) works.** The executable is the same as the
  European version 1.01 (code and data identical byte for byte; the `.psarc` files as well), so all
  fixes, patch modes (60 FPS, 16x filtering, render size) and the red zone protection apply to
  both. Only the logos and legal screens differ (the game selects the region from
  `gameversion.txt`). Tested up to the second colossus.
- The GPU fixes previously limited to CUSA08809 (LDS barriers, wave64, T# tables, early depth tests
  for decals, periodic flush) now apply to both serials (`IsSotc()`). Without the early tests,
  decals behind an object painted their entire box over it (background edges and textures in
  front). The log shows `SOTC early fragment tests: on`.
- **Separate saves per version** (`user/savedata/1/CUSA08034`) and a dedicated shader cache
  (`user/cache/CUSA08034`). **DLC from the same region only** (folder `addcont/CUSA08034`, content
  ID `UP9000-CUSA08034_00-...`).
- **Launcher:** finds the US dump by the `TITLE_ID` in `param.sfo`, under any folder name (such as
  `CUSA08034-app`), shows "✓ CUSA08034 version 01.00: all modes available", and no longer marks the
  dump as unsupported. With both dumps present, the European one is listed first.
- Tools (`tools/`) and the benchmark save template accept both serials. New tests
  (`tests/test_games.py`) fail if any code goes back to checking a single serial.
- The US version does not have the 3 Arabic strings from the European 1.01 update.

### 0.60.6 — 2026-10-22
- **No more rainbow noise on textures or "out of nowhere" crashes (`0x49500`).** The cause was
  "hot" GPU readbacks (`SOTC_RB_HOT`): along with each readback, the port speculatively wrote
  previously read regions into the game's memory; after loading a save or a new area, the game had
  already reused that memory for new textures and its heaps, and the stale GPU data landed on top.
  They are now disabled (average FPS in the demo drops by ~4%). Found using a user's reproduction
  steps: new area → wait → exit → reload the save.
- **Black dots in a grid on the sand ground** (0.60.5): the bind optimization from PR #232 is now
  disabled by default (`SOTC_FAST_BIND=1` enables it; its fix is awaiting testing).
- **PR #219 (memory leak) disabled by default**: with it, coloured noise and giant black polygons
  ("vertex explosions") appeared much more often (`SOTC_CP_FREE_TASKS=1` enables it).
- **In-game menu (F1):** only what works in SotC (the upscaler, preset, resolution and effects
  options inherited from Bloodborne did nothing); **FPS (30/60/unlimited), Upscaler and Sharpness**
  change immediately; **Maximum** textures disabled in PS4 Pro mode (it crashed within minutes); a
  hint explains what takes effect immediately.
- **Hard disk:** after the game loads, the game files (13.7 GB) are read once in order and kept in
  RAM: new locations no longer stall waiting on the HDD (`SOTC_PREFETCH=0` disables this).
- **Loads:** on each save or new area, unused textures are discarded (`SOTC_LOAD_FLUSH=0` disables
  this).
- **Event log `user/events.log`:** configuration spelled out in full, system (CPU, RAM, HDD/SSD),
  menu, keys, controller, loads, textures, per-minute performance, explained errors; launcher
  language; the last 5 sessions, plus a copy in `user/crashes` after a crash.
- PRs: #215 (pipeline preloading on workers: new driver 106 s → 7 s), #198 (4-thread CPUs), #205
  (LDS barriers on AMD GCN), #207, #212.
- Code review: world heap guard, hang watchdog, memory on PCs with little RAM, launcher "Unlimited"
  setting preserved, translucent loading screen during short stutters.

### 0.60.5 — 2026-10-21
- **Texture quality selector: Off / High / Maximum** (#235), in the launcher (Quick settings →
  Textures) and in the in-game menu (changes immediately). **High (stable)**, the default, keeps
  the protection from 0.60.3/0.60.4 (textures one level coarser when the game's texture memory is
  running out); since that memory is almost always full in large areas, 0.60.4 was less sharp than
  0.60.3. **Maximum (sharp)** removes the protection: always the finest level, but **it may crash
  in very wide views** (sword raised against the sun, the Shrine, the last colossus's area;
  `0x1a371`, #186), especially in PS4 Pro mode; if this happens, switch back to High. **Off** =
  only the game's 256×256 versions. The former "High-quality textures" checkbox becomes High (or
  Off).
- **Memory leak** (~60 MB per minute, more than 7 GB in half an hour; stutters in long sessions):
  each nested command buffer left its coroutine in memory (PR #219, Oussama268).
- **Higher FPS:** descriptors from the previous draw are reused when the next draw uses the same
  pipeline and the same resources (PR #232, Tadomikaari312-Hy): demo +3.3% on average, +12% on the
  minimum (72.8 / 37.1 → 75.2 / 41.7). `SOTC_FAST_BIND=0` disables this.
- **Loading after a graphics driver update: 106 s → 7 s** (cached pipelines are built by the worker
  threads; PR #215, Oussama268). `SOTC_ASYNC_PRELOAD=0` restores the old behaviour.
- **AMD GCN** (RX 470–590, Vega, APUs): shared memory barriers that these cards were not receiving,
  a likely fix for white characters and smoke (PR #205, Lucas-blip-png; testing on GCN cards by
  players). `SOTC_AMD64_LDS=0` disables this.
- **CPUs with 4 threads:** the command recorder and the command processor no longer occupy an
  entire core while waiting (stuck at loading → 73 FPS; PR #198, Lucas-blip-png).
- **Launcher:** no longer stops when the console refuses writes (#104, PR #207); a dump or modules
  that are still encrypted now report which file it is (PR #212).
- **Loading screen:** short in-game stutters only darken the image; it becomes opaque after 3 s
  without a frame (previously it flashed black during 1 s hangs).
- **Hang watchdog:** after 15 s without a game frame, thread stacks are written to the log
  (`SOTC_WATCHDOG=0` disables this), to track down "closed out of nowhere" cases.
- **Code review:** the world heap protection skipped the guard on subsequent heap chunks and left
  new chunks unguarded for up to 2 s; PCs with little RAM received the memory size before the
  reduction; the menu's 60 FPS button replaced "Unlimited" with "60".
- In-game menu and shortcuts (F1, F7, F8, F10, F11) explained in the README, with a screenshot.

### 0.60.4 — 2026-10-20
- **Cyan blocks on rocks and flickering flowers** (with "High-quality textures"): the 0.60.3
  adjustment that prevents the crash changed the texture detail level every one or two seconds in
  large areas (4, 3, 2, 3, 2…), and each change made the game discard or reload textures that were
  being drawn. It now lowers the detail only below 8% free memory and only raises it again above
  16%. Tested from New Game+ to the 2nd colossus: 3 changes instead of ~8, no cyan blocks, no
  flickering flowers and no crashes.

### 0.60.3 — 2026-10-20
**The game can be completed from start to finish.** Played from the save before the last colossus
through the fight with Malus, the final scenes and the credits, with "High-quality textures", PS4
Pro mode and 60 FPS enabled. Thanks to **Lucas-blip-png**.

- **Crash with "High-quality textures" in wide views** (sword raised against the sun, the last
  colossus's area; `0x1a371`, #186): when the game's texture memory starts running out, visible
  textures request one detail level lower (two when it is nearly full), and the game frees space
  before it locks up. It remains sharper than with the option disabled (PR #191, Lucas-blip-png).
- **Crash on loading after the credits** (`0x49500`): the game memory protection did not see the
  pools the world creates, because they grow in scattered 1 MB chunks; each one is now protected
  where its free blocks are (#146).
- **+2.7% FPS in the temple:** GPU readbacks in 64 KB windows (PR #196, Lucas-blip-png).

### 0.60.2 — 2026-10-20
- **In-game quick settings:** in the menu (Insert, F1 or L3+R3), without leaving the game: 60 FPS,
  frame generation (off / DLSS-G / FSR 3), high-quality textures, reuse of distant shadows, and
  volume. They take effect immediately and are saved to the launcher for next time. 16x AF, PS4 Pro
  mode, resolution and video memory usage remain in the launcher (they require a restart).
- **Mouse in the menu:** while the menu is open, the mouse is released and visible for clicking;
  when the menu closes, it controls the camera again.
- **Animated loading screen also during shader building** when the game starts (previously the
  window was frozen, sometimes for minutes after a driver update), with an opaque black background
  and an animation in the centre (spinning circle, pulsing title, "Loading…").
- Menu title and Quick settings in the launcher language.

### 0.60.1 — 2026-10-20
Thanks to **nosekefik, Oussama268, plassardewen-cpu and lunar-me**.

- **In-game menu** (Insert, F1 or L3+R3): FPS, graphics card and video memory, fullscreen,
  resolution, scaling and sharpness, camera and mouse, effects (fog, vignette, occlusion, water
  reflections). This was the Bloodborne menu, hidden in the port; it is now SotC's own and enabled
  by default (`BB_OVERLAY_MENU=0` disables it). Text in English (PR #94, nosekefik).
- **Bloodborne leftovers removed** (~11,700 lines: old launcher, Bloodborne patches and mods,
  icons, documents), based on PR #60 by Oussama268. The game executable (`bb-probe.exe`) now has
  the SotC icon.
- The game search ignores the Windows `WpSystem` folder (PR #110, plassardewen-cpu).
- GitHub page in English only.

### 0.60 — 2026-10-20
Thanks to **Lucas-blip-png, Einar and KingJhoss**.

**Fixes**
- **Textures on GTX 1060/1070/1080 (Pascal):** untextured surfaces, flat or nearly black colours
  (#57, #68, #102). Pascal lacks the interpolation feature the game uses; it is emulated with a
  geometry shader (PR #169, Lucas-blip-png).
- **Game freezing or closing on the title screen** on some PCs (CPU cores with differing clocks):
  the game clock now comes from Windows (PR #174, Einar).
- **Failed to start on PCs with little memory** (regression in 0.59): if Windows refuses the
  9152 MB, the game uses the console size and records a warning in the log.
- **FSR 3: black squares in generated frames** on AMD RX 7000 (PR #177, KingJhoss).
- **PLAY button clipped** with Windows scaling above 100% (PR #171).
- Fix in code inherited from shadPS4: atomic floating-point min/max on cards without
  `VK_EXT_shader_atomic_float2` performed two operations instead of one.

**New**
- **60 FPS mode using the game's own Performance mode** (the PS4 Pro one): the game now targets
  60 FPS, so its timing keeps pace. This should fix incorrect physics at 60 FPS (#100, #120); still
  being tested on those colossi (PR #183, Lucas-blip-png). `SOTC_MODES=af16` restores the old mode.
- **Each copy of the port has its own `user` folder** (saves, cache, logs and launcher settings in
  `user\launcher_settings.json`). Previously all copies used the folder of the first copy. On first
  run, the new copy inherits the settings and **copies** the saves and sound modules.
- **+6% FPS in the temple:** the game memory protection is no longer rebuilt every frame (PR #168,
  Lucas-blip-png).
- Frame generation without the interface (health bar without flicker), **experimental and
  disabled**: `SOTC_FG_HUDLESS=1` (PR #154, Lucas-blip-png).
- When playing through `SotC.bat`, the log is also written to `user\last_run.log` (PR #175, Einar);
  benchmarking tools fixed (PR #181, Einar).

**Known issues:** "High-quality textures" may still crash the game in very wide views (sword raised
against the sun, the last colossus's area; #186): disable it if this happens. White characters on
AMD GCN (RX 470–590, Vega, APUs): under investigation.

### 0.59.1 — 2026-10-20
Thanks to **Lucas-blip-png and Oussama268**.

- **Flickering distant shadows** (#178, #157, #163; AMD and NVIDIA GTX 1080, RTX 40/50): shadow
  reuse (since 0.58) redrew distant shadows only every other frame, and the game does not keep the
  shadow for the skipped frame. It is now disabled (PR #170, Lucas-blip-png). Measured over the
  entire demo: off 61.8 FPS average / 37.2 minimum, on 59.5 / 35.6, so it did not gain any FPS. New
  **Distant shadows** option in the launcher (Off / On / Automatic).
- **AMD: stripes on shadows and water** (Oussama268's fork, opt8): direct memory access is disabled
  by default on AMD cards (it can be enabled in the launcher).
- **Intel, partial fixes** (Oussama268): valid dispatch sizes and layouts, no 64-bit integers when
  the GPU lacks them, fixed subgroup size.
- **The log shows the graphics card and driver** (`GPU:` line), for issue reports;
  `SOTC_GPU_VENDOR=10de|1002|8086` selects the GPU on laptops with two (Oussama268).
- Minor review fixes (Oussama268).

### 0.59 — 2026-10-20
Thanks to **Oussama268, Lucas-blip-png and EdouardSence**.

**Performance**
- **Higher FPS and fewer stutters** (Oussama268's fork, opt5–opt7): draws are prepared on a
  dedicated thread (#99 Lucas-blip-png), direct memory is uploaded incrementally, and shaders and
  compute pipelines are translated on worker threads (the object waits a few frames instead of the
  screen freezing). Title screen demo: average 63 → 65–67 FPS, minimum 30 → 34–37, bridge
  30–32 → 37–40.

**Stability**
- **Crash at the end of the game and when loading saves** (`0x1a371` / `0x49500`): the GPU
  sometimes returned stale data to memory the game had already freed, corrupting the allocator.
  The Pink-shadPS4 protection covered only one of the game's memory pools; it now covers all pools
  of the same type, including those created when the world loads (the last colossus's area). The
  game also receives 9152 MB of direct memory in all modes (#146, #149). Cause found using logs
  from real crashes; a full playthrough to the credits is still being tested.
- **6 GB cards** closing due to insufficient video memory: the exact size and the PC's RAM are
  tried before giving up (#137).
- **Launcher on Chinese Windows** broke on the "™" in the title (#151).

**New**
- **F11** toggles windowed / fullscreen (#118, EdouardSence).
- **Video memory usage:** Economy, Standard, High and Maximum (previously the game purged textures
  from 60% of the card's memory, the "6 GB limit" on 8–16 GB cards) (#143).
- **Volume:** 100 to 300%, without clipping (#95).
- **Automatic updates:** the launcher notifies you when a new version is released and installs it
  automatically, keeping saves, the shader cache and sotc.ini (#136, Oussama268). Applies from this
  version onward.
- **Ultrawide with PS4 Pro mode:** 21:9 and 32:9 had black borders with Pro mode enabled; the wide
  screen now takes priority (#142, #145).

### 0.58.1 — 2026-10-19
- **High-quality textures** (enabled by default in Quick settings): since 0.0.13 the port never
  told the game which textures were in use, so it did not load the high-resolution versions
  (blurry leaves on the opening tree and blurry rocks). When disabled, the old behaviour returns,
  for cards with little video memory.
- **Game window icon:** an artwork made for the port (the boy raising the sword), so that it does
  not look the same as the launcher's in the taskbar. In Image and FPS: default, the game icon, or
  an image of your own.

### 0.58 — 2026-10-19
Community release: combines pull requests and forks from several people, reviewed and tested
together. Thanks to **Lucas-blip-png, Gabriell-os, Oussama268, jownatan, xeodeo, nakaionyt/y7ngboy
and tabnul**.

**Stability**
- **Crashes due to insufficient video memory** (`EnsureResident: Assertion Failed`, AMD and
  4–6 GB cards): buffer memory is allocated in 64 MB blocks (previously thousands of allocations,
  above the driver limit), and idle memory is returned during use after the data is verified
  (Gabriell-os #98, Oussama268 #59/#83/#103).
- **Game freezing** ("GPU hook queue overflow"): the memory notification queue grows on demand.
- **Crash after the 1st and 2nd colossus in PS4 Pro mode** (`0x1a371`): Pro mode receives the
  memory of a PS4 Pro (Lucas-blip-png #106).
- Padding packet in the compute queue that corrupted the following packet; RTZ rounding disabled
  on NVIDIA (the driver hung); region lock without deadlock on Windows (jownatan #81).

**Performance:** title screen demo 54–55 → **62 FPS average**, minimum 26 → 31; video memory
~4.3 → ~2.7 GB (reductions in the command processor, pipelines compiled in the background, caches
read in parallel, distant shadows reused).

**AMD:** black block in the corner of the screen on RDNA3/RDNA4 (#43) and BC5/BC6H textures
(xeodeo); wave64 uniform branches and periodic submission enabled on AMD (Gabriell-os #98). Not
yet tested on an AMD card here: please report in the issues.

**New**
- **Widescreen / ultrawide:** 16:10, 21:9 and 32:9 with a wider image, without stretching
  (tabnul #54).
- **FPS: 30 / 60 / unlimited** in the launcher.
- **Xbox button icons** in the game (A B X Y, RB, LT/RT, Menu, View), drawn for the port;
  PlayStation / Xbox option in Controls (texture replacement mechanism by xeodeo).
- **Frame generation (experimental):** FSR 3 on any card and DLSS-G on RTX 40/50 (official NVIDIA
  runtime in the package, with the licences), with the game at 30 → 60 on screen
  (nakaionyt/y7ngboy). It only makes a difference with the FPS at 30, and the health/stamina bar
  may flicker.
- Performance overlay (F10), F7 toggles the 30 FPS limit, F8 switches frame generation.
- **Launcher in Spanish, Chinese, Korean and Russian.**
- Window with the game icon and, in the title bar, the version and FPS.
- **Save list with the screenshot of each save**, as on PS4 / shadPS4.
- **Loading screen** with a percentage bar while the shader cache is read.
- Language selected right on the first launcher screen; PS4 Pro mode and Sharpness (RCAS) in Quick
  settings.
- The shader cache is rebuilt on the first start.

### 1.0.1 — 2026-10-18 (fork MMadmer)
- **Port sound by default:** the launcher no longer copies the sound modules of an installed
  shadPS4 (`user/sys_modules`) on its own, nor enables them: the port's own sound library plays all
  sounds. With the automatically copied modules, most sound effects (hits, hooves, arrows) were
  reported missing, leaving only the music and voices. Configurations in which an earlier launcher
  made this copy revert to the port's library; a manually chosen folder still applies.
- **libSceAudio3d and NGS2 UserFx:** the port implements libSceAudio3d (objects positioned in
  stereo by azimuth and spread, played on the default output device) and calls the UserFx of the
  NGS2 sampler voices, through which the game's sound engine (sndx) delivers 3D audio objects when
  that mode is active. Sounds that the PS4 sends to the controller speaker, the headset or the
  auxiliary output also play on the default device.
- **Window mode:** windowed, borderless fullscreen (no borders, at the monitor resolution) or
  exclusive fullscreen (the monitor switches to the output resolution), replacing the "Fullscreen"
  checkbox (`SOTC_WINDOW_MODE=windowed|borderless|fullscreen`; a checked box becomes borderless
  fullscreen).
- **Launcher languages:** English, Russian, Chinese (Simplified), Japanese and Portuguese, with
  instant switching and Windows fonts for Chinese and Japanese; the automatic language follows the
  Windows language.
- Russian documentation texts revised in a formal style.

### 1.0.0 — 2026-10-18 (fork MMadmer)
First release of the fork [MMadmer/ShadowOfTheColossus-PC](https://github.com/MMadmer/ShadowOfTheColossus-PC),
with everything from 0.57-opt6 and 0.57-opt7.
- **Full Xbox controller support and the buttons of the device in use:** with an Xbox (or
  generic) controller, the game shows Xbox buttons (A/B/X/Y, LB/RB, LT/RT, LS/RS, Menu, View,
  D-pad), white in a black circle; with keyboard and mouse, the configured keys and mouse buttons,
  in the same style (the D-pad and the left stick as the four keys, with the key for the current
  direction lit; the camera as the mouse); with DualShock or DualSense, the game's own. The diagram
  in Options > Controls becomes an Xbox controller, or the keys and mouse of each row. Everything is
  drawn by a shader at startup (along with shader building), much finer than the game's and sharp
  at any resolution. The game's textures are recognized by their content when they are uploaded to
  the GPU, and the swap is done at bind time; the game's memory and files are not changed. The swap
  is instant: connecting, disconnecting or using another device switches the buttons shown and the
  controller diagram (`SOTC_PAD_GLYPHS=xbox|ps|keyboard` forces one; **Button icons** option in
  Controls).
- **Vibration** identical to the PS4 on any controller: the value the game requests holds until
  the game changes it (previously it ended after 1 s) and carries over to a controller connected
  mid-session.
- **Installer and zip:** `SotC-PC-Setup-<version>.exe` installs for the current user (no
  administrator rights), with shortcuts and an uninstaller that keeps saves and settings; the zip
  contains the same folder, portable.
- **One-command build:** `build.cmd` downloads its own MSYS2 into `.deps`, installs the packages,
  the submodules and PkgTool, builds and opens the launcher; `build.cmd -Package` produces the zip
  and the installer. Test bench in `tools/bench`.
- Sound uses the port's own library; the console sound modules are optional.
- Russian guide: `docs/README_RU.md` (shipped in the package as `README_RU.md`).

### 0.57-opt7 — 2026-10-18
- **Command processor without busy-waiting:** when all packets are waiting (WAIT_REG_MEM,
  semaphores, CE/DE counters), it spun at 100% (clock and lock on every iteration: a third of its
  time). Now, after a few iterations, it sleeps until the draw thread executes the next operation,
  or yields the core (SwitchToThread) and naps when no draw is queued (`SOTC_CP_IDLE=0` restores
  the previous behavior). Shrine: 17.6 → 14.3 ms of parsing per frame.
- **Draw thread:** the render targets of a draw with the same target registers and the same set of
  images are reused without a new lookup (`SOTC_RT_MEMO=0` disables); the thread check in register
  reads no longer calls winpthreads (~2.5% of the thread); re-marking a buffer already written by
  the GPU does not take the lock.
- **Repeated push descriptors discarded:** a reused bind keeps the flat buffer copy in the same
  command buffer, and the recorder discards push descriptors and push constants identical to the
  last ones (`SOTC_PUSH_DEDUPE=0` disables). Fewer driver calls on the recording thread.
- **Up to 4 cores:** the recorder yields the core while waiting for work (`SOTC_REC_POLITE`)
  instead of spinning at high priority.
- **Sound:** the NGS2 mixer reverb keeps the filter state in registers and does not allocate
  buffers for each block (same result, bit for bit).
- **Automatic render resolution:** on the first launch with a given graphics card, the launcher
  picks 810p for GTX 1050, MX and 2 GB cards, 900p for GTX 1050 Ti and cards with up to 3 GB, 720p
  for integrated graphics; this happens once per card, and a choice made in Graphics is kept.
- An invalid T# read to compile a shader (write via FMask) no longer terminates the game.
- Tested (4 cores without SMT + emulated 2 GB budget, RTX 3070 Laptop): Shrine 30 FPS standing
  still, 29.6 on average while walking (previously 29.0); colossus 13 at 810p with running and the
  camera rotating 30.0 FPS (minimum 30.0), 1.85 cores busy (previously 2.1), GPU 5.1 ms at
  2100 MHz and 1.77 GB of VRAM; 62% of the Shrine's push descriptors discarded.
- Diagnostics: `SOTC_PROFILE="A|B|C"` samples several threads and `SOTC_PROFILE_STACKS=<arquivo>`
  records full stacks (`tools/profile_tree.py` builds the call tree); `SOTC_CENSUS_DUMP=<arquivo>`
  lists the images in the cache.

### 0.57-opt6 — 2026-10-18
- **4-core PCs:** the game's four workers kept polling the task queue nonstop (on the PS4 each one
  has its own core) and took the cores away from the render threads. Now, after a series of empty
  polls, they sleep for 0.1 ms (`SOTC_POLL_NAP_US`, 0 disables). The PS4 mutexes use Windows SRW
  locks. Shrine: 7 → 3.5 cores busy, 27.4 → 29.9 FPS. With up to 4 cores, the command and draw
  threads run at high priority, and so does the sound mixer (`SOTC_GPU_PRIORITY`).
- **GTX 10 (Pascal):** the barycentrics read by the game's pixel shaders go through a
  *passthrough* geometry shader (VK_NV_geometry_shader_passthrough): the pixel shader resolves the
  barycentrics from the position. Shrine: GPU 16.1 → 13.8 ms on the Pascal profile
  (`SOTC_GS_PASSTHROUGH=0` restores the previous behavior). The shader cache is rebuilt on the first
  start.
- **Render resolution** (launcher, modes `res900`/`res810`/`res720`/`res960`): the game renders
  below 1080p and upscales to 1080p itself, as the PS4 Pro does from 1440p to 4K. Textures and fog
  are unchanged.
- **Shaders compiled once:** the `shaderpack` folder contains the pipeline cache recorded in the
  game; on first launch it becomes the player's cache and all pipelines are built on the loading
  screen, with no compilation during gameplay.
- **The launcher unpacks .pkg dumps:** the game, the update and all DLC found become the folders
  `CUSA08809`, `CUSA08809-UPDATE` and `CUSA08809-DLC` next to the packages (PkgTool, from
  LibOrbisPkg, LGPL-3.0, in `tools/PkgTool`). This includes DLC that only unlocks content
  (license).
- **Less work per draw:** two thirds of binds repeat the previous one and reuse the descriptors
  (`SOTC_BIND_REUSE=0` disables); the shader permutation lookup is memoized; the game heap
  protection on GPU reads walks the free tree at most once every 250 ms.
- **Cards with less than 3 GB:** buffer memory stays at ~45% of the budget (blocks idle for 8 s are
  released). `SOTC_VRAM_LIMIT_MB` (emulated card) no longer refuses allocations: the `SOTC VRAM`
  line shows by how much the emulated size has been exceeded.
- The system language follows the Windows language when the launcher is left on "Automatic";
  Russian added to the list.
- Tested (4 cores without SMT + emulated 2 GB budget, RTX 3070 Laptop): Shrine 30 FPS (the game's
  cap), title screen demo 30.0 FPS (minimum 29.9).

### 0.57-opt5 — 2026-10-17 (fork Oussama268)
- **Long sessions without video memory growth:** the game keeps its 4 GB memory block mapped and
  reuses the addresses all the time, so GPU buffer memory only grew (~45 MB per minute in the title
  screen demo: 1.6 → 4.3–4.4 GB in 30 minutes). On a 6 GB card this exceeded the budget after
  ~40 minutes of play and the stutters returned. Now, when buffer memory exceeds 1.5 GB, blocks that
  have not been used for 30 seconds and contain no GPU-written data are released (at most 64 MB
  every 2 seconds, ~20 ms each time). Before releasing a block, the port reads it back and compares
  it with the game's memory, so the image does not change. In 30 minutes of demo: video memory peak
  **3.4 GB, stable** (previously 4.3–4.4 GB and rising), no emergency collections.
  `SOTC_ARENA_EVICT_MB=0` disables; `SOTC_ARENA_EVICT_MB`, `SOTC_ARENA_EVICT_IDLE_S` and
  `SOTC_ARENA_EVICT_STEP_MB` adjust it.
- **Slightly faster open world:** when a command writes to a page that the game will read back,
  the port submits the work to the GPU right away (`SOTC_RB_EARLY`, now the default). Demo:
  55.0 → 56.2 FPS (+2%); Shrine unchanged.
- **Rare crash fix:** 0.57-opt4 sometimes closed when loading a save with the error "Attempted to
  protect region as write-only" (2 times in ~16 runs at the Shrine). The probable cause has been
  fixed and, if it still happens, the whole page is protected and the game continues; the log
  (`user/last_run.log`) records a "Write-only protection" line for investigation.
- PGO profile rebuilt for the new code.
- Diagnostics: with `SOTC_FPS=1` the log shows the `SOTC ARENA` line (released memory).
- Tested: 30 minutes of demo (stable memory), 15 minutes with the final build, demo and Shrine
  without errors (interleaved pairs against 0.57-opt4), identical images (screenshot sheet).

### 0.57-opt4 — 2026-10-16 (fork Oussama268)
- **More FPS everywhere:** the thread that turns the game's commands into Vulkan commands (the
  port's bottleneck) spent ~42% of its time inside Windows and the C library: locks, page
  protection changes, copies and allocations. Measured against 0.57-opt3, on the same PC at the
  same time (RTX 3060 Laptop, interleaved pairs):
  - Shrine: 29.3 / 29.1 → **35.0 / 34.1 FPS** (+18%).
  - Title screen demo: 47.4 → **53.5 FPS**, 1% low 29.4 → **37.9**, frames over 50 ms
    129 → **26**.
- **What changed:**
  - The texture cache lock is taken once per group of textures, not twice per texture (62,000
    times per frame), and on the command thread side it no longer uses an atomic instruction.
    Game write faults on pages without textures do not go through the lock at all.
    `SOTC_TC_BATCH=0`, `SOTC_TC_BIASED=0` and `SOTC_TC_SKIP_EMPTY=0` disable the individual parts.
  - The tables that the game fills every frame with a copy shader (done on the CPU by the port) are
    compared page by page, and only the pages that changed are written, directly into GPU memory,
    without marking the memory as modified: per-frame buffer uploads dropped from 21 to ~13 MB.
    `SOTC_FILL_HLE_PAGES=0` restores the old method.
  - A pair of 960x540 depth images was created and destroyed every frame; it is now reused
    ("depth twin", which already existed as an option and is now the default).
    `SOTC_DEPTH_TWIN=0` disables.
  - Faster memory lookups: a larger per-thread cache of the game's memory regions
    (`SOTC_REGION_DIRECT=0` disables), a cache of already resident buffer ranges, the clock read
    every 8 commands.
  - PGO profile rebuilt for the new code.
- Diagnostics: with `SOTC_CMD_STATS=1` the log shows the `SOTC sys/frame` line (page protections,
  locks, video memory allocations per frame).
- Tested: Shrine and demo without errors, identical images (screenshot sheet), 15 minutes of demo.
### 0.57-opt3 — 2026-10-16 (fork Oussama268)
- **Fewer hitches on the first visit to an area:** new graphics pipelines are now built on
  separate threads, and an object that needs a pipeline still being built is not drawn for a few
  frames (it appears a moment later) instead of the whole game stalling. With an empty shader
  cache, in the title screen demo: frames over 50 ms 235 → 156, 1% low 23.9 → 29.1 FPS.
  Fullscreen passes (post-processing) are still built immediately, so that no black frame appears.
  `SOTC_ASYNC_SHADERS=0` restores the old behavior.
- Shader translation (most of the remaining hitching) still runs on the main command thread;
  moving it to the background is the next step.
- The shader patch folder is listed only once, instead of two disk queries per shader.
- Diagnostics: with `SOTC_FPS=1`, the `SOTC COMPILE` line shows where compilation time goes.
- Tested: 15 minutes of demo with an empty cache without errors; with a full cache, Shrine
  32.4 FPS and demo 50.0 FPS (no loss).

### 0.57-opt2 — 2026-10-16 (fork Oussama268)
- **Far fewer stutters when entering new areas (6 GB cards):** textures whose T# starts at a mip
  level above 0 never use the finest mips, but the port kept the whole chain in video memory. Now
  these read-only textures keep only the mips in use ("mip tail"). In the title screen demo, peak
  video memory dropped from ~6.1 GB to ~3.4 GB on a 6 GB card, frames over 50 ms dropped from 150 to
  ~105, and the worst frame when loading an area from 460 ms to ~280 ms. Identical image.
  `SOTC_MIP_TAIL=0` disables.
- **Asynchronous GPU reads by default:** when the game reads memory written by the GPU (every
  frame), only the game thread waits for the GPU; the command processor keeps working. Shrine
  29.2 → 30.7 FPS; in the demo, ~25% fewer frames over 50 ms. `SOTC_RB_ASYNC=0` disables.
- **Buffer memory released on unmap:** previously it was never returned and video memory grew
  ~90 MB per minute (1.6 → 6.0 GB in 48 minutes in the demo) until memory ran out. Now it grows much
  more slowly (24 minutes: 4.1 GB instead of 4.8–5.0 GB). It was optional because of an old crash
  when loading a save, which did not recur in testing (3 loads, 25 minutes of demo).
  `SOTC_ARENA_RELEASE=0` restores the old behavior.
- With all of this enabled, the demo ran at 49.9 FPS on average with 47 frames over 50 ms
  (previously: ~47 FPS and 100–150).
- **Crashes leave a record:** the launcher keeps the logs of the last 5 sessions (`last_run.1.log`
  …) and copies the log of a session that ended with an error to `user/crashes/`; bb-probe writes a
  minidump (`.dmp`) there when the game crashes. `SOTC_MINIDUMP=0` disables the dump.
- **Stutter report for developers:** with `SOTC_FPS=1` the log shows, every 5 s, the frame time
  (median, 1% low, worst frame) and where the time of slow frames went; `tools/frame_report.py`
  summarizes a log.
- Buffer memory in 4 MB blocks instead of one allocation per request (`SOTC_ARENA_SLAB_MB`),
  parallel copying of large uploads (`SOTC_COPY_BATCH`).
- **Not yet resolved:** the first time in each area, shaders are compiled and the game stutters
  (up to 1–2 s); from the second time on it is smooth. Background compilation is the next step.
- On first launch some new shaders are compiled (a few seconds); the existing cache remains
  valid.

### 0.57 — 2026-10-14
- **AMD cards: fix for green/pink colors** in the fog, on Wander and on Agro (issues #6, #7, #9,
  possibly #10). AMD reports 64-thread groups, but only allows that size to be fixed in compute
  shaders; the port required 64 in all stages and failed to convert the compute shaders. AMD cards
  now use the same 32-thread path as NVIDIA (compute fixed at 32, shared memory barriers, without
  the uniform-branch optimization that made characters black in shadow). Diagnosis by
  raulbezerro-dev in issue #7. Nothing changes on NVIDIA.
  **Not yet tested on an AMD card:** please report in the issue whether it improved.
- **New launcher option:** "Full water reflections (direct memory access)", enabled by default.
  When disabled, FPS increases by ~10% (title screen demo: 54 → 59), but distant water looks
  smoother. Each mode has its own shader cache folder.
- The shader cache is rebuilt on the first start.

### 0.56 — 2026-10-13
- **Open-world water with waves:** lakes and rivers below the waterfalls were smooth as a mirror,
  without movement. This water's shader saves the active thread mask with `s_wqm_b64 vcc, exec`
  and then uses it to decide whether to apply the flow map (the waves). The recompiler treated
  `S_WQM_B64` as "do nothing", VCC held garbage and the shader always wrote a normal pointing
  straight up. Now the instruction does what the hardware does: it expands the mask to whole
  2×2 pixel quads. A simple copy fixed the water but left "colored garbage" in the canopies of
  distant trees, which use the expansion in the transparency test. Details in
  `docs/sotc/WATER.md`.
- **Direct memory access (DMA) enabled by default,** as in the Pink-shadPS4 configuration for
  SotC. Reads from tables with an index computed in the shader (the *probe* records used by the
  screen-space reflection) always read position 0; they now read the correct address. As a result,
  the upper lake ripples and reflects far into the distance. `BB_DIRECT_MEMORY_ACCESS=0` disables.
- The shader cache is rebuilt on the first start (the first load stutters).

### 0.0.13 — 2026-10-13
- **Fix for the 0.0.12 crash:** the game could close during the opening or in the menu (fault at
  `0x1a371`, 1 in 3 runs). Cause: the port marked every texture seen as "still in use" for
  ~3 seconds, and the game tried to load more textures than fit in its pool. Now only the textures
  used since the last request count (as in Pink-shadPS4). Tested: 0 crashes in 5 runs of the
  opening and a session of almost 40 minutes walking around the map, with two save loads.
- **Much larger water reflections:** the lake now reflects cliffs, temples and vegetation across
  almost the entire surface, with ripples. A loop safety limit in the shaders cut off the
  screen-space reflection rays; the limit is now per loop.
- Under investigation: where the water should reflect the sky, it still looks dark and smooth.

### 0.0.12 — 2026-10-13
- **Sharp textures are back, without the game closing:** the memory corruption that made the game
  close right after loading a save (fault at `0x1a371`) and left textures with "colored garbage"
  came from the emulator copying GPU data into memory that the game was freeing. This has been
  fixed, and texture streaming is enabled again at level 2 (2048 textures up to 512×512): rocks
  and walls are much sharper. Tested for more than 25 minutes near the waterfall, without crashes.
- **Water:** splashes, rings and foam around Wander when swimming; the lake reflects the sky and
  the sun's glare. Environment reflections (local IBL pass) now use the correct *cubemap* for each
  *probe* — a fix brought over from Pink-shadPS4 (`dynamic_tsharp_array_size`).
- Under investigation: the water near the camera still has no ripples.
- Options for testing: `SOTC_LOD_STATS=0` (streaming disabled), `SOTC_LOD_LEVEL`,
  `SOTC_UNMAP_READBACK=1`, `SOTC_TSHARP_ARRAY=0`.

### 0.0.11 — 2026-10-13
- **Fix for 0.0.10 — grass with a "colored garbage" texture:** at streaming level 3 the game freed
  mips that the GPU was still using (seen on the grass near the waterfall). Texture streaming now
  ships **disabled**, as in 0.0.9: normal grass, waterfall curtains visible and no crashes when
  loading a save. To test streaming: `SOTC_LOD_STATS=1` (with `SOTC_LOD_LEVEL=2` it is sharper,
  but may close the game).
- Under investigation: the still water (mirror-like lake, no waves) and the waterfall spray.

### 0.0.10 — 2026-10-13
- **Saving fixed:** the game could never save ("no free space"); it now saves.
- **On-screen save list:** "Load Game" and "Manual Save" show the saves (region, play time, date)
  to choose from, with "+ New Save" when saving. Arrows/D-pad to choose, Enter/X to confirm,
  Esc/O to go back, double-click with the mouse.
- **Delete save:** in the list, Delete (keyboard) or Triangle/Y (controller), with confirmation.
- **High-resolution textures (streaming):** the port now responds to the GPU texture statistics
  command (`GET_LOD_STATS`) that the game uses to decide what to load. At level 2, 2048 textures
  reached 512×512 and the waterfalls gained their water curtains.
- **Game closing on its own after loading a save:** streaming at level 2 corrupted the game's
  memory (3 crashes in 4 runs near the waterfall). The default is now level 3, which passed 4 of
  4 runs without crashing; at this level 2048 textures stay at 256×256, as before streaming (the
  waterfall curtains have not yet been checked at this level). `SOTC_LOD_LEVEL=2` returns to the
  sharper level, with the risk of crashing.
- **Black shadows fixed:** grass, rocks and walls in shadow were black. Three shader recompiler
  fixes brought over from Pink-shadPS4 (fetch shader 1.0 constants that were ignored,
  `V_ALIGNBIT`/`V_ALIGNBYTE` with a shift of 0, `V_CVT_PK_U8_F32`).
- **GPU particles:** shared memory barriers in groups smaller than a wave; a particle system that
  never appeared is back. The waterfall spray and foam still do not appear (under investigation).
- The launcher writes the game log to `user/last_run.log`, with the exit code.
- Diagnostic tools: F12 captures a frame when the game runs under RenderDoc.
- New icon (the face of a colossus).
- **Note:** if switching saves within the same session closes the game, restart it before loading
  another save.
- Full project review in `docs/sotc/PROJECT-REVIEW.md`.

### 0.0.9 — 2026-10-12
- **Automatic initial setup:** on first run, `SotC.exe` opens a welcome screen that only asks for
  the game folder (or offers the one it found on the PC). It then configures on its own the DLCs
  (inside the game folder and next to it), the saves (it copies a shadPS4 save if the port does not
  have one yet) and the sound, and shows the result. **Configure again** repeats the process.
- **Main folder as in shadPS4:** you point to the game's main folder and it finds the subfolders of
  the base game (CUSA08809), the update and the DLCs.
- **Update:** the `CUSA08809-UPDATE` or `CUSA08809-patch` folder next to the game is used as in
  shadPS4: its files take priority over those of the base game (`SOTC_UPDATE_DIR`). If it contains
  `eboot.bin`, that is the one that runs.
- Cleaner main screen: the game log became an unobtrusive link; the verification shows the DLCs.
- **DLC folder** in the launcher (Game and saves), as in shadPS4. It is also found by the
  automatic search: the shadPS4 `addcont` folder or a folder with one subfolder per DLC.
- The runtime lists the installed DLCs to the game (`sceAppContentGetAddcontInfoList`), reading
  the `CONTENT_ID` from the `param.sfo` of each one (`SOTC_DLC_DIR`).
- **DLC in .pkg without installing:** DLC packages (type `0x1b`) are read directly from the
  header, without extraction. This is sufficient for the game's DLCs, which only unlock items
  (ancient bow, cloak, sword, horses). The search looks in the chosen folder, the game folder and
  the neighboring folders.
- **PS4 Pro mode:** the game log shows whether the game was launched as a PS4 Pro
  (`sceKernelIsNeoMode -> 1`). The option only unlocks the PS4 Pro Graphics Mode: you must choose
  **Cinematic** or **Performance** in the game, under Options > Presentation.

### 0.0.8 — 2026-10-12

First public release.

#### Launcher (`SotC.exe`)
- **Automatic search** on startup: finds on the PC's drives the game (CUSA08809), the user's sound
  modules in an installed shadPS4 (copied to the port's data folder) and shadPS4 saves (it asks
  before importing). **Search PC** repeats the search.
- **Game library** like that of shadPS4: game folders, a list with icon, title, ID and version.
- **Key rebinding:** click the action and press the new key (saved as `key.<entrada>=` in
  `sotc.ini`).
- **New icon** (original art). The window and the desktop shortcut use the `icon0.png` from your
  dump.
- **GPU work submission:** Automatic (NVIDIA), On for any card (AMD/Intel, experimental) or Off.

#### Performance
Title screen demo, 60 FPS mode, i9-13900K + RTX 2060 SUPER: average 37–45 → **~67 FPS**,
heaviest section ~17 → **29–34 FPS**.
- Vulkan commands recorded and submitted on a dedicated thread (`cp_record_thread` from
  Pink-shadPS4).
- Periodic GPU work submission by time (every 0.5 ms) instead of by number of commands.
- Indirect draws with arguments on the CPU become direct draws.
- Caches: image descriptions by T#, image lookups, shader binary information and per-thread memory
  pointer.
- Lock-free buffer sync check when nothing has changed; `RangeSet::Add` does not reinsert intervals
  that are already marked.
- Write faults: the faulting page and the following ones, with no GPU data, are released together
  (~50,000 → ~4,000 faults per second).
- Readbacks:
  - recently read ranges are downloaded together;
  - "clean" reads in shader resource tables, with the bitmap tracking the actual protection of the
    pages;
  - descriptor table filling (compute `0x50784801`) is done on the CPU;
  - early reads.
- From Pink-shadPS4: the cvars `doIndexBufferCulling=0 doSunShadowIndexBufferCulling=0` and the
  protection of the BPE heap against GPU writes.
- Optional, for testing: asynchronous readback (`SOTC_RB_ASYNC=1`) and wait marker
  (`SOTC_WAIT_MARKER=2`).

#### Fixes
- GPU queue hang with recording on a dedicated thread: presentation waits for the frame to reach
  the queue.
- Silent death caused by page protection on a fiber's stack.
- Black screen at boot; crash in PS4 Pro mode (`alt_tile` on video buffers).

### Before (development, 7–8 Oct 2026)
- Game boot in the bbport loader (imports by NID, fibers, SSE4a, image below 2 GiB).
- Video core from the shadPS4 SotC fork; sound with the user's modules.
- 60 FPS and 16x AF modes as loader patches; frozen Tkinter launcher (`SotC.exe`).
- Keyboard and controller together, mouse camera, 720p–4K output, PS4 Pro mode, engine effects.

---

## Русский

### 1.2.0 — 2026-10-31 (форк Madmer)
Включает в форк версию 0.70 исходного проекта и версии 0.60.6-C1–C3 (см. ниже), часть открытых в
нём pull requests и исправления ещё открытых issues. Благодарим **tabnul, Lucas-blip-png,
HiddenUserHere, Lukto02, Oussama268, InkAurora, NathanMBR, Gabriel-TwoBits и AjncSystem**. Кэш
шейдеров создаётся заново при первом запуске.

**Новое**
- **Американская версия (CUSA08034)** из исходного проекта: исполняемый файл тот же, что у
  европейской 1.01; доступны все режимы, а сохранения, кэш шейдеров и DLC разделены по регионам.
- **NVIDIA DLSS** теперь использует реализацию исходного проекта (tabnul): работает также в режиме
  PS4 Pro, при любом соотношении сторон и в 4K, а интерфейс отрисовывается в выходном разрешении.
  Игра рендерит в выходном разрешении, делённом на коэффициент режима (в режиме PS4 Pro — в
  размерах самой Pro); DLAA — режим с коэффициентом 1. Без видеокарты RTX то же изображение
  масштабирует FSR 1. При выводе в HDR вместо DLSS используется FSR 1.
- **Множитель DLSS-G до x6 и режим «Динамический»** (GeForce RTX 50, в зависимости от драйвера) в
  лаунчере и в меню игры (PR #314).
- **Трава:** тени, дальность, высота, детализация и плавное исчезновение травы и кустарника в
  лаунчере (Графика > Трава) и в меню игры, из исходного проекта (tabnul).
- **Тени от солнца** (Графика > Эффекты и меню игры): их отключение повышает FPS, когда
  производительность ограничена процессором (#294).
- **Равномерная подача кадров** при неограниченном FPS (PR #351, tabnul): каждый кадр выводится в
  собственном темпе игры, без чередования 8 и 17 мс; меню игры (F1) или `SOTC_FLIP_PACER=0`.
- **Страница «Отладка»** в лаунчере: отладочное меню Bluepoint (F6 или Start+Select), быстрые
  считывания из GPU (экспериментально), контроль памяти текстур и сравнение DLSS/FSR 1 (F9).
- **Текстуры «Максимум» в режиме PS4 Pro** (экспериментально) с исправлением страничного
  аллокатора игры (tabnul, `0x1a371`).
- Примечания к автоматическому обновлению отображаются на языке лаунчера, если они есть в выпуске.

**Исправления**
- **Расход памяти:** кадры вложенных командных задач никогда не освобождались (около 44 МБ в
  минуту, 2,6 ГБ в час) — это часть причины сообщённого высокого потребления ОЗУ (Lucas-blip-png).
  Журнал раз в минуту показывает объём памяти процесса (`SOTC RAM`).
- **Повреждение памяти игры при разделении гибкого отображения:** правая часть указывала на
  страницы левой, и её освобождение обнуляло используемые страницы (Lucas-blip-png). Вероятная
  причина вылетов из-за чтения по недействительному указателю (`0x49476`, `0x48ac5` и другие).
- **Считывания из GPU во время боёв** (PR #349, tabnul): асинхронное считывание, данные которого
  изменил GPU, завершается частично и выполняется повторно без остановки командного процессора
  (рывки в боях с колоссами). Исправлены две ошибки, из-за которых в память игры могли
  записываться устаревшие данные.
- **Сверхширокие экраны (21:9, 32:9) и 16:10:** 3D-сцена снова отображается с правильными
  пропорциями (в 0.70 она растягивалась, #327, #331, #341).
- **Чёрные логотипы при запуске, уведомление о сохранении и тексты меню** при бюджете загрузки
  текстур из 0.70 (#341, #353): текстура ожидает не более 2 кадров, а бюджет сбрасывается после
  100 мс без кадра.
- **Кэш шейдеров:** файл новее пакета `cache.sotcpack` теперь имеет приоритет над копией из пакета
  (конвейеры отклонялись при каждом запуске); небольшие текстуры, изменённые процессором,
  проверяются целиком (цветные пятна) (PR #339, HiddenUserHere).
- **Видеокарты Intel:** драйвер аварийно завершался при записи кэша конвейеров во время их
  создания (#199, #211, #217); не более 4 одновременных компиляций; полные отражения в воде по
  умолчанию отключены, как на AMD (полосы на тенях и воде).
- **Видеокарты AMD:** нехватка видеопамяти при свободном бюджете (RX 9070 XT, #293) и недопустимые
  форматы дескрипторов больше не закрывают игру; недопустимая MSAA-текстура не прерывает работу
  транслятора шейдеров (#255). `SOTC_GCN_NV=1` (тест для RX 400/500) использует параметры шейдеров
  NVIDIA.
- **Генерация кадров FSR 3 теперь работает на официальной среде выполнения AMD**
  (`amd_fidelityfx_vk.dll` из FidelityFX SDK 1.1.4, FSR 3.1, входит в пакет) с собственной цепочкой
  буферов генерации кадров и темпом кадров, как в других играх; модуль FSR-Vulkan больше не
  используется. **При повороте камеры за ней тянулся тёмный прямоугольник, а изображение было
  мутным** (это также выглядело как мерцающее окно): у краёв экрана, где точка одного из двух кадров
  игры находится за краем, FSR 3 заполняет полосу шириной примерно в половину смещения кадра
  размытой оценкой. Порт восстанавливает эту полосу по кадру, в котором точка видна, смещённому на
  половину пути по векторам движения игры, и плавно совмещает её с изображением FSR 3. Клавиша F8
  теперь включает и выключает генерацию кадров в режиме, выбранном при запуске: FSR 3 и DLSS-G
  используют разные цепочки буферов, поэтому для переключения между ними требуется перезапуск. С
  FSR 3 среда выполнения NVIDIA Streamline не загружается. У быстрых объектов вблизи камеры (Агро,
  пробегающий мимо неё) в сгенерированных кадрах ещё могут быть видны тонкие контуры, как и с FSR 3
  в других играх при 30 FPS.
- **Игра могла зависнуть при загрузке сохранения** (за экраном загрузки, в тестах примерно при
  каждой шестой загрузке): поток отрисовки зацикливался на ошибке доступа к памяти, сравнивая
  таблицу, которую игра заполняет каждый кадр. Источник таблицы теперь читается через основное
  представление памяти, как и сама таблица.
- **Четырёхпоточные процессоры (i5-3470 и подобные): без генерации кадров игра не выходила из
  первого экрана загрузки** (upstream #267). Поток вывода кадров работал с обычным приоритетом,
  ниже повышенных потоков GPU, загрузки и звука; на 4 потоках и меньше он теперь тоже повышен.
- **Пропуск вступления:** щелчок мышью во время загрузки (чтобы вернуть окну фокус) больше не
  отменяет пропуск вступительного ролика.
- **Панель задач поверх игры в полноэкранном оконном режиме:** окно игры выходит на передний план
  после закрытия экрана загрузки шейдеров.
- **HDR:** с FSR 1 изображение было слишком тёмным; опорный белый — 200 нит (`SOTC_HDR_WHITE`).
- **Тихий звук** на стереовыходах (#214): 8-канальный звук сводится портом в стерео, фронтальные
  каналы — на полной громкости. Если вы подняли громкость выше 100% для компенсации, уменьшите её.
- **Автоматическое обновление:** сертификаты HTTPS проверяются через хранилище сертификатов
  Windows (`CERTIFICATE_VERIFY_FAILED`, #359).
- **`sotc.ini`:** выпуск содержит `sotc.ini.example`, который копируется в `sotc.ini`, только если
  последнего нет; распаковка новой версии больше не стирает настройки (#136).
- **Повреждённые файлы игры:** проверка охватывает файлы из папки обновления и американскую версию
  (#112). Распознаются DLC типа 0x1c (дополнительные лицензии) (#137).
- **Тихие вылеты при запуске:** журнал показывает фактический код завершения (ранее `0xffffffff`) и
  событие ошибки Windows; детектор зависаний записывает стеки потоков и до появления титульного
  экрана. `SotC.bat` также делает паузу при этих кодах.
- **Генерация кадров:** F8 переключает режим не чаще одного раза в 1,5 с (сбой драйвера NVIDIA);
  видеопамять FSR 1 и FSR 3 освобождается, пока они не используются (PR #308, Lukto02).
- Подсказка о FPS в лаунчере называет известные случаи: выше 30 FPS — бой с колоссом Avion; выше
  60 FPS вступительный ролик может закрыть игру (#100, #331).

**Новые параметры для диагностики:** `SOTC_POOL_FIX=0` (без исправления страничного аллокатора),
`SOTC_EXTRA_CVARS` и cvar в формате `NOME(ARGS)` (PR #345), `SOTC_SWAP_IMAGES` (глубина swapchain,
PR #352), `SOTC_DRIVER_CACHE_LOCK`, `SOTC_SLOW_BUILD_MS`, `SOTC_WIDE_ASPECT=0`,
`SOTC_THREAD_TIMES`.

### 1.1.3 — 2026-10-23 (форк Madmer)
Включает в форк версии 0.60.5 и 0.60.6 исходного проекта (см. ниже) и недостававшие открытые в нём
pull requests, а также исправления ещё открытых issues. Благодарим **Oussama268, JihedMrouki,
Lucas-blip-png, Anthonypc23 и AjncSystem**. Кэш шейдеров создаётся заново при первом запуске.

**Новое**
- **Поле зрения** (Графика > Общие): 100% (оригинал), 110%, 120% или 130%. Используется
  коэффициент «FOV Adjustment» собственной камеры игры, поэтому отсечение и тени учитывают более
  широкий обзор; параметр действует и в сюжетных сценах (#179).
- **DLAA** в NVIDIA DLSS: сглаживание DLSS в исходном разрешении (#34).
- **HDR (экспериментально)** (Графика > Экран): на мониторе HDR10 игра предлагает собственный
  режим HDR (#37).
- **Выход из игры** в меню игры (F1) (#210).
- Лаунчер предупреждает, если файлы игры неполные или повреждены (размер файлов `.psarc`
  CUSA08809), вместо того чтобы игра закрывалась при запуске (#30, #56).

**Исправления**
- **Игра навсегда зависала на экране загрузки** (наблюдалось один раз на процессоре с 4 потоками
  при выходе с титульного экрана): рабочий поток останавливался на первом чтении задачи, и игра
  его ожидала. Вероятная причина, теперь исправленная: страница, помеченная как записанная GPU, за
  которой не стояло ни одного байта GPU (диапазон без привязанной памяти удаляется из списка, а
  пометка страницы сохранялась), оставалась защищённой, и чтение или запись игры бесконечно
  вызывали одну и ту же ошибку страницы. Теперь такая страница освобождается после чтения.
  Детектор зависаний повторно записывает стеки потоков через 45 с, а журнал показывает
  освобождение такой страницы.
- **Привязка памяти, трижды отклонённая драйвером** (видеокарты с 2 ГБ): привязки без новой памяти
  (смена арены, освобождения) по-прежнему считались выполненными, и шейдеры читали там нули;
  теперь они сохраняются и отправляются повторно со следующей отправкой, раньше остальных.
  `SOTC_TEST_BIND_REFUSE_RETRY=1` имитирует этот случай для тестирования.
- **Текстуры «Максимум»:** защита из 1.1.1 от превышения бюджета видеопамяти (текстуры на один или
  два уровня грубее при 150% / 190%) действовала только на «Высоком»; теперь она действует и на
  «Максимуме», где отключается лишь подстройка по пулу страниц игры.
- **Пропуск интро:** 3D-звук игры (Audio3d) также отключается под экраном загрузки, а громкость из
  лаунчера (`SOTC_VOLUME`) теперь применяется и к нему.
- **Автоматическое обновление:** второй экземпляр SotC.exe, открытый во время обновления, удалял
  резервную копию заменённых файлов; теперь он не затрагивает папку `update`, а прерванное
  обновление откатывается при следующем запуске.
- **Графика Intel HD/UHD со старым драйвером** (6–11-е поколения, версии 101.2xxx и более ранние):
  динамическое состояние ввода вершин отключено, поскольку этот драйвер выводил синие и розовые
  артефакты (#20). Без него конвейеры компилируются в командном процессоре (рывки при первом
  появлении эффекта), а кэш шейдеров хранится в отдельной папке (`_novid`). Iris Xe, Arc и
  Core Ultra используют новый драйвер, и для них ничего не меняется.
  `SOTC_VERTEX_INPUT_DYNAMIC=0/1` задаёт режим принудительно.
- **F7 и настройка FPS в меню игры** меняют целевой FPS игры и в том случае, если она запускается
  в режиме 30 FPS (#44, #49).
- **Упреждающее чтение файлов (HDD)** использует не более четверти ОЗУ, от меньших файлов к
  большим; файлы, которые не помещаются, пропускаются и указываются в журнале (#264).
- **Процессоры с 4 потоками:** рабочие потоки игры получают приоритет ниже обычного, а потоки
  чтения файлов — выше обычного, как в исправлении порта God of War III для тех же процессоров
  (#267; `SOTC_JOB_PRIORITY=0` отключает).
- **AMD GCN (RX 400/500):** преобразование шейдеров в 32 линии стало необязательным
  (`SOTC_AMD_WAVE32=1`): эти видеокарты работают только с 64 линиями, и драйвер мог аварийно
  завершаться.
- **Возврат неиспользуемых буферов** (PR #238, Oussama268): командный процессор больше не ожидает
  копирования для считывания; возврат завершается в одном из следующих кадров, когда GPU уже
  догнал.
- **PR #258** (JihedMrouki): на видеокартах без встроенной поддержки барицентрических координат
  (GTX 10xx) положение вершин инвариантно, и предварительный проход глубины совпадает с проходом
  цвета; командный процессор пропускает пустые очереди, не блокируя их.
- **Эксклюзивный полноэкранный режим:** если частота рабочего стола не кратна 60 Гц (75 Гц,
  144 Гц…), используется наибольшая кратная 60 Гц частота монитора в том же разрешении, и 30 FPS
  больше не чередуются между 2 и 3 обновлениями экрана (PR #243, Anthonypc23); `BB_FULLSCREEN_HZ`
  задаёт другую частоту принудительно.
- Меню игры (F1) и журнал событий, включая сообщения загрузчика, переведены на испанский, русский,
  китайский, японский и корейский языки.

### 1.1.2 — 2026-10-22 (форк Madmer)
- **Пропуск интро** теперь предлагает выбор: *Отключено* (по умолчанию), *Только заставки*
  (`skipSplash=1` движка: логотипы при запуске) или *Заставки и вступительный ролик*. В 1.1.1
  параметр использовал `+nointro`, который пропускал также титульный экран и выбор профиля: новая
  игра начиналась сразу, а меню предлагало только *Новую игру* (сохранения не затрагивались). В
  движке нет параметра для пропуска только вступительного ролика: при существующем профиле экран
  загрузки порта закрывает игру, а звук отключён до титульного экрана, и ролик пропускается под
  ним. Экран «Нажмите любую кнопку» и меню с пунктом *Продолжить* сохраняются.

### 1.1.1 — 2026-10-22 (форк Madmer)
- **Бесконечная загрузка на видеокартах с 2 ГБ** (GTX 1050) — при запуске с отключёнными
  текстурами высокого качества и посреди игры в открытом мире:
  - сборщик кэша текстур ничего не освобождал на видеокартах с малым объёмом памяти: изображения,
    записанные GPU, которые он не может освободить, оставались в начале очереди и расходовали
    каждый проход («GC freed 0»); видеопамять доходила до 3,4 ГБ на видеокарте с 2 ГБ. Теперь они
    перемещаются в конец очереди;
  - привязка памяти, отклонённая драйвером, считалась выполненной, и шейдеры навсегда читали там
    нули: игра ожидала данные, которые так и не поступали. Теперь она отменяется и выполняется
    заново при следующем использовании, а неиспользуемые блоки буферов возвращаются, чтобы
    освободить место;
  - с текстурами высокого качества превышение бюджета видеопамяти (150%/190%) также заставляет
    текстуры запрашивать более грубые MIP-уровни, а не только пул страниц игры.
- Журнал (`user/last_run.log`) раз в минуту получает данные о видеопамяти, изображениях, сборщике и
  ошибках выделения памяти.
- **Лаунчер:** параметр *Пропуск интро* (собственный `+nointro` движка, по умолчанию отключён) и
  полоса прокрутки на главной странице (быстрые настройки обрезались на небольших экранах).

### 1.1.0 — 2026-10-22 (форк Madmer)
Включает в форк всё с 0.58 по 0.60.4 исходного проекта и открытые в нём pull requests, а также
исправления ещё открытых issues, NVIDIA DLSS и оптимизации. Благодарим **Lucas-blip-png,
Oussama268, DUDUYG, jownatan и AjncSystem**, чьи коммиты включены с сохранением авторства.

**Слабые видеокарты, память и другие исправления**
- **Пропадающие звуки на процессорах с 4 потоками** (i5-7300HQ и аналогичные): поток звуковой
  системы игры, запускающий эффекты (стук копыт Агро, колоссы), оказывался позади рабочих потоков
  игры и потоков GPU, из-за чего эффекты, а иногда и музыка терялись. Теперь звуковые потоки имеют
  приоритет над ними.
- **Разрешение рендеринга 720p** покрывало сцену графическими артефактами на всех видеокартах, в
  том числе в режиме DLSS «Качество»: ширина рендеринга от 1153 до 1280 даёт целям рендеринга шаг
  строки, который порт эмулирует неправильно. Теперь этот режим рендерит в 1296 × 729 с тем же
  изображением.
- **Игра закрывалась через несколько минут при нехватке виртуальной памяти** (`0x1a371`; ноутбук с
  GTX 1050): при небольшом файле подкачки Windows отказывала в выделении 9152 МиБ памяти игры, и
  порт сразу переходил к 5056 МиБ консоли, которые порт исчерпывает. Теперь игра получает
  наибольший объём памяти, который Windows ещё может выделить, оставляя 2,5 ГБ остальному процессу.
  Лаунчер предупреждает, если свободной виртуальной памяти недостаточно (рекомендуется файл подкачки
  16 ГБ). `BB_DMEM_COMMIT_MB=<MiB>` имитирует меньший лимит для тестирования.
- **Игра намертво зависала на видеокартах с 2 ГБ** (GTX 1050, 2-й колосс): при нехватке видео- и
  системной памяти драйвер отклонял привязку памяти буферов, и семафор, которого ожидала следующая
  отправка, так и не получал сигнал. Теперь привязка повторяется после предыдущих, а в случае
  неудачи семафор сигнализируется процессором: игра продолжается (диапазоны без памяти читаются как
  нули).
- Текстура, вытесненная из заполненной видеопамяти, записывает обратно в память игры только те
  байты, которые игра не изменила с момента запроса.
- **Светлый квадрат вокруг значков кнопок** (Xbox, клавиатура и мышь) в подсказках: игра смешивает
  интерфейс с предварительно умноженной альфой, и серая кайма за пределами круглых значков
  прибавлялась к изображению. Теперь значки записываются с цветом, предварительно умноженным на
  покрытие.
- **Ход распаковки пакетов (.pkg)** в лаунчере: индикатор в первую же секунду подскакивал до 100% и
  оставался там до конца (PkgTool создаёт каждый файл сразу с итоговым размером). Теперь он
  отслеживает байты, фактически записанные PkgTool.
- **Зависания:** если в течение 60 с от игры не поступает ни одного кадра, журнал
  (`user/last_run.log`) получает стеки всех потоков для поиска причины.

**Исходный проект и pull requests**
- **Версии с 0.58 по 0.60.4 проекта AjncSystem/ShadowOfTheColossus-PC** (поток отрисовки,
  трансляция шейдеров и вычислительные конвейеры во вспомогательных потоках, генерация кадров
  DLSS-G и FSR 3, меню игры, текстуры на Pascal, видеопамять, громкость, автоматическое обновление
  и остальное из примечаний ниже) вместе с собственными возможностями форка (значки кнопок
  используемого устройства, NGS2/Audio3d, оконные режимы, языки лаунчера, установщик). Лаунчер
  проверяет наличие обновлений в выпусках этого форка.
- **Шейдеры после обновления драйвера:** предварительная загрузка кэша собирает конвейеры во
  вспомогательных потоках (PR #215, Oussama268: экран загрузки сократился со 107 с до 6,5 с на
  RTX 3060).
- **AMD GCN (RX 470–590, Vega):** барьеры общей памяти (PR #205) и преобразование в 32 линии по
  умолчанию, устранившее бело-зелёное мерцание на RX 570 (#152, #187, #201; `SOTC_AMD_WAVE32=0`
  отключает), от Lucas-blip-png.
- **Процессоры с 4–8 потоками:** поток записи команд больше не находится в активном ожидании
  (PR #198, Lucas-blip-png).
- **Процессоры без BMI1/LZCNT/MOVBE** (Ivy Bridge и более ранние, #38, #40, #70, #88): эти
  инструкции эмулируются, вместо того чтобы игра не запускалась (Oussama268, Lucas-blip-png).
  `bb-probe.exe` этого форка скомпилирован для x86-64-v3 (AVX2), которого у этих процессоров нет:
  пакет также содержит `bb-probe-compat.exe` (x86-64-v2), который `sotc.py` автоматически
  запускает на них (`SOTC_PROBE_COMPAT=1` включает принудительно).
- Считывания из GPU, попадающие в окно, которое уже считывается, ожидают его завершения (PR #69,
  DUDUYG; `SOTC_RB_JOIN=0` отключает); таймер высокого разрешения при подаче кадров; точный ключ в
  кэше форматов изображений; отдельный кэш шейдеров без однородных ветвлений wave64 (PR #93);
  обновлён `tools/SotC-Shaders.ps1` (PR #81); понятное сообщение для файлов, которые ещё
  зашифрованы (PR #212); лаунчер продолжает работу при отклонённом стандартном выводе (PR #207).

**Рывки (stutter)**
- **Шейдеры транслировались заново при каждом запуске:** двоичные файлы шейдеров сохранялись
  только по хешу программы; файлы от другой версии транслятора (предыдущей версии или старого
  пакета шейдеров) занимали имена, а файлы этой версии так и не сохранялись и транслировались
  заново прямо во время игры в каждом сеансе. Теперь кэш учитывает версию двоичных файлов, а кэш
  другой версии однократно переносится в сторону.
- **Пакет шейдеров от другого профиля** копировался заново при каждом запуске (19 с) и сразу
  отбрасывался вместе с кэшем, собранным во время игры; теперь он копируется только один раз.
- **Кэш драйвера** (VkPipelineCache) сохраняется вместе с кэшем шейдеров: конвейеры,
  скомпилированные драйвером, при следующем запуске загружаются готовыми, даже если кэш драйвера
  NVIDIA заполнен.

**NVIDIA DLSS**
- **DLSS Super Resolution** (видеокарты GeForce RTX): в режимах разрешения рендеринга ниже 1080p
  сцена игры масштабируется с помощью DLSS вместо собственного билинейного растяжения игры, с
  использованием глубины, векторов движения и камеры игры; интерфейс по-прежнему отрисовывается
  игрой в 1080p. Режимы «Качество» (720p), «Баланс» (630p), «Производительность» (540p) и «Ультра
  производительность» (360p) (`SOTC_DLSS`). Можно включать и отключать в меню игры. Официальная
  среда выполнения NVIDIA Streamline 2.14.16 входит в пакет.
- **Чёткий интерфейс при генерации кадров:** сгенерированные кадры получают сцену без интерфейса,
  поверх которой накладывается интерфейс реального кадра (FSR 3.1); тексты и полосы больше не
  смазываются при движении, а экраны загрузки и логотип на титульном экране выводятся чисто, в том
  числе при выходном разрешении выше 1080p (FSR 1). Интерфейс компонуется с учётом прозрачности
  собственного слоя интерфейса игры. DLSS-G получает изображение без интерфейса
  (`SOTC_FG_HUDLESS=0` отключает). Множитель кадров DLSS-G x3 и x4 на GeForce RTX 50.
- **Генерация кадров только там, где она работает:** DLSS-G доступна для RTX 40/50, FSR 3 — для
  остальных видеокарт. Значение DLSS-G, сохранённое на видеокарте без её поддержки, сменяется на
  автоматическое (FSR 3).

**Лаунчер, меню и управление**
- **Меню игры** (Insert / F1 / L3+R3) и экранные тексты порта на 7 языках лаунчера; для
  китайского, японского и корейского используется шрифт из состава Windows. Удалены параметры,
  которые в этом порте ни на что не влияли (выбор апскейлера, пресет, выходное разрешение и
  резкость). DLSS и полноэкранный режим (F11) переключаются сразу; эффекты игры (виньетка, туман,
  SSAO, полные отражения в воде) сохраняются в настройках лаунчера и применяются при следующем
  запуске игры.
- **Вибрация контроллера** от 0 до 100% (#210).
- **Кнопки мыши** также можно переназначать на экране управления: щёлкните действие и нажмите
  кнопку (#192).
- **Обновление игры** также обнаруживается в папке `update` или `patch` внутри папки игры или рядом
  с ней, если `param.sfo` относится к обновлению игры (#66).

**Исправления**
- **Исчерпание видеопамяти** (видеокарты с 4–6 ГБ, #12, #62, #137): сборка текстур углубляется на
  три уровня (без блокировки потока, удерживающего кэш текстур), а запрос разбивается на более
  мелкие блоки; при полном отсутствии памяти участок остаётся без памяти до следующей попытки,
  вместо того чтобы закрывать игру.
- **Игра закрывалась при запуске, если Windows отказывала в выделении 9152 МБ** (#194), и
  **генерация кадров на ПК с AMD и NVIDIA** (#194), от Lucas-blip-png; **FSR 1 без синхронизации
  на AMD** (#209).
- **Кэш шейдеров с повторяющимися ключами** закрывал игру при каждом запуске, пока его не удаляли
  (#88).
- **Полосы интерфейса заполнены или чёрные в 2560x1080** (#202).
- **Игровые часы:** частота TSC измеряется парными замерами в течение 100 мс (рассинхронизация
  звука и изображения в длинных сюжетных сценах, #91).
- **Отсутствие звука, если устройство вывода ещё не существует при запуске** (Bluetooth-наушники,
  #214): порт повторяет попытку каждую секунду.
- **Сохранение в меню игры стирало из `sotc.ini`** назначения клавиш, переназначения контроллера и
  настройки `sotc.py`; теперь перезаписываются только строки меню, а `sotc.ini` и настройки
  лаунчера записываются через новый файл, заменяющий старый (прерванная запись ничего не теряет).
- **Исправления кода для процессоров Intel:** при эмуляции BMI из исходного проекта таблица функций
  игры терялась при чтении таблиц модулей, а короткие инструкции SSE4a и функции, использующие
  область ниже указателя стека, оставались без исправления; теперь эти исправления возвращены.
  Инструкция, исправляемая во время игры, больше не блокирует обработчик исключений.
- **GTX 10 (Pascal):** шейдеры, прочитанные из кэша, не содержали входов пиксельного шейдера, и
  конвейеры, собранные позже с ними, оставались без геометрического шейдера барицентрических
  координат (неправильное изображение); теперь кэш сохраняет эти данные.
- **Финал игры (#146):** защита списков свободных блоков куч, созданных миром, снова отслеживает
  каждое их изменение.
- **Исчерпание видеопамяти:** после полного отказа игра больше не повторяет сборку текстур с
  ожиданием GPU при каждой отрисовке (что приводило к зависанию) и повторяет попытку через 250 мс.
- **DLSS:** при сбое обработки сцена отображается растянутой, как в самой игре, вместо
  неопределённого изображения, а после 8 сбоев подряд DLSS отключается (меню показывает причину);
  смены ракурса камеры снова очищают историю DLSS.
- **Кэш драйвера** сохраняется и при закрытии окна игры.
- **ПК с видеокартами разных производителей** (ноутбук Intel + NVIDIA, APU + GeForce) или с
  `BB_GPU_ID`: генерация кадров и DLSS ориентируются на видеокарту, которую будет использовать игра.
- **Переход в полноэкранный режим через меню** мог приводить к зависанию игры; F11 работает при
  открытом меню; управление камерой мышью включается и отключается в меню сразу.

**Производительность**
- **GTX 10 (Pascal):** геометрический шейдер, передающий барицентрические координаты, пропускает
  вершины дальше без копирования (VK_NV_geometry_shader_passthrough) на основе реализации из
  PR #169 (`SOTC_GS_PASSTHROUGH=0` отключает).
- Таблицы, которые игра реплицирует каждый кадр, сравниваются в потоках копирования.
- Исполняемый файл скомпилирован с оптимизацией по профилю (PGO), обученной на демонстрационном
  ролике титульного экрана.

Кэш шейдеров создаётся заново при первом запуске.

### 0.70 — 2026-10-29
> ⚠️ **Внимание: устанавливайте игру на SSD (желательно NVMe).** Каждая новая область считывает
> сотни МБ текстур и моделей из папки игры; на HDD смена областей сопровождается гораздо более
> сильными подвисаниями, а игра может замирать на несколько секунд. Разместите папку игры (дамп)
> и по возможности папку порта на SSD.

- **Смена областей без падения FPS почти до нуля.** Раньше каждая смена области обрушивала FPS и
  иногда вызывала принудительные экраны загрузки на 1–3 с; худший кадр превышал полсекунды
  (534 мс). Теперь худший кадр составляет 58–75 мс, принудительных экранов загрузки нет, а финал
  игры проходится до титров. При каждой смене всё ещё теряется 1–6 кадров: это следующая задача.
  Причинами были блокировка таблицы памяти игры, копирования памяти с переотображением гигабайтов,
  повторный поиск регионов GPU при каждой записи, многократный (тысячи раз) обход одного и того же
  участка защитой памяти игры и большое число новых текстур в одном кадре. Теперь за кадр
  загружается не более 32 МБ крупных новых текстур, а те, что переносятся на следующий кадр, на
  мгновение отображаются чёрными. Для каждого исправления предусмотрен переключатель отключения
  (`SOTC_LOCK_WAKE=0`, `SOTC_VIEW_CHUNK_MB=0`, `SOTC_REGION_LOG=0`, `SOTC_REGION_FAST=0`,
  `SOTC_PROTECT_FAST=0`, `SOTC_WRITE_FAST=0`, `SOTC_BATCH_SPLIT=0`, `SOTC_GUARD_DEDUP=0`,
  `SOTC_UPLOAD_BUDGET_MB=0`).
- **Редкий вылет в аллокаторе игры после загрузки** (`0x49800`): два упрощения в защите памяти
  игры могли позволить GPU записать данные в блок, который игра только что освободила. Теперь они
  отключены по умолчанию (`SOTC_GUARD_STALE=1` и `SOTC_GUARD_USER_POINTER=1` включают их). Сессия
  длительностью 36 мин с 6 загрузками прошла без вылетов; если вылет всё же произойдёт, пришлите,
  пожалуйста, лог.
- **DLSS в режиме PS4 Pro при 30 FPS без мерцания изображения:** повторяемый кадр показывал
  изображение без DLSS.
- **Генерация кадров (F8) с FSR 1: HUD больше не дрожит.**
- **LOD feedback** (`SOTC_LOD_FEEDBACK=1`, экспериментально) больше не отнимает ~7 FPS.
- Диагностика: `SOTC_DRAW_SPLIT=1` (при включённом счётчике FPS на экране) записывает в лог,
  на что расходуется время потока отрисовки.

### 0.60.6-C3 — 2026-10-27
- **Полоса выносливости с DLSS (исправление C2):** область полосы теперь использует ширину экрана
  в 1080 строк с соотношением сторон вывода (1080 × Ш/В) вместо ширины рендеринга. С DLSS
  разрешение рендеринга меньше (1704x720 при 2560x1080), и жёлтая полоса снова останавливалась
  далеко от круга; при 16:9 с DLSS вне режима PS4 Pro версия C2 также её ломала.
- **Ограничение 30 FPS:** значение «30» (меню F1 или F7) удерживает 30 FPS, даже когда игра
  запрашивает работу без ограничения (режим PS4 Pro); раньше частота держалась на уровне ~42.

### 0.60.6-C2 — 2026-10-26
- **Широкий экран (21:9, 32:9): корректные полосы здоровья и выносливости (#301, #202).** У полосы
  здоровья был лишний серый фрагмент, а жёлтая полоса выносливости останавливалась далеко от круга
  и заполнялась в обратную сторону. Соотношение сторон экранного объекта игры остаётся 16:9 (игра
  рисует по нему полосу здоровья), а два прямоугольника интерфейса шириной 1920 теперь используют
  ширину экрана.
- **Вибрацию контроллера можно отключить (#292, Lucas-blip-png):** в лаунчере (раздел
  «Управление») и в меню F1 (раздел «Контроллер»), изменение применяется сразу.
- **Более высокий FPS в Святилище (Lucas-blip-png):** барьер для storage-изображений GENERAL
  выполняется один раз на fence, а не перед каждой отрисовкой (каждый такой барьер завершал проход
  рендеринга, ~20 тыс. раз в секунду), а буфер глубины, читаемый во время записи трафарета,
  остаётся в одном layout. `SOTC_GENERAL_SYNC=0` / `SOTC_DEPTH_RO=0` отключают это.
- **Генерация кадров без интерфейса (опция от Lucas-blip-png, экспериментально):** «Изображение и
  FPS» → «Генерация кадров без интерфейса». Текст и полосы больше не дрожат с FSR 3 (работает и в
  режиме PS4 Pro). Логотип на титульном экране может выглядеть блёклым.
- **DLSS «Ультрапроизводительность» (3×) (#306)**; DLSS переходит на масштабирование 2× + FSR 1
  только тогда, когда ни один режим не поддерживает нужный масштаб (режим Pro в 4K).
- Для тестирования: `SOTC_RENDER_KEEP`, `SOTC_RENDER_EXTRA` и `SOTC_WIDE_ASPECT` (подбор значений
  размера экрана игры).

### 0.60.6-C1 — 2026-10-26
Версия от сообщества: исправления и функции от tabnul, Oussama268, NathanMBR и JihedMrouki,
протестированные с текстурами **«Максимальные» + режим PS4 Pro + 60 FPS** (RTX 2060 SUPER).

- **Исправление страничного аллокатора игры (tabnul, PR #288 / fork #112):** счётчик свободных
  страниц игры расходился с реальной картой, и игра выполняла чтение до начала карты (вылет
  `0x1a371`). С этим исправлением и резервом памяти текстур, равным 0 (в том числе в режиме
  PS4 Pro), стример больше не сбрасывает все текстуры до уровня 3 каждую секунду. Результат:
  **час игры в режиме «Максимальные» + Pro без вылетов**, а почти весь цветной мусор после
  загрузок исчез.
- **Текстуры «Максимальные» в режиме PS4 Pro (экспериментально):** снова доступны (лаунчер
  заменял их на «Высокие»). `SOTC_MAX_IN_PRO=0` возвращает прежнее поведение.
- **NVIDIA DLSS (RTX 20 и новее), от tabnul (fork PR #81):** игра рендерится в меньшем разрешении,
  а DLSS масштабирует изображение; интерфейс рисуется в выходном разрешении, TAA игры отключён,
  виньетка накладывается после масштабирования. Новое в этой версии: **работает в режиме PS4 Pro**
  (в финальной композиции Pro сцена и интерфейс расположены в другом порядке и имеют другой
  размер) и **в 4K** (DLSS масштабирует в 2x, а FSR 1 выполняет остальное; раньше экран
  становился чёрным). В меню F1 апскейлер отображается как «NVIDIA DLSS» и переключается только в
  лаунчере (раньше поверх записывалось значение «fsr1»). Файлы `sl.dlss.dll` и `nvngx_dlss.dll`
  (NVIDIA Streamline 2.14.16) включены в поставку.
- **Трава (tabnul):** дальность, высота и детализация травы и кустарника меняются сразу в меню F1;
  тени и затухание травы настраиваются в лаунчере; буферы травы увеличены в 4 раза (дальняя трава
  больше не пропадает полосами).
- **Более высокий FPS: быстрое чтение данных GPU (экспериментально),** новая опция в «Быстрых
  настройках», с финальной проверкой и постраничной блокировкой от Oussama268 (`SOTC_RB_HOT=1`).
  По умолчанию отключена.
- **Повторное использование привязок** снова включено по умолчанию (Oussama268; исправление
  песчаной поверхности проверено). Также от Oussama268: обработчик команд не ожидает пустых
  очередей, а неиспользуемая память арен возвращается без ожидания GPU.
- **Американская версия CUSA08034 (NathanMBR, PR #270)** — подробности ниже.
- **Слабые CPU и GTX 10xx (JihedMrouki, PR #258):** инвариантная позиция в пайплайнах с эмуляцией
  барицентрических координат (расхождение текстур/глубины на земле), и кэш шейдеров больше не
  пытается читать с диска несуществующие файлы. Кэш шейдеров создаётся заново при первом запуске.
- **F7** переключает **30 → 60 → без ограничения** (раньше клавиша только включала и отключала
  ограничение 30 FPS).
- **Вкладка «Отладка» в лаунчере:** отладочное меню Bluepoint (**F6 или Start+Select** на
  контроллере; один Select по-прежнему открывает карту), **пропуск вступительных логотипов**,
  **переход сразу в игру** (`+nointro`, также пропускает экран Start), монитор памяти текстур, F5
  (захват текстур кадра, не более 1 ГБ) и F9 (перезагрузка текстур; при включённой опции
  «Сравнить DLSS» переключает DLSS / FSR 1).
- Меню F1: тени и затухание травы показывают значение игры, пока они ещё не сохранены.

#### Американская версия (CUSA08034), NathanMBR
- **Американский дамп CUSA08034 (1.00, единственная американская версия) поддерживается.**
  Исполняемый файл совпадает с европейской версией 1.01 (код и данные идентичны побайтно; файлы
  `.psarc` тоже), поэтому все исправления, режимы патчей (60 FPS, фильтрация 16x, размер
  рендеринга) и защита red zone действуют для обеих версий. Различаются только логотипы и
  юридические экраны (игра определяет регион по `gameversion.txt`). Протестировано до второго
  колосса.
- Исправления GPU, ранее применявшиеся только к CUSA08809 (барьеры LDS, wave64, таблицы T#, ранние
  тесты глубины для декалей, периодический flush), теперь действуют для обоих серийных номеров
  (`IsSotc()`). Без ранних тестов декали за объектом закрашивали поверх него весь свой
  прямоугольник (края и текстуры фона оказывались впереди). В логе отображается
  `SOTC early fragment tests: on`.
- **Отдельные сохранения для каждой версии** (`user/savedata/1/CUSA08034`) и собственный кэш
  шейдеров (`user/cache/CUSA08034`). **DLC только того же региона** (папка `addcont/CUSA08034`,
  content ID `UP9000-CUSA08034_00-...`).
- **Лаунчер:** находит американский дамп по `TITLE_ID` из `param.sfo` при любом имени папки
  (например, `CUSA08034-app`), показывает «✓ CUSA08034 версия 01.00: доступны все режимы» и больше
  не помечает дамп как неподдерживаемый. При наличии обоих дампов первым идёт европейский.
- Инструменты (`tools/`) и шаблон сохранения для бенчмарков принимают оба серийных номера. Новые
  тесты (`tests/test_games.py`) завершаются ошибкой, если какой-либо код снова начнёт проверять
  только один серийный номер.
- В американской версии нет 3 строк на арабском языке из европейского обновления 1.01.

### 0.60.6 — 2026-10-22
- **Устранены радужный шум на текстурах и вылеты «на ровном месте» (`0x49500`).** Причиной было
  «горячее» чтение данных GPU (`SOTC_RB_HOT`): вместе с каждым чтением порт спекулятивно записывал
  в память игры ранее прочитанные регионы; после загрузки сохранения или новой области игра уже
  успевала использовать эту память под новые текстуры и свои кучи, и устаревшие данные GPU
  записывались поверх. Теперь эта функция отключена (средний FPS в демо снижается примерно на 4%).
  Проблема найдена по сценарию воспроизведения от пользователя: новая область → подождать → выйти
  → загрузить сохранение заново.
- **Чёрные точки сеткой на песчаной поверхности** (0.60.5): оптимизация привязок из PR #232
  отключена по умолчанию (`SOTC_FAST_BIND=1` включает её; исправление ожидает тестирования).
- **PR #219 (утечка памяти) отключён по умолчанию**: с ним цветной шум и гигантские чёрные
  полигоны («взрыв вершин») появлялись гораздо чаще (`SOTC_CP_FREE_TASKS=1` включает его).
- **Меню игры (F1):** только то, что работает в SotC (унаследованные от Bloodborne параметры
  апскейлера, пресета, разрешения и эффектов ни на что не влияли); **FPS (30/60/без ограничения),
  апскейлер и резкость** меняются сразу; текстуры **«Максимальные»** отключены в режиме PS4 Pro
  (игра вылетала через несколько минут); подсказка поясняет, какие параметры применяются сразу.
- **Жёсткий диск:** после загрузки игры её файлы (13,7 ГБ) однократно последовательно считываются
  и остаются в ОЗУ: новые локации больше не подвисают в ожидании HDD (`SOTC_PREFETCH=0` отключает
  это).
- **Загрузки:** при каждой загрузке сохранения или новой области неиспользуемые текстуры
  выгружаются (`SOTC_LOAD_FLUSH=0` отключает это).
- **Журнал событий `user/events.log`:** подробная запись конфигурации, системы (CPU, ОЗУ,
  HDD/SSD), меню, клавиш, контроллера, загрузок, текстур, производительности по минутам, ошибок с
  пояснениями; язык лаунчера; 5 последних сессий и копия в `user/crashes` после вылета.
- PR: #215 (предзагрузка пайплайнов в рабочих потоках: новый драйвер 106 с → 7 с), #198 (CPU с 4
  потоками), #205 (барьеры LDS на AMD GCN), #207, #212.
- Ревизия кода: защита куч мира, сторожевой таймер зависаний, память на ПК с малым объёмом ОЗУ,
  сохранение значения «Без ограничения» из лаунчера, полупрозрачный экран загрузки при коротких
  подвисаниях.

### 0.60.5 — 2026-10-21
- **Выбор качества текстур: «Отключено» / «Высокие» / «Максимальные»** (#235), в лаунчере
  («Быстрые настройки» → «Текстуры») и в меню игры (меняется сразу). **«Высокие» (стабильно)**,
  значение по умолчанию, сохраняет защиту из 0.60.3/0.60.4 (текстуры на один уровень грубее, когда
  память текстур игры заканчивается); поскольку в больших областях эта память почти всегда
  заполнена, версия 0.60.4 давала менее чёткое изображение, чем 0.60.3. **«Максимальные»
  (чётко)** снимают защиту: всегда используется самый детальный уровень, но **игра может вылетать
  на очень широких планах** (меч, поднятый к солнцу, Святилище, область последнего колосса;
  `0x1a371`, #186), особенно в режиме PS4 Pro; в этом случае вернитесь к «Высоким».
  **«Отключено»** — только версии текстур игры 256×256. Прежний флажок «Текстуры высокого
  качества» преобразуется в «Высокие» (или «Отключено»).
- **Утечка памяти** (~60 МБ в минуту, более 7 ГБ за полчаса; подвисания в долгих сессиях): каждый
  вложенный буфер команд оставлял в памяти свою корутину (PR #219, Oussama268).
- **Более высокий FPS:** дескрипторы предыдущей отрисовки используются повторно, если следующая
  отрисовка использует тот же пайплайн и те же ресурсы (PR #232, Tadomikaari312-Hy): демо +3,3% по
  среднему, +12% по минимальному значению (72,8 / 37,1 → 75,2 / 41,7). `SOTC_FAST_BIND=0`
  отключает это.
- **Загрузка после обновления драйвера видеокарты: 106 с → 7 с** (сохранённые пайплайны
  собираются вспомогательными потоками; PR #215, Oussama268). `SOTC_ASYNC_PRELOAD=0` возвращает
  прежнее поведение.
- **AMD GCN** (RX 470–590, Vega, APU): барьеры разделяемой памяти, которые эти видеокарты не
  получали, — вероятное исправление белых персонажей и дыма (PR #205, Lucas-blip-png; проверка на
  видеокартах GCN силами игроков). `SOTC_AMD64_LDS=0` отключает это.
- **CPU с 4 потоками:** запись команд и обработчик команд больше не занимают целое ядро в
  ожидании (зависание на загрузке → 73 FPS; PR #198, Lucas-blip-png).
- **Лаунчер:** больше не останавливается, когда консоль отказывает в записи (#104, PR #207); для
  дампа или модулей, которые всё ещё зашифрованы, указывается, о каком файле идёт речь (PR #212).
- **Экран загрузки:** при коротких подвисаниях в игре изображение лишь затемняется; экран
  становится непрозрачным через 3 с без кадра (раньше при зависаниях на 1 с он мигал чёрным).
- **Сторожевой таймер зависаний:** через 15 с без кадра игры стеки потоков записываются в лог
  (`SOTC_WATCHDOG=0` отключает это) — для поиска причин случаев «закрылась на ровном месте».
- **Ревизия кода:** защита куч мира пропускала охранную проверку на последующих фрагментах кучи и
  оставляла новые фрагменты без защиты на срок до 2 с; ПК с малым объёмом ОЗУ получали размер
  памяти до его уменьшения; кнопка 60 FPS в меню заменяла «Без ограничения» на «60».
- Меню игры и горячие клавиши (F1, F7, F8, F10, F11) описаны в README, с иллюстрацией.

### 0.60.4 — 2026-10-20
- **Голубые блоки на камнях и мерцающие цветы** (с опцией «Текстуры высокого качества»):
  предотвращающая вылет корректировка из 0.60.3 меняла уровень детализации текстур каждые одну-две
  секунды в больших областях (4, 3, 2, 3, 2…), и при каждой смене игра выгружала или заново
  загружала текстуры, которые в этот момент отрисовывались. Теперь детализация снижается только
  при свободной памяти менее 8% и повышается снова только при более чем 16%. Протестировано от
  «Новой игры+» до 2-го колосса: 3 смены вместо ~8, без голубых блоков, без мерцающих цветов и без
  вылетов.

### 0.60.3 — 2026-10-20
**Игру можно пройти от начала до конца.** Пройдено от сохранения перед последним колоссом через
битву с Малусом, финальные сцены и титры с включёнными опциями «Текстуры высокого качества»,
режимом PS4 Pro и 60 FPS. Благодарим **Lucas-blip-png**.

- **Вылет с «Текстурами высокого качества» на широких планах** (меч, поднятый к солнцу, область
  последнего колосса; `0x1a371`, #186): когда память текстур игры начинает заканчиваться, видимые
  текстуры запрашивают уровень детализации на один ниже (на два — когда память почти заполнена), и
  игра освобождает место до того, как зависнуть. Изображение остаётся чётче, чем с отключённой
  опцией (PR #191, Lucas-blip-png).
- **Вылет при загрузке после титров** (`0x49500`): защита памяти игры не видела пулы, создаваемые
  миром, поскольку они растут разрозненными фрагментами по 1 МБ; теперь каждый из них защищается
  там, где находятся его свободные блоки (#146).
- **+2,7% FPS в храме:** чтение данных GPU окнами по 64 КБ (PR #196, Lucas-blip-png).

### 0.60.2 — 2026-10-20
- **Быстрые настройки прямо в игре:** в меню (Insert, F1 или L3+R3), не выходя из игры: 60 FPS,
  генерация кадров (отключено / DLSS-G / FSR 3), текстуры высокого качества, повторное
  использование дальних теней и громкость. Изменения применяются сразу и сохраняются в лаунчере
  для следующего запуска. Анизотропная фильтрация 16x, режим PS4 Pro, разрешение и использование
  видеопамяти по-прежнему настраиваются в лаунчере (требуют перезапуска).
- **Мышь в меню:** при открытом меню курсор мыши освобождается и становится видимым для нажатий;
  после закрытия меню мышь снова управляет камерой.
- **Анимированный экран загрузки и во время сборки шейдеров** при запуске игры (раньше окно
  замирало, иногда на несколько минут после обновления драйвера), с непрозрачным чёрным фоном и
  анимацией по центру (вращающийся круг, пульсирующий заголовок, «Загрузка…»).
- Заголовок меню и быстрые настройки отображаются на языке лаунчера.

### 0.60.1 — 2026-10-20
Благодарим **nosekefik, Oussama268, plassardewen-cpu и lunar-me**.

- **Меню в игре** (Insert, F1 или L3+R3): FPS, видеокарта и видеопамять, полноэкранный режим,
  разрешение, масштабирование и резкость, камера и мышь, эффекты (туман, виньетка, затенение,
  отражения на воде). Это было меню Bloodborne, скрытое в порте; теперь оно адаптировано для SotC
  и включено по умолчанию (`BB_OVERLAY_MENU=0` отключает его). Тексты на английском языке (PR #94,
  nosekefik).
- **Удалены остатки Bloodborne** (~11 700 строк: старый лаунчер, патчи и моды Bloodborne, значки,
  документы) на основе PR #60 от Oussama268. Исполняемый файл игры (`bb-probe.exe`) теперь имеет
  значок SotC.
- Поиск игры игнорирует папку Windows `WpSystem` (PR #110, plassardewen-cpu).
- Страница на GitHub — только на английском языке.

### 0.60 — 2026-10-20
Благодарим **Lucas-blip-png, Einar и KingJhoss**.

**Исправления**
- **Текстуры на GTX 1060/1070/1080 (Pascal):** поверхности без текстур, плоские или почти чёрные
  цвета (#57, #68, #102). В Pascal отсутствует функция интерполяции, которую использует игра; она
  эмулируется с помощью geometry shader (PR #169, Lucas-blip-png).
- **Зависание или закрытие игры на титульном экране** на некоторых ПК (ядра CPU с разной частотой
  таймеров): часы игры теперь берутся из Windows (PR #174, Einar).
- **Игра не запускалась на ПК с малым объёмом памяти** (регрессия в 0.59): если Windows отказывает
  в выделении 9152 МБ, игра использует объём консоли и записывает предупреждение в журнал.
- **FSR 3: чёрные квадраты в сгенерированных кадрах** на AMD RX 7000 (PR #177, KingJhoss).
- **Обрезанная кнопка ИГРАТЬ** при масштабировании Windows выше 100% (PR #171).
- Исправление в коде, унаследованном от shadPS4: атомарные операции минимума/максимума с числами с
  плавающей запятой на видеокартах без `VK_EXT_shader_atomic_float2` выполняли две операции
  вместо одной.

**Новое**
- **Режим 60 FPS на основе собственного режима «Производительность» игры** (режима PS4 Pro): игра
  теперь рассчитана на 60 FPS, поэтому её внутреннее время идёт синхронно. Это должно исправить
  некорректную физику при 60 FPS (#100, #120); на этих колоссах изменение ещё тестируется
  (PR #183, Lucas-blip-png). `SOTC_MODES=af16` возвращает прежний режим.
- **У каждой копии порта собственная папка `user`** (сохранения, кэш, журналы и настройки лаунчера
  в `user\launcher_settings.json`). Раньше все копии использовали папку первой копии. При первом
  запуске новая копия наследует настройки и **копирует** сохранения и звуковые модули.
- **+6% FPS в храме:** защита памяти игры больше не перестраивается каждый кадр (PR #168,
  Lucas-blip-png).
- Генерация кадров без интерфейса (полоса здоровья не дрожит), **экспериментально и отключено**:
  `SOTC_FG_HUDLESS=1` (PR #154, Lucas-blip-png).
- При запуске через `SotC.bat` журнал также записывается в `user\last_run.log` (PR #175, Einar);
  исправлены инструменты замеров (PR #181, Einar).

**Известные проблемы:** «Текстуры высокого качества» по-прежнему могут приводить к закрытию игры
на очень широких планах (меч, поднятый к солнцу, область последнего колосса; #186): в этом случае
отключите эту опцию. Белые персонажи на AMD GCN (RX 470–590, Vega, APU): проблема изучается.

### 0.59.1 — 2026-10-20
Благодарим **Lucas-blip-png и Oussama268**.

- **Мерцание дальних теней** (#178, #157, #163; AMD и NVIDIA GTX 1080, RTX 40/50): повторное
  использование теней (начиная с 0.58) перерисовывало дальние тени только раз в два кадра, а игра
  не сохраняет тень пропущенного кадра. Теперь функция отключена (PR #170, Lucas-blip-png).
  Замер на всём демо: отключено — 61,8 FPS в среднем / 37,2 минимум, включено — 59,5 / 35,6, то
  есть прироста FPS не было. Новая опция **«Дальние тени»** в лаунчере («Отключено» / «Включено» /
  «Автоматически»).
- **AMD: полосы на тенях и воде** (форк Oussama268, opt8): прямой доступ к памяти по умолчанию
  отключён на видеокартах AMD (его можно включить в лаунчере).
- **Intel, частичные исправления** (Oussama268): допустимые размеры dispatch и layout, отказ от
  64-битных целых чисел, если GPU их не поддерживает, фиксированный размер subgroup.
- **Журнал показывает видеокарту и драйвер** (строка `GPU:`) для отчётов в issues;
  `SOTC_GPU_VENDOR=10de|1002|8086` выбирает GPU на ноутбуках с двумя видеокартами (Oussama268).
- Мелкие исправления по итогам ревизии (Oussama268).

### 0.59 — 2026-10-20
Благодарим **Oussama268, Lucas-blip-png и EdouardSence**.

**Производительность**
- **Более высокий FPS и меньше подвисаний** (форк Oussama268, opt5–opt7): отрисовка
  подготавливается в отдельном потоке (#99 Lucas-blip-png), прямая память передаётся порциями, а
  шейдеры и вычислительные пайплайны транслируются во вспомогательных потоках (объект ждёт
  несколько кадров вместо того, чтобы экран зависал). Демо титульного экрана: среднее 63 →
  65–67 FPS, минимум 30 → 34–37, мост 30–32 → 37–40.

**Стабильность**
- **Вылет в конце игры и при загрузке сохранений** (`0x1a371` / `0x49500`): GPU иногда возвращал
  устаревшие данные в память, которую игра уже освободила, повреждая аллокатор. Защита из
  Pink-shadPS4 охватывала только один из пулов памяти игры; теперь она охватывает все пулы того же
  типа, включая создаваемые при загрузке мира (область последнего колосса). Кроме того, игра
  получает 9152 МБ прямой памяти во всех режимах (#146, #149). Причина найдена по логам реальных
  вылетов; полное прохождение до титров ещё тестируется.
- **Видеокарты с 6 ГБ**, на которых игра закрывалась из-за нехватки видеопамяти: перед отказом
  проверяются точный размер и ОЗУ ПК (#137).
- **Лаунчер в китайской версии Windows** ломался из-за символа «™» в названии (#151).

**Новое**
- **F11** переключает оконный / полноэкранный режим (#118, EdouardSence).
- **Использование видеопамяти:** «Экономное», «Стандартное», «Высокое» и «Максимальное» (раньше
  игра очищала текстуры, начиная с 60% памяти видеокарты, — так называемый «лимит 6 ГБ» на
  видеокартах с 8–16 ГБ) (#143).
- **Громкость:** от 100 до 300% без перегрузки звука (#95).
- **Автоматическое обновление:** лаунчер сообщает о выходе новой версии и устанавливает её
  самостоятельно, сохраняя сохранения, кэш шейдеров и sotc.ini (#136, Oussama268). Действует
  начиная с этой версии.
- **Сверхширокий экран с режимом PS4 Pro:** при 21:9 и 32:9 с включённым режимом Pro по краям были
  чёрные полосы; теперь приоритет отдаётся широкому экрану (#142, #145).

### 0.58.1 — 2026-10-19
- **Текстуры высокого качества** (по умолчанию включены в «Быстрых настройках»): начиная с 0.0.13
  порт ни разу не сообщал игре, какие текстуры используются, и она не загружала версии высокого
  разрешения (размытые листья дерева во вступлении и размытые камни). При отключении опции
  возвращается прежнее поведение — для видеокарт с малым объёмом видеопамяти.
- **Значок окна игры:** рисунок, созданный для порта (мальчик, поднимающий меч), чтобы значок на
  панели задач не совпадал со значком лаунчера. В разделе «Изображение и FPS»: значок по
  умолчанию, значок игры или ваше собственное изображение.

### 0.58 — 2026-10-19
Версия от сообщества: объединяет pull request и форки нескольких участников, проверенные и
протестированные вместе. Благодарим **Lucas-blip-png, Gabriell-os, Oussama268, jownatan, xeodeo,
nakaionyt/y7ngboy и tabnul**.

**Стабильность**
- **Вылеты из-за нехватки видеопамяти** (`EnsureResident: Assertion Failed`, видеокарты AMD и с
  4–6 ГБ): память буферов выделяется блоками по 64 МБ (раньше — тысячами выделений, сверх лимита
  драйверов), а простаивающая память возвращается по мере использования с предварительной
  проверкой данных (Gabriell-os #98, Oussama268 #59/#83/#103).
- **Зависание игры** («GPU hook queue overflow»): очередь уведомлений о памяти растёт по мере
  необходимости.
- **Вылет после 1-го и 2-го колосса в режиме PS4 Pro** (`0x1a371`): режим Pro получает объём
  памяти PS4 Pro (Lucas-blip-png #106).
- Пакет-заполнитель в очереди compute, повреждавший следующий пакет; округление RTZ отключено на
  NVIDIA (драйвер зависал); блокировка регионов без взаимоблокировки в Windows (jownatan #81).

**Производительность:** демо титульного экрана 54–55 → **62 FPS в среднем**, минимум 26 → 31;
видеопамять ~4,3 → ~2,7 ГБ (оптимизации обработчика команд, компиляция пайплайнов в фоне,
параллельное чтение кэшей, повторное использование дальних теней).

**AMD:** чёрный блок в углу экрана на RDNA3/RDNA4 (#43) и текстуры BC5/BC6H (xeodeo); однородные
ветвления wave64 и периодическая отправка включены на AMD (Gabriell-os #98). На видеокартах AMD
у нас это ещё не тестировалось: сообщайте о результатах в issues.

**Новое**
- **Широкий / сверхширокий экран:** 16:10, 21:9 и 32:9 с более широким изображением, без
  растягивания (tabnul #54).
- **FPS: 30 / 60 / без ограничения** в лаунчере.
- **Значки кнопок Xbox** в игре (A B X Y, RB, LT/RT, Menu, View), нарисованные для порта; выбор
  PlayStation / Xbox в разделе «Управление» (механизм замены текстур от xeodeo).
- **Генерация кадров (экспериментально):** FSR 3 на любой видеокарте и DLSS-G на RTX 40/50
  (официальная среда выполнения NVIDIA в комплекте, с лицензиями), при этом игра выводит на экран
  30 → 60 (nakaionyt/y7ngboy). Имеет смысл только при FPS 30; полоса здоровья/выносливости может
  дрожать.
- Оверлей производительности (F10), F7 включает и отключает ограничение 30 FPS, F8 переключает
  генерацию кадров.
- **Лаунчер на испанском, китайском, корейском и русском языках.**
- Окно со значком игры, в заголовке — версия и FPS.
- **Список сохранений с изображением каждого сохранения**, как на PS4 / в shadPS4.
- **Экран загрузки** с индикатором в процентах на время чтения кэша шейдеров.
- Язык выбирается уже на первом экране лаунчера; режим PS4 Pro и резкость (RCAS) в «Быстрых
  настройках».
- Кэш шейдеров создаётся заново при первом запуске.

### 1.0.1 — 2026-10-18 (форк MMadmer)
- **Звук порта по умолчанию:** лаунчер больше не копирует самостоятельно звуковые модули
  установленного shadPS4 (`user/sys_modules`) и не включает их: все звуки воспроизводит собственная
  звуковая библиотека порта. При автоматически скопированных модулях поступали сообщения об
  отсутствии большей части звуковых эффектов (удары, стук копыт, стрелы) — оставались только музыка
  и голоса. Конфигурации, в которых предыдущий лаунчер выполнил такое копирование, возвращаются к
  библиотеке порта; папка, выбранная вручную, по-прежнему используется.
- **libSceAudio3d и UserFx в NGS2:** порт реализует libSceAudio3d (объекты позиционируются в
  стереопанораме по азимуту и ширине и воспроизводятся на устройстве вывода по умолчанию) и
  вызывает UserFx голосов сэмплера NGS2, через которые звуковой движок игры (sndx) передаёт объекты
  3D-звука, когда этот режим активен. Звуки, которые PS4 направляет в динамик геймпада, на
  гарнитуру или на дополнительный выход, также воспроизводятся на устройстве по умолчанию.
- **Режим окна:** оконный, полноэкранный в окне (без рамки, в разрешении монитора) или
  эксклюзивный полноэкранный (монитор переключается на выходное разрешение) вместо флажка
  «Полноэкранный режим» (`SOTC_WINDOW_MODE=windowed|borderless|fullscreen`; установленный флажок
  соответствует полноэкранному режиму в окне).
- **Языки лаунчера:** английский, русский, китайский (упрощённый), японский и португальский, с
  мгновенным переключением и шрифтами Windows для китайского и японского; автоматический выбор
  языка следует языку Windows.
- Русскоязычные тексты документации отредактированы в официальном стиле.

### 1.0.0 — 2026-10-18 (форк MMadmer)
Первый релиз форка [MMadmer/ShadowOfTheColossus-PC](https://github.com/MMadmer/ShadowOfTheColossus-PC),
включающий всё из 0.57-opt6 и 0.57-opt7.
- **Полная поддержка геймпада Xbox и кнопки используемого устройства:** с геймпадом Xbox (или
  универсальным) игра показывает кнопки Xbox (A/B/X/Y, LB/RB, LT/RT, LS/RS, Menu, View,
  крестовина) белым цветом в чёрном круге; при игре на клавиатуре и мыши — назначенные клавиши и
  кнопки мыши в том же стиле (крестовина и левый стик — в виде четырёх клавиш с подсветкой клавиши
  текущего направления; камера — в виде мыши); с DualShock или DualSense — кнопки самой игры.
  Схема в разделе «Параметры > Управление» превращается в геймпад Xbox либо в клавиши и мышь для
  каждой строки. Всё отрисовывается шейдером при запуске (вместе со сборкой шейдеров), значительно
  тоньше, чем в игре, и чётко при любом разрешении. Текстуры игры распознаются по содержимому при
  загрузке в GPU, а подмена выполняется при привязке (bind); память и файлы игры не изменяются.
  Подмена происходит мгновенно: подключение, отключение или использование другого устройства
  меняет отображаемые кнопки и схему геймпада (`SOTC_PAD_GLYPHS=xbox|ps|keyboard` задаёт
  фиксированный вариант; параметр **Значки кнопок** в разделе «Управление»).
- **Вибрация** как на PS4 на любом геймпаде: запрошенное игрой значение сохраняется до тех пор,
  пока игра его не изменит (ранее вибрация прекращалась через 1 с), и передаётся геймпаду,
  подключённому в процессе игры.
- **Установщик и zip-архив:** `SotC-PC-Setup-<версия>.exe` выполняет установку для текущего
  пользователя (без прав администратора), создаёт ярлыки и программу удаления, которая не
  затрагивает сохранения и настройки; zip-архив содержит ту же папку в портативном виде.
- **Сборка одной командой:** `build.cmd` загружает собственный MSYS2 в `.deps`, устанавливает
  пакеты, подмодули и PkgTool, выполняет сборку и открывает лаунчер; `build.cmd -Package` создаёт
  zip-архив и установщик. Тестовый стенд находится в `tools/bench`.
- Для звука используется собственная библиотека порта; звуковые модули консоли необязательны.
- Руководство на русском языке: `docs/README_RU.md` (входит в пакет как `README_RU.md`).

### 0.57-opt7 — 2026-10-18
- **Командный процессор без активного ожидания:** когда все пакеты находятся в ожидании
  (WAIT_REG_MEM, семафоры, счётчики CE/DE), он работал вхолостую на 100% (чтение часов и
  блокировка на каждой итерации: треть его времени). Теперь после нескольких итераций он засыпает
  до тех пор, пока поток отрисовки не выполнит следующую операцию, либо уступает ядро
  (SwitchToThread) и ненадолго засыпает, если в очереди нет отрисовок (`SOTC_CP_IDLE=0`
  возвращает прежнее поведение). Храм: 17,6 → 14,3 мс разбора на кадр.
- **Поток отрисовки:** render targets отрисовки с теми же регистрами целей и тем же набором
  изображений используются повторно без нового поиска (`SOTC_RT_MEMO=0` отключает); проверка
  потока при чтении регистров больше не вызывает winpthreads (~2,5% времени потока); повторная
  пометка буфера, уже записанного GPU, не захватывает блокировку.
- **Отбрасывание повторяющихся push descriptors:** повторно используемая привязка сохраняет копию
  flat buffer в том же command buffer, а поток записи отбрасывает push descriptors и push
  constants, совпадающие с последними (`SOTC_PUSH_DEDUPE=0` отключает). Меньше вызовов драйвера в
  потоке записи.
- **До 4 ядер:** поток записи уступает ядро, пока ожидает работу (`SOTC_REC_POLITE`), вместо
  вращения в цикле с высоким приоритетом.
- **Звук:** реверберация микшера NGS2 хранит состояние фильтров в регистрах и не выделяет буферы
  на каждый блок (результат побитово идентичен).
- **Автоматическое разрешение рендеринга:** при первом запуске с конкретной видеокартой лаунчер
  выбирает 810p для GTX 1050, MX и видеокарт с 2 ГБ, 900p для GTX 1050 Ti и видеокарт с объёмом до
  3 ГБ, 720p для встроенной графики; выбор выполняется один раз для каждой видеокарты, а значение,
  заданное в разделе «Графика», сохраняется.
- Недопустимый T#, прочитанный при компиляции шейдера (запись через FMask), больше не приводит к
  завершению игры.
- Протестировано (4 ядра без SMT + эмуляция бюджета 2 ГБ, RTX 3070 Laptop): Храм — 30 FPS на
  месте, 29,6 в среднем при ходьбе (ранее 29,0); колосс 13 в 810p при беге с вращением камеры —
  30,0 FPS (минимум 30,0), занято 1,85 ядра (ранее 2,1), GPU 5,1 мс на частоте 2100 МГц и 1,77 ГБ
  видеопамяти; отброшено 62% push descriptors в Храме.
- Диагностика: `SOTC_PROFILE="A|B|C"` сэмплирует несколько потоков, а
  `SOTC_PROFILE_STACKS=<arquivo>` записывает полные стеки (`tools/profile_tree.py` строит дерево
  вызовов); `SOTC_CENSUS_DUMP=<arquivo>` выводит список изображений в кэше.

### 0.57-opt6 — 2026-10-18
- **ПК с 4 ядрами:** четыре рабочих потока игры непрерывно опрашивали очередь задач (на PS4 у
  каждого из них своё ядро) и отнимали ядра у потоков рендеринга. Теперь после серии пустых
  опросов они засыпают на 0,1 мс (`SOTC_POLL_NAP_US`, 0 отключает). Мьютексы PS4 используют
  SRW-блокировки Windows. Храм: занято ядер 7 → 3,5, 27,4 → 29,9 FPS. При 4 ядрах и менее потоки
  команд и отрисовки работают с высоким приоритетом, как и звуковой микшер (`SOTC_GPU_PRIORITY`).
- **GTX 10 (Pascal):** барицентрические координаты, которые читают пиксельные шейдеры игры,
  проходят через геометрический шейдер *passthrough* (VK_NV_geometry_shader_passthrough):
  пиксельный шейдер вычисляет барицентрические координаты по позиции. Храм: GPU 16,1 → 13,8 мс в
  профиле Pascal (`SOTC_GS_PASSTHROUGH=0` возвращает прежнее поведение). Кэш шейдеров создаётся
  заново при первом запуске.
- **Разрешение рендеринга** (лаунчер, режимы `res900`/`res810`/`res720`/`res960`): игра
  рендерит изображение ниже 1080p и сама масштабирует его до 1080p, как PS4 Pro делает это с 1440p
  до 4K. Текстуры и туман не меняются.
- **Однократная компиляция шейдеров:** папка `shaderpack` содержит кэш конвейеров, записанный в
  игре; при первом запуске он становится кэшем игрока, и все конвейеры собираются на экране
  загрузки, без компиляции во время игры.
- **Лаунчер распаковывает дампы .pkg:** игра, обновление и все найденные DLC превращаются в папки
  `CUSA08809`, `CUSA08809-UPDATE` и `CUSA08809-DLC` рядом с пакетами (PkgTool из LibOrbisPkg,
  LGPL-3.0, в `tools/PkgTool`). В том числе DLC, которые только открывают доступ к контенту
  (лицензия).
- **Меньше работы на каждую отрисовку:** две трети привязок повторяют предыдущую и повторно
  используют дескрипторы (`SOTC_BIND_REUSE=0` отключает); результат поиска перестановки шейдера
  запоминается; защита кучи игры при чтениях GPU обходит дерево свободных блоков не чаще одного
  раза в 250 мс.
- **Видеокарты с объёмом памяти менее 3 ГБ:** память буферов удерживается на уровне ~45% бюджета
  (блоки, не используемые 8 с, освобождаются). `SOTC_VRAM_LIMIT_MB` (эмулируемая видеокарта)
  больше не отклоняет выделение памяти: строка `SOTC VRAM` показывает, насколько превышен
  эмулируемый объём.
- Язык системы соответствует языку Windows, если в лаунчере оставлено значение «Автоматически»;
  в список добавлен русский язык.
- Протестировано (4 ядра без SMT + эмуляция бюджета 2 ГБ, RTX 3070 Laptop): Храм — 30 FPS
  (ограничение игры), демо на титульном экране — 30,0 FPS (минимум 29,9).

### 0.57-opt5 — 2026-10-17 (форк Oussama268)
- **Длительные сессии без роста потребления видеопамяти:** игра держит отображённым свой блок
  памяти объёмом 4 ГБ и постоянно повторно использует адреса, поэтому память буферов в GPU только
  росла (~45 МБ в минуту в демо на титульном экране: 1,6 → 4,3–4,4 ГБ за 30 минут). На видеокарте
  с 6 ГБ это приводило к превышению бюджета примерно через 40 минут игры, и подтормаживания
  возвращались. Теперь, когда память буферов превышает 1,5 ГБ, блоки, которые не использовались
  30 секунд и не содержат данных, записанных GPU, освобождаются (не более 64 МБ каждые 2 секунды,
  ~20 мс за раз). Перед освобождением порт считывает блок обратно и сравнивает его с памятью игры,
  поэтому изображение не меняется. За 30 минут демо: пик видеопамяти **3,4 ГБ, стабильно** (ранее
  4,3–4,4 ГБ с продолжающимся ростом), ни одной экстренной очистки. `SOTC_ARENA_EVICT_MB=0`
  отключает; `SOTC_ARENA_EVICT_MB`, `SOTC_ARENA_EVICT_IDLE_S` и `SOTC_ARENA_EVICT_STEP_MB`
  настраивают.
- **Немного более быстрый открытый мир:** когда команда выполняет запись в страницу, которую игра
  затем прочитает, порт сразу отправляет работу на GPU (`SOTC_RB_EARLY`, теперь по умолчанию).
  Демо: 55,0 → 56,2 FPS (+2%); Храм без изменений.
- **Исправление редкого вылета:** 0.57-opt4 иногда закрывалась при загрузке сохранения с ошибкой
  «Attempted to protect region as write-only» (2 раза примерно из 16 тестов в Храме). Вероятная
  причина устранена, а если ошибка всё же возникнет, защищается вся страница и игра продолжает
  работу; в журнал (`user/last_run.log`) записывается строка «Write-only protection» для
  расследования.
- Профиль PGO создан заново для нового кода.
- Диагностика: при `SOTC_FPS=1` в журнале выводится строка `SOTC ARENA` (освобождённая память).
- Протестировано: 30 минут демо (память стабильна), 15 минут на финальной сборке, демо и Храм без
  ошибок (чередующиеся пары в сравнении с 0.57-opt4), изображения идентичны (лист скриншотов).

### 0.57-opt4 — 2026-10-16 (форк Oussama268)
- **Больше FPS везде:** поток, преобразующий команды игры в команды Vulkan (узкое место порта),
  тратил ~42% времени внутри Windows и библиотеки C: блокировки, изменения защиты страниц,
  копирование и выделение памяти. Измерено в сравнении с 0.57-opt3 на том же ПК в то же время
  (RTX 3060 Laptop, чередующиеся пары):
  - Храм: 29,3 / 29,1 → **35,0 / 34,1 FPS** (+18%).
  - Демо на титульном экране: 47,4 → **53,5 FPS**, 1% low 29,4 → **37,9**, кадров дольше 50 мс
    129 → **26**.
- **Что изменилось:**
  - Блокировка кэша текстур захватывается один раз на группу текстур, а не дважды на каждую
    текстуру (62 тыс. раз за кадр), а на стороне потока команд обходится без атомарной
    инструкции. Сбои записи игры в страницы без текстур вообще не проходят через блокировку.
    `SOTC_TC_BATCH=0`, `SOTC_TC_BIASED=0` и `SOTC_TC_SKIP_EMPTY=0` отключают соответствующие
    части.
  - Таблицы, которые игра заполняет каждый кадр шейдером копирования (портом это выполняется на
    CPU), сравниваются постранично, и записываются только изменившиеся страницы — напрямую в
    память GPU, без пометки памяти как изменённой: объём загрузки буферов за кадр снизился с 21 до
    ~13 МБ. `SOTC_FILL_HLE_PAGES=0` возвращает прежний способ.
  - Пара изображений глубины 960x540 создавалась и уничтожалась каждый кадр; теперь она
    используется повторно («depth twin» — эта опция уже существовала и стала вариантом по
    умолчанию). `SOTC_DEPTH_TWIN=0` отключает.
  - Более быстрый поиск в памяти: увеличенный кэш регионов памяти игры для каждого потока
    (`SOTC_REGION_DIRECT=0` отключает), кэш уже резидентных диапазонов буферов, чтение часов раз в
    8 команд.
  - Профиль PGO создан заново для нового кода.
- Диагностика: при `SOTC_CMD_STATS=1` в журнале выводится строка `SOTC sys/frame` (защита
  страниц, блокировки, выделения видеопамяти за кадр).
- Протестировано: Храм и демо без ошибок, изображения идентичны (лист скриншотов), 15 минут демо.
### 0.57-opt3 — 2026-10-16 (форк Oussama268)
- **Меньше подвисаний при первом посещении области:** новые графические конвейеры теперь
  собираются в отдельных потоках, а объект, которому нужен ещё не собранный конвейер, не
  отрисовывается несколько кадров (появляется мгновением позже) вместо остановки всей игры. С
  пустым кэшем шейдеров в демо на титульном экране: кадров дольше 50 мс 235 → 156, 1% low
  23,9 → 29,1 FPS. Полноэкранные проходы (постобработка) по-прежнему собираются сразу, чтобы не
  появлялся чёрный кадр. `SOTC_ASYNC_SHADERS=0` возвращает прежнее поведение.
- Трансляция шейдеров (основная часть оставшихся подвисаний) по-прежнему выполняется в основном
  потоке команд; её перенос в фоновый режим — следующий шаг.
- Содержимое папки патчей шейдеров считывается один раз, а не двумя обращениями к диску на каждый
  шейдер.
- Диагностика: при `SOTC_FPS=1` строка `SOTC COMPILE` показывает, на что уходит время компиляции.
- Протестировано: 15 минут демо с пустым кэшем без ошибок; с заполненным кэшем — Храм 32,4 FPS и
  демо 50,0 FPS (без потерь).

### 0.57-opt2 — 2026-10-16 (форк Oussama268)
- **Значительно меньше подтормаживаний при входе в новые области (видеокарты с 6 ГБ):** текстуры,
  у которых T# начинается с mip-уровня выше 0, никогда не используют самые детальные mip-уровни,
  но порт хранил в видеопамяти всю цепочку. Теперь такие текстуры, доступные только для чтения,
  хранят лишь используемые mip-уровни («mip tail»). В демо на титульном экране пик видеопамяти на
  видеокарте с 6 ГБ снизился с ~6,1 ГБ до ~3,4 ГБ, число кадров дольше 50 мс — со 150 до ~105, а
  худший кадр при загрузке области — с 460 мс до ~280 мс. Изображение идентично.
  `SOTC_MIP_TAIL=0` отключает.
- **Асинхронное чтение данных GPU по умолчанию:** когда игра читает память, записанную GPU
  (каждый кадр), GPU ожидает только поток игры; командный процессор продолжает работу. Храм
  29,2 → 30,7 FPS; в демо — примерно на 25% меньше кадров дольше 50 мс. `SOTC_RB_ASYNC=0`
  отключает.
- **Освобождение памяти буферов при снятии отображения:** раньше она никогда не возвращалась, и
  видеопамять росла на ~90 МБ в минуту (1,6 → 6,0 ГБ за 48 минут в демо), пока память не
  заканчивалась. Теперь она растёт гораздо медленнее (за 24 минуты: 4,1 ГБ вместо 4,8–5,0 ГБ).
  Функция была необязательной из-за давнего вылета при загрузке сохранения, который в тестах не
  повторился (3 загрузки, 25 минут демо). `SOTC_ARENA_RELEASE=0` возвращает прежнее поведение.
- Со всеми этими изменениями демо работало в среднем на 49,9 FPS с 47 кадрами дольше 50 мс (ранее:
  ~47 FPS и 100–150).
- **Вылеты оставляют след:** лаунчер хранит журналы последних 5 сессий (`last_run.1.log`
  …) и копирует журнал сессии, завершившейся с ошибкой, в `user/crashes/`; при вылете игры
  bb-probe записывает туда минидамп (`.dmp`). `SOTC_MINIDUMP=0` отключает дамп.
- **Отчёт о подтормаживаниях для разработчиков:** при `SOTC_FPS=1` журнал каждые 5 с показывает
  время кадра (медиана, 1% low, худший кадр) и то, на что ушло время медленных кадров;
  `tools/frame_report.py` формирует сводку по журналу.
- Память буферов выделяется блоками по 4 МБ вместо отдельного выделения на каждый запрос
  (`SOTC_ARENA_SLAB_MB`); крупные загрузки копируются параллельно (`SOTC_COPY_BATCH`).
- **Пока не решено:** при первом посещении каждой области шейдеры компилируются, и игра
  подтормаживает (до 1–2 с); начиная со второго раза всё идёт плавно. Фоновая компиляция —
  следующий шаг.
- При первом запуске компилируются некоторые новые шейдеры (несколько секунд); существующий кэш
  остаётся действительным.

### 0.57 — 2026-10-14
- **Видеокарты AMD: исправлены зелёные/розовые цвета** в тумане, на Вандере и на Агро (issues #6,
  #7, #9, возможно #10). AMD сообщает о группах по 64 потока, но позволяет зафиксировать этот
  размер только в вычислительных шейдерах; порт требовал 64 на всех стадиях и не преобразовывал
  вычислительные шейдеры. Теперь видеокарты AMD используют тот же 32-поточный путь, что и NVIDIA
  (compute зафиксирован на 32, барьеры разделяемой памяти, без оптимизации однородных ветвлений,
  из-за которой персонажи в тени становились чёрными). Диагностику провёл raulbezerro-dev в
  issue #7. На NVIDIA ничего не меняется.
  **Ещё не протестировано на видеокарте AMD:** сообщите в issue, стало ли лучше.
- **Новый параметр в лаунчере:** «Полные отражения воды (прямой доступ к памяти)», включён по
  умолчанию. При его отключении FPS возрастает примерно на 10% (демо на титульном экране: 54 → 59),
  но дальняя вода выглядит более гладкой. У каждого режима собственная папка кэша шейдеров.
- Кэш шейдеров создаётся заново при первом запуске.

### 0.56 — 2026-10-13
- **Вода открытого мира с волнами:** озёра и реки под водопадами были гладкими, как зеркало, без
  движения. Шейдер этой воды сохраняет маску активных потоков инструкцией `s_wqm_b64 vcc, exec`, а
  затем по ней решает, применять ли карту течения (волны). Рекомпилятор обрабатывал `S_WQM_B64`
  как «ничего не делать», в VCC оставался мусор, и шейдер всегда записывал нормаль, направленную
  строго вверх. Теперь инструкция делает то же, что и оборудование: расширяет маску до целых
  квадов 2×2 пикселя. Простое копирование исправляло воду, но оставляло «цветной мусор» в кронах
  далёких деревьев, которые используют это расширение в тесте прозрачности. Подробности — в
  `docs/sotc/WATER.ru.md`.
- **Прямой доступ к памяти (DMA) включён по умолчанию,** как в конфигурации Pink-shadPS4 для
  SotC. Чтения из таблиц с индексом, вычисляемым в шейдере (записи *probe*, используемые
  отражениями в экранном пространстве), всегда читали позицию 0; теперь они читают правильный
  адрес. Благодаря этому верхнее озеро покрывается рябью и отражает окружение на большом
  расстоянии. `BB_DIRECT_MEMORY_ACCESS=0` отключает.
- Кэш шейдеров создаётся заново при первом запуске (первая загрузка сопровождается
  подтормаживанием).

### 0.0.13 — 2026-10-13
- **Исправлен вылет в 0.0.12:** игра могла закрываться во время вступления или в меню (сбой по
  адресу `0x1a371`, 1 из 3 тестов). Причина: порт помечал каждую увиденную текстуру как «всё ещё
  используемую» примерно на 3 секунды, и игра пыталась загрузить больше текстур, чем помещается в
  её пул. Теперь учитываются только текстуры, использованные с момента последнего запроса (как в
  Pink-shadPS4). Протестировано: 0 вылетов за 5 прогонов вступления и сессия продолжительностью
  почти 40 минут с перемещением по карте и двумя загрузками сохранений.
- **Значительно более обширные отражения в воде:** озеро теперь отражает скалы, храмы и
  растительность почти по всей поверхности, с рябью. Предохранительный лимит итераций циклов в
  шейдерах обрывал лучи отражений в экранном пространстве; теперь лимит задаётся для каждого цикла
  отдельно.
- Изучается: там, где вода должна отражать небо, она по-прежнему тёмная и гладкая.

### 0.0.12 — 2026-10-13
- **Чёткие текстуры возвращены, без закрытия игры:** повреждение памяти, из-за которого игра
  закрывалась сразу после загрузки сохранения (сбой по адресу `0x1a371`), а на текстурах
  появлялся «цветной мусор», было вызвано тем, что эмулятор копировал данные GPU в память, которую
  игра освобождала. Это исправлено, и потоковая загрузка текстур снова включена на уровне 2
  (текстуры 2048 — до 512×512): камни и стены стали значительно чётче. Протестировано более
  25 минут рядом с водопадом без вылетов.
- **Вода:** брызги, круги и пена вокруг Вандера при плавании; озеро отражает небо и солнечные
  блики. Отражения окружения (проход локального IBL) теперь используют правильную *cubemap* для
  каждого *probe* — исправление перенесено из Pink-shadPS4 (`dynamic_tsharp_array_size`).
- Изучается: вода рядом с камерой по-прежнему не покрывается рябью.
- Параметры для тестирования: `SOTC_LOD_STATS=0` (потоковая загрузка отключена), `SOTC_LOD_LEVEL`,
  `SOTC_UNMAP_READBACK=1`, `SOTC_TSHARP_ARRAY=0`.

### 0.0.11 — 2026-10-13
- **Исправление ошибки 0.0.10 — трава с текстурой из «цветного мусора»:** на уровне 3 потоковой
  загрузки игра освобождала mip-уровни, которые GPU ещё использовал (было заметно на траве рядом с
  водопадом). Потоковая загрузка текстур теперь по умолчанию **отключена**, как в 0.0.9: трава
  отображается нормально, водяные завесы водопадов видны, вылетов при загрузке сохранения нет. Для
  тестирования потоковой загрузки: `SOTC_LOD_STATS=1` (с `SOTC_LOD_LEVEL=2` изображение чётче, но
  игра может закрыться).
- Изучается: неподвижная вода (озеро как зеркало, без волн) и брызги водопадов.

### 0.0.10 — 2026-10-13
- **Исправлено сохранение:** игра никогда не могла сохраниться («нет свободного места»); теперь
  сохранение работает.
- **Список сохранений на экране:** «Загрузить игру» и «Ручное сохранение» показывают сохранения
  (регион, время игры, дата) для выбора, а при сохранении — пункт «+ Новое сохранение».
  Стрелки/крестовина — выбор, Enter/X — подтверждение, Esc/O — возврат, также поддерживается
  двойной щелчок мышью.
- **Удаление сохранения:** в списке — Delete (клавиатура) или Треугольник/Y (геймпад), с
  подтверждением.
- **Текстуры высокого разрешения (потоковая загрузка):** порт теперь отвечает на команду
  статистики текстур GPU (`GET_LOD_STATS`), по которой игра решает, что загружать. На уровне 2
  текстуры 2048 достигали 512×512, а у водопадов появлялись водяные завесы.
- **Самопроизвольное закрытие игры после загрузки сохранения:** потоковая загрузка на уровне 2
  повреждала память игры (3 вылета из 4 тестов рядом с водопадом). Теперь по умолчанию
  используется уровень 3, на котором 4 теста из 4 прошли без вылетов; на нём текстуры 2048
  остаются в 256×256, как до появления потоковой загрузки (водяные завесы водопадов на этом уровне
  ещё не проверялись). `SOTC_LOD_LEVEL=2` возвращает более чёткий уровень с риском вылета.
- **Исправлены чёрные тени:** трава, камни и стены в тени были чёрными. Три исправления
  рекомпилятора шейдеров перенесены из Pink-shadPS4 (игнорировавшиеся константы 1.0 в fetch
  shader, `V_ALIGNBIT`/`V_ALIGNBYTE` со сдвигом 0, `V_CVT_PK_U8_F32`).
- **Частицы GPU:** барьеры разделяемой памяти в группах меньше одной wave; вернулась система
  частиц, которая раньше никогда не отображалась. Брызги и пена водопадов пока не отображаются
  (изучается).
- Лаунчер записывает журнал игры в `user/last_run.log` вместе с кодом завершения.
- Инструменты диагностики: F12 захватывает кадр, когда игра запущена под RenderDoc.
- Новый значок (лицо колосса).
- **Внимание:** если смена сохранения в рамках одной сессии приводит к закрытию игры,
  перезапустите её перед загрузкой другого сохранения.
- Полный обзор проекта — в `docs/sotc/PROJECT-REVIEW.ru.md`.

### 0.0.9 — 2026-10-12
- **Автоматическая начальная настройка:** при первом запуске `SotC.exe` открывает экран
  приветствия, на котором запрашивается только папка игры (или сразу предлагается найденная на
  ПК). Затем он самостоятельно настраивает DLC (внутри папки игры и рядом с ней), сохранения
  (копирует сохранение shadPS4, если у порта его ещё нет) и звук, после чего показывает
  результат. **Настроить заново** повторяет процесс.
- **Основная папка, как в shadPS4:** вы указываете основную папку игры, и лаунчер находит
  подпапки базовой игры (CUSA08809), обновления и DLC.
- **Обновление:** папка `CUSA08809-UPDATE` или `CUSA08809-patch` рядом с игрой используется так
  же, как в shadPS4: её файлы имеют приоритет над файлами базовой игры (`SOTC_UPDATE_DIR`). Если в
  ней есть `eboot.bin`, запускается именно он.
- Более лаконичный главный экран: журнал игры стал неприметной ссылкой; проверка показывает DLC.
- **Папка DLC** в лаунчере (раздел «Игра и сохранения»), как в shadPS4. Она также находится при
  автоматическом поиске: папка `addcont` из shadPS4 или папка с отдельной подпапкой для каждого
  DLC.
- Среда выполнения передаёт игре список установленных DLC (`sceAppContentGetAddcontInfoList`),
  считывая `CONTENT_ID` из `param.sfo` каждого из них (`SOTC_DLC_DIR`).
- **DLC в формате .pkg без установки:** пакеты DLC (тип `0x1b`) читаются непосредственно из
  заголовка, без распаковки. Этого достаточно для DLC игры, которые только открывают предметы
  (древний лук, плащ, меч, лошади). Поиск выполняется в выбранной папке, в папке игры и в соседних
  папках.
- **Режим PS4 Pro:** журнал игры показывает, была ли она запущена как PS4 Pro
  (`sceKernelIsNeoMode -> 1`). Этот параметр лишь открывает графический режим PS4 Pro: в игре
  необходимо выбрать **Кинематографический** или **Производительность** в разделе
  «Параметры > Отображение».

### 0.0.8 — 2026-10-12

Первая публичная версия.

#### Лаунчер (`SotC.exe`)
- **Автоматический поиск** при запуске: находит на дисках ПК игру (CUSA08809), пользовательские
  звуковые модули в установленном shadPS4 (копируются в папку данных порта) и сохранения shadPS4
  (перед импортом запрашивается подтверждение). **Искать на ПК** повторяет поиск.
- **Библиотека игр**, как в shadPS4: папки игр, список со значком, названием, ID и версией.
- **Переназначение клавиш:** щёлкните действие и нажмите новую клавишу (сохраняется как
  `key.<entrada>=` в `sotc.ini`).
- **Новый значок** (оригинальный арт). Окно и ярлык на рабочем столе используют `icon0.png` из
  вашего дампа.
- **Отправка работы на GPU:** «Автоматически» (NVIDIA), «Включено на любой видеокарте»
  (AMD/Intel, экспериментально) или «Отключено».

#### Производительность
Демо на титульном экране, режим 60 FPS, i9-13900K + RTX 2060 SUPER: в среднем 37–45 → **~67 FPS**,
самый тяжёлый участок ~17 → **29–34 FPS**.
- Команды Vulkan записываются и отправляются в отдельном потоке (`cp_record_thread` из
  Pink-shadPS4).
- Периодическая отправка работы на GPU по времени (каждые 0,5 мс), а не по количеству команд.
- Непрямые отрисовки с аргументами на CPU превращаются в прямые.
- Кэши: описания изображений по T#, поиск изображений, сведения о бинарных файлах шейдеров и
  указатель памяти для каждого потока.
- Проверка синхронизации буферов без блокировки, если ничего не изменилось; `RangeSet::Add` не
  вставляет повторно уже помеченные интервалы.
- Сбои записи: страница, вызвавшая сбой, и следующие за ней страницы без данных GPU
  освобождаются вместе (~50 тыс. → ~4 тыс. сбоев в секунду).
- Обратное чтение (readback):
  - недавно прочитанные диапазоны загружаются вместе;
  - «чистые» чтения в таблицах ресурсов шейдеров, при этом битовая карта отслеживает фактическую
    защиту страниц;
  - заполнение таблиц дескрипторов (compute `0x50784801`) выполняется на CPU;
  - упреждающее чтение.
- Из Pink-shadPS4: cvars `doIndexBufferCulling=0 doSunShadowIndexBufferCulling=0` и защита кучи
  BPE от записи со стороны GPU.
- Необязательно, для тестирования: асинхронное обратное чтение (`SOTC_RB_ASYNC=1`) и маркер
  ожидания (`SOTC_WAIT_MARKER=2`).

#### Исправления
- Зависание очереди GPU при записи в отдельном потоке: вывод кадра ожидает его попадания в
  очередь.
- Тихое аварийное завершение из-за защиты страниц в стеке файбера.
- Чёрный экран при загрузке; вылет в режиме PS4 Pro (`alt_tile` в видеобуферах).

### Ранее (разработка, 07–08.10.2026)
- Загрузка игры в загрузчике bbport (импорты по NID, файберы, SSE4a, образ ниже 2 ГиБ).
- Видеоядро из форка shadPS4 SotC; звук с пользовательскими модулями.
- Режимы 60 FPS и 16x AF в виде патчей загрузчика; «замороженный» лаунчер на Tkinter
  (`SotC.exe`).
- Клавиатура и геймпад одновременно, управление камерой мышью, вывод 720p–4K, режим PS4 Pro,
  эффекты движка.
