# Shadow of the Colossus — PC (native port)

Native port of the PS4 **Shadow of the Colossus** (CUSA08809, EU version 1.01, or CUSA08034,
US version 1.00) to Windows.
The game's code runs directly on the CPU. This is not a full emulator: there is a loader and the
system functions the game needs. The GPU uses the Vulkan video core of the shadPS4 SotC fork.
It is based on [bloodborne_pc](https://github.com/Supermedo/bloodborne_pc) (GPL-2.0).

This package **contains neither the game nor Sony firmware**. You need to have your own.

## Requirements

- Windows 10/11 64-bit and a GPU with Vulkan 1.3.
- Python is not required for `SotC.exe`; only `SotC.bat` needs
  [Python 3](https://www.python.org/downloads/).
- **Your own dump** of the game CUSA08809 (European 1.01) or CUSA08034 (American 1.00) (a folder
  with `eboot.bin`, `sce_module`, `sce_sys`...).
- Optional: `libSceNgs2.sprx` and `libSceUlt.sprx` from **your own console**, decrypted, so that
  the sound uses the console's library instead of the port's. They are the same files as in the
  `user/sys_modules` folder of shadPS4; on a PS4 with GoldHEN they are in `/system/common/lib/`.

## How to play

1. Open `SotC.exe`, the launcher.
2. The first time, it **searches on its own** across the PC's drives for:
   - the game folder (CUSA08809 or CUSA08034);
   - a game save in shadPS4, which it offers to import;
   - the game's DLCs (the shadPS4 `addcont` folder or a folder with one subfolder per DLC).
3. Check them under **Game & saves**. The **game library** works as in shadPS4: add the folders
   where your games are and choose the game from the list (double-click). **Search this PC**
   repeats the automatic search.
4. Click **PLAY**.

The **Play** screen checks everything first: the game version, the sound modules, the saves and
the graphics card. The first time, the game image is prepared, which takes a few seconds. This is
repeated only when the game or the modules change. Saves and the shader cache are kept in `user/`.

Without the launcher, `SotC.bat` does the same by reading `sotc.ini`. `SotC.exe --play` starts the
game directly with the launcher's last options, which is useful for shortcuts and for Steam.
The launcher's Advanced page has a button that creates this shortcut on the desktop; it can be added to Steam as a non-Steam game.

If the connection to GitHub cannot be verified (a certificate error, common with an antivirus or proxy that inspects HTTPS, or a Windows without up-to-date root certificates), the launcher says so and shows where to download the new version: https://github.com/MMadmer/ShadowOfTheColossus-PC/releases

## Image and performance

All of this is in the launcher, on the **Image & FPS** page (and the main ones on **Play**):

- **Output** 720p, 1080p, 1440p or 4K. The game renders at 1080p, as on the PS4, and the image is
  upscaled to the output with **FSR 1** + sharpening (RCAS) or bilinear. The window never exceeds
  90% of the screen; fullscreen uses the whole monitor.
- **60 FPS** (experimental): removes the game's 30 FPS limit (GoldHEN patch by illusion), with
  vblank at 120 Hz. It measured 38 FPS in the open field and ~25 in the temple.
- **Bind reuse** (off by default since 0.60.6; `SOTC_FAST_BIND=1` turns it on): skips rebuilding
  the descriptors when the next draw uses the same pipeline, resources and uniforms. Measured at
  the Shrine: ~25 → 30 FPS. In 0.60.5 it left black dots in a grid on the sand ground; the fix is
  awaiting testing.
- **Sharp textures**: 16x anisotropic filtering, almost free.
- **PS4 Pro mode**: the game sees itself on a PS4 Pro and offers in its own menu (Options >
  Presentation) the **Cinematic** Graphics Mode (1440p, more detail) or **Performance**. It is
  heavier: ~25 FPS in the open field.
- **Effects**: vignette, atmospheric fog and ambient occlusion (SSAO) can be turned off.
  Motion blur, brightness and filters are in the game's own menu.
- **Engine cvars** (Advanced): Bluepoint engine variables, `NAME=value`, for those who want to
  experiment.

The patch modes apply to EU version 1.01 and to US 1.00 (the executable is the same); on another
version they are not applied. Each version has its own saves (`user/savedata/1/CUSA08809` or
`CUSA08034`) and uses only DLCs from the same region.

## Controls

**Mouse:** click in the game window to capture the mouse, which then rotates the camera; Esc or
switching windows releases the mouse. Left button = R1 (grab), right = □ (attack),
middle = L1. Sensitivity and inversion are under **Controls** in the launcher. To remap the mouse
buttons, use `mouse.<input>=left,right,middle,x1,x2`.

A controller (DualShock 4, DualSense, Xbox...) is used automatically. With several connected,
choose one under **Controls** in the launcher (or `BB_GAMEPAD=` in `sotc.ini`). The keyboard works
together with the controller:

| Keyboard | PS4 |
|---|---|
| W A S D | left stick |
| arrow keys | right stick (camera) |
| Space | ✕ |
| Left Shift | ○ |
| E | □ |
| Q | △ |
| 1 / 3 | L1 / R1 |
| R / F | L2 / R2 |
| Z / C | L3 / R3 |
| Enter | Options |
| I J K L | D-pad |
| Tab / Backspace | touchpad (left / right) |

The keys can be changed under **Controls** in the launcher: click the action and press the new
key. The controller and the mouse can also be remapped in `sotc.ini`, for example
`pad.circle=b`. The inputs are:

- buttons: `cross circle square triangle l1 r1 l2 r2 l3 r3 options touchpad touchpad_right`;
- D-pad: `up down left right`;
- on the keyboard, the sticks: `move_up` `move_down` `move_left` `move_right` and
  `look_up` `look_down` `look_left` `look_right`.

## Status

Working: logos, intro, menus with text, loading a save, exploration on horseback,
sound, DualShock, DualSense and Xbox controllers (with each one's icons and vibration).

Performance measured in the title-screen demo, 60 FPS mode, i9-13900K + RTX 2060 SUPER:
~67 FPS on average; the heaviest stretch (the canyon bridge) stays at 29–34 FPS. In normal
mode the game runs locked at 30 FPS, as on the PS4.

## Frequently asked questions

- **DirectX 11/12?** No. The video core (shadPS4) translates the PS4 GPU to Vulkan; the game's
  shaders become SPIR-V. Every graphics card with recent DirectX 12 also has Vulkan 1.3: update
  the driver. A DirectX mode would require rewriting the entire video core.
- **AMD and Intel cards?** The optimizations apply to any card, except two that turn on by
  themselves only on NVIDIA (periodic work submission to the GPU and early reads).
  Under **Image & FPS > GPU work submission**, choose **On** to test them on AMD or Intel; if the
  screen goes black, switch back to **Automatic**.

## Credits

- [bloodborne_pc](https://github.com/Supermedo/bloodborne_pc) (bbport, deadinside28 and forks): loader,
  native runtime and launcher, the base of this port.
- [shadPS4](https://github.com/shadps4-emu/shadPS4) and the shadPS4 SotC fork: Vulkan video core.
- [Pink-shadPS4](https://github.com/luizgustavs/Pink-shadPS4) (luizgustavs): the culling cvars, the protection
  of the game heap against GPU reads (`bpe_heap_guard`), the periodic flush, the early reads, the
  wait marker and the Vulkan command recorder on its own thread (`cp_record_thread`).

## Diagnostics

These environment variables help investigate problems:

| Variable | Effect |
|---|---|
| `SOTC_FPS=1` | frame rate every 5 s |
| `SOTC_WINDOW_MODE=windowed/borderless/fullscreen` | window, borderless fullscreen window or exclusive fullscreen (at the output resolution) |
| `SOTC_PAD_GLYPHS=xbox/ps/keyboard` | fixes the button icons (default: those of the device in use) |
| `BB_FAULT_RING=file` | records the latest exceptions (survives crashes) |
| `BB_STACKS_TRIGGER=file` | creating the file dumps the thread stacks |
| `BB_READBACKS=0/1/2` | GPU reads; the default 2 is the one the game requires |
| `SOTC_FAST_BIND=1` | reuses descriptor binds between identical draws (default: off since 0.60.6) |
| `SOTC_RECORD_THREAD=0` | records the Vulkan commands on the command thread itself (default: its own thread) |
| `SOTC_INDIRECT_DIRECT=0` | keeps indirect the draws whose arguments are on the CPU |
| `SOTC_WAIT_MARKER=1/2` | waits for the GPU poll a marker written by it (experimental) |
| `SOTC_CMD_STATS=1/2` | draws and dispatches per frame (2: most used compute shaders) |
| `SOTC_TSC=qpc/native` | game clock: `qpc` always uses the Windows QPC, `native` always the processor's TSC (default: QPC only when the cores disagree on the TSC) |
| `SOTC_UI_HIRES=0` | with DLSS, the interface (text, logos, HUD) is again drawn at the render resolution and upscaled, instead of at the output resolution |
| `SOTC_FG_MARK=1` | FSR 3: a magenta square in the bottom-left corner of generated frames, a cyan one on game frames held in their place (telling them apart in screen recordings) |
