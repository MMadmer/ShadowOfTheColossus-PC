# Shadow of the Colossus PC

**English** · [Русский](README_RU.md) · [Download](https://github.com/MMadmer/ShadowOfTheColossus-PC/releases/latest)

A native Windows port of **Shadow of the Colossus** for PlayStation 4 (Bluepoint, 2018). The
game's own code runs directly on your PC's processor and its graphics are translated to Vulkan.
It is not an emulator, so it needs far less power: a laptop with a GeForce GTX 1050 and a 4-core
i5 runs the game at a steady 30 FPS.

This is Madmer's fork of [AjncSystem/ShadowOfTheColossus-PC](https://github.com/AjncSystem/ShadowOfTheColossus-PC),
focused on performance, weak hardware and ease of use.

> **No game files are included.** You need your own copy of the game, dumped from your own
> PlayStation 4. This project is not affiliated with Sony Interactive Entertainment or Bluepoint
> Games.

## What you get

- **The whole game, start to finish**, including the ending, the credits and the DLC.
- **Smooth play on modest PCs**: the launcher picks a suitable render resolution for weak graphics
  cards, shaders are prepared once and never compiled during play.
- **NVIDIA DLSS** on GeForce RTX cards and **frame generation** (AMD FSR 3.1 on any card,
  NVIDIA DLSS-G on RTX 40/50) with a sharp interface.
- **30 or 60 FPS**, resolutions from 720p to 4K, widescreen 16:10, 21:9 and 32:9, PS4 Pro mode,
  high quality textures, 16x texture filtering.
- **Any controller or keyboard and mouse**: Xbox, DualShock 4, DualSense and others. The game shows
  the buttons of the device you use, Xbox and keyboard ones included. Keys and mouse buttons can
  be remapped, rumble strength adjusted.
- **A launcher that sets everything up**: it finds the game on your drives, unpacks `.pkg` dumps
  (game, update and DLC), imports shadPS4 saves and updates itself.
- **An in-game menu** (Insert, F1 or L3+R3) to change FPS, frame generation, DLSS, textures and
  volume while playing.
- Launcher and in-game menu in English, Russian, Spanish, Portuguese, Chinese, Japanese and Korean.

## Requirements

| | Minimum | Recommended |
|---|---|---|
| System | Windows 10 or 11, 64-bit | Windows 11 |
| Processor | 4 threads (e.g. Core i5-7300HQ) | 6 cores or more |
| Graphics | Vulkan 1.3, 2 GB (e.g. GeForce GTX 1050, Radeon RX 470) | 6 GB or more; GeForce RTX for DLSS |
| Memory | 8 GB RAM and a **16 GB page file** | 16 GB RAM |
| Disk | 15 GB free (30 GB to unpack `.pkg` files) | SSD |

Keep your graphics driver up to date. Processors without AVX2 (Sandy/Ivy Bridge, AMD FX) are
supported: a compatible build is selected automatically.

**What you need from your console:** the game **CUSA08809**, preferably the European version with
update **1.01** (60 FPS and some options need it). Either an unpacked folder (with `eboot.bin`
inside) or the `.pkg` files of the game, the update and the DLC.

## Installation

1. Download from the [latest release](https://github.com/MMadmer/ShadowOfTheColossus-PC/releases/latest):
   - **`SotC-PC-Setup-<version>.exe`**: the installer. No administrator rights needed; you choose
     the folder; it creates shortcuts and an uninstaller.
   - **`SotC-PC-<version>.zip`**: the same port without installation. Unpack it into any folder.
2. Run **`SotC.exe`**. On the first start the launcher looks for the game, its `.pkg` files, DLC
   and shadPS4 saves on your drives. If the game is packed, it shows how much space is needed
   and unpacks it next to the packages.
3. Check the **Game & saves** page. If something was not found, pick the folder yourself or
   press **Search this PC**.
4. Press **PLAY**. The first start prepares the shaders once (a few minutes, with a counter on
   the loading screen); later starts are fast.

**Updating:** the launcher tells you when a new version is out. Install it over the old one:
saves and settings are kept. Saves, the shader cache and logs live in the `user` folder of the
port.

## Controls

| | Controller | Keyboard and mouse |
|---|---|---|
| Camera | Right stick | Mouse (click the window to capture it, Esc releases it) |
| Pause menu | Start / Options | Enter |
| Skip a cutscene | Hold A / ✕ | Hold Space |
| In-game port menu | L3 + R3 | Insert or F1 |
| Frame generation on/off | | F8 |
| Fullscreen on/off | | F11 |

Every key and mouse button can be remapped on the **Controls** page of the launcher. Controllers
connect at any time, also while playing.

## Settings for weak PCs

- **Render resolution** is the main option for a weak graphics card: the 3D scene is drawn at
  810p, 900p or 720p and scaled to the screen; textures, fog and effects stay. The launcher picks
  it on the first start (GTX 1050: 810p, GTX 1050 Ti: 900p).
- **DLSS** (GeForce RTX) gives a sharper picture than a lower render resolution at the same speed.
- **Frame generation** shows 60 frames per second while the game runs at 30. It feels best when
  the game holds 30 FPS steadily.
- **60 FPS** needs a noticeably stronger PC; the original game runs at 30 FPS on PS4.
- With 2–4 GB of video memory, set **Video memory usage** to **Low** and, if needed, turn off
  **High quality textures**.
- On a 4-thread processor, close background programs: the game uses every core.

## Troubleshooting

- **"Virtual memory" warning in the launcher, or the game closes after a few minutes:** the page
  file is too small. Set it to 16 GB: *System → About → Advanced system settings → Performance →
  Settings → Advanced → Virtual memory*.
- **The first start takes long:** the shaders are being prepared, which happens once and again
  after a port or graphics driver update.
- **No sound effects or music on a weak PC:** update to the latest version; close background
  programs.
- **The game froze or closed:** open an [issue](https://github.com/MMadmer/ShadowOfTheColossus-PC/issues)
  and attach `user\last_run.log`. If the game froze, wait a minute before closing it: the log then
  records what every thread was doing.

## Building from source

You need Windows 10/11 and [Git for Windows](https://git-scm.com/download/win). Fork the
repository on GitHub if you plan to change it, then:

```
git clone https://github.com/MMadmer/ShadowOfTheColossus-PC
cd ShadowOfTheColossus-PC
build.cmd
```

`build.cmd` downloads its own MSYS2 toolchain into `.deps` (about 4 GB the first time; nothing is
installed system-wide), builds the port and starts the launcher from the source tree. Later runs
only rebuild what changed.

- `build.cmd -NoLaunch`: build only.
- `build.cmd -Package`: also the zip and the installer in `dist\` (needs Python for Windows).
- `MSYS2_ROOT=<folder>`: use your own MSYS2 installation (packages are listed in
  [packaging/windows/README.md](packaging/windows/README.md)).

To run a build you also need the game, as above. An overview of the code (loader, runtime, the
Vulkan video core) is in [CLAUDE.md](CLAUDE.md); diagnostic switches are listed in
[docs/sotc/README-SotC.md](docs/sotc/README-SotC.md). The packaging refuses any Sony firmware
file, so no `.sprx`/`.prx` can end up in a package by mistake.

## Credits

- [AjncSystem/ShadowOfTheColossus-PC](https://github.com/AjncSystem/ShadowOfTheColossus-PC): the
  original port this fork is based on.
- [bloodborne_pc](https://github.com/Supermedo/bloodborne_pc) (Supermedo, deadinside28): the loader,
  native runtime and launcher the port grew from.
- [shadPS4](https://github.com/shadps4-emu/shadPS4) and
  [Pink-shadPS4](https://github.com/luizgustavs/Pink-shadPS4) (luizgustavs): the Vulkan video core
  and many game-specific fixes.
- Community contributors: Oussama268, Lucas-blip-png, Gabriell-os, jownatan, DUDUYG, xeodeo,
  nakaionyt / y7ngboy, tabnul, EdouardSence, Einar, KingJhoss, nosekefik, raulbezerro-dev and
  everyone who reports problems with logs. 60 FPS patch: illusion (GoldHEN cheats).

## License

**GPL-2.0-or-later** ([LICENSE](LICENSE)), as the original port. Changes of this fork © 2026
Madmer. Third-party components keep their own licenses: shadPS4 (GPL-2.0-or-later), the
submodules listed in `.gitmodules`, the files under `gpu/third_party`, NVIDIA Streamline, and
PkgTool / LibOrbisPkg (LGPL-3.0, [source](https://github.com/maxton/LibOrbisPkg)), shipped
unmodified in `tools/PkgTool`. Release packages are built from this source; each release's exact
source is its git tag.
