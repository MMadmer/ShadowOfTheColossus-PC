# Building on Windows
[English](#building-on-windows) · [Русский](#русский)

The port builds and runs natively on 64-bit Windows 10 (1903+) / 11 with the same sources as
on Linux. Players: see `README.md` (it ships in the package) and `docs/sotc/README-SotC.md`.

## Build

The short way: `build.cmd` in the repository root does everything below with a private MSYS2 in
`.deps` (and `build.cmd -Package` also makes the zip and the installer). By hand:

Install [MSYS2](https://www.msys2.org) (`winget install MSYS2.MSYS2`), open the
**MSYS2 CLANG64** shell and install the toolchain and libraries (the counterpart of
`shell.nix`):

```bash
pacman -S --needed git patch \
  mingw-w64-clang-x86_64-{toolchain,cmake,ninja,pkgconf,python} \
  mingw-w64-clang-x86_64-{vulkan-headers,vulkan-loader,vulkan-memory-allocator,glslang} \
  mingw-w64-clang-x86_64-{sdl3,fmt,boost,robin-map,xxhash,ffmpeg,zydis} \
  mingw-w64-clang-x86_64-{spirv-tools,spirv-headers,spirv-cross}
```

magic_enum, miniz and xbyak (not packaged by MSYS2) are submodules under `gpu/third_party/`.
Then, in the repository:

```bash
git submodule update --init --recursive
bash build.sh                      # out/bb-probe.exe, out/bb-gpu-capabilities.exe
python sotc.py                     # prepares the image and runs the game (SOTC_GAME_DIR or sotc.ini)
python launcher/sotc_launcher_win.py
bash packaging/windows/package_sotc.sh  # dist/SotC-PC/ and dist/SotC-PC.zip: SotC.exe + DLLs
```

`sotc.py` runs the preparation scripts (`scripts/`) when the game or the scripts change, then
`bb-probe.exe`; its docstring lists the settings (environment variables or `sotc.ini`). From a
source tree it adds `C:\msys64\clang64\bin` (or `$MSYS2_ROOT`) to `PATH` for the DLLs.
The package freezes the launcher with PyInstaller (`WINPYTHON`: a Windows Python with
PyInstaller and Pillow) into `SotC.exe`, which also runs `sotc.py` (`--run`), the preparation
scripts (`--script`) and the game without the window (`--play`): players need no Python.

The icon (`launcher/sotc.ico`/`.png`) is drawn by `launcher/make_icon.py` and built into
`SotC.exe`, `bb-probe.exe` (the game window) and `bb-gpu-capabilities.exe`.

## What is different from Linux

| Linux | Windows |
|---|---|
| `mov rax, fs:[0]` rewritten to `gs:[0]`, GS base set with `arch_prctl` | GS is the TEB and cannot move. The loader rewrites the displacement to `gs:[0x1480 + 8*slot]` (the TEB's `TlsSlots[slot]`, same 9-byte instruction) and each guest thread keeps its TCB in that TLS slot. The slot is claimed first thing in `main` (Vulkan drivers take many). |
| One memfd for direct + flexible memory, `mmap(MAP_FIXED)` views | A pagefile-backed section; the PS4 range 64 GiB–1 TiB is reserved as one placeholder and views replace placeholders (`VirtualAlloc2`/`MapViewOfFile3`, as in shadPS4). Partial unmaps split views and restore page protections (GPU tracking). |
| `-no-pie`, heap below 1 TiB | `--disable-high-entropy-va`: heaps and thread stacks stay low and out of the PS4 range. |
| SIGSEGV handler, `sigsetjmp` recovery | Vectored exception handler; `bb_setjmp`/`bb_longjmp` (`src/compat_win.c`) restore all Win64 callee-saved registers without SEH unwinding (guest frames have no unwind data; clang's `__builtin_longjmp` restores a wrong frame pointer on Win64). |
| Guest thread stacks from `runtime_low_map` | Windows allocates them, fully committed (`pthread_attr_setstacksize`): guest code has no stack probes and would skip the guard page. The main thread stack is 16 MiB committed. |
| glibc `nanosleep`, monotonic condition variables | High-resolution waitable timers (`compat_sleep_ns`; winpthreads' sleep rounds to 15.6 ms); semaphore deadlines use `CLOCK_REALTIME` (winpthreads rejects monotonic condition variables). |
| `libbbgpu.so` | `libbbgpu.a`, linked into `bb-probe.exe` by CMake (`gpu/CMakeLists.txt`), with a manifest for UTF-8 paths and long paths. |

Diagnostics added on the way: `BB_FRAME_DUMP_TRIGGER=<file>` writes the guest display buffer
and the presented image of the next frame to `BB_DUMP_DIR` (raw 32-bit pixels), which also
works for menus and movies.

Not ported: the watchdog/`--timeout` thread dumps (Linux signals), userfaultfd tracking
(`BB_UFFD`), MangoHud, PGO (GCC flags), the GTK launcher (`launcher/sotc_launcher_win.py`, Tkinter, replaces it), and
the `tests/` programs (several use Linux-only APIs).

---

## Русский

Порт собирается и работает нативно в 64-битных Windows 10 (1903+) / 11 из тех же исходников, что и
в Linux. Игрокам: см. `README.md` (входит в пакет) и `docs/sotc/README-SotC.ru.md`.

### Сборка

Короткий путь: `build.cmd` в корне репозитория выполняет всё описанное ниже с собственной копией
MSYS2 в `.deps` (а `build.cmd -Package` также создаёт zip-архив и установщик). Вручную:

Установите [MSYS2](https://www.msys2.org) (`winget install MSYS2.MSYS2`), откройте оболочку
**MSYS2 CLANG64** и установите набор инструментов и библиотеки (аналог `shell.nix`):

```bash
pacman -S --needed git patch \
  mingw-w64-clang-x86_64-{toolchain,cmake,ninja,pkgconf,python} \
  mingw-w64-clang-x86_64-{vulkan-headers,vulkan-loader,vulkan-memory-allocator,glslang} \
  mingw-w64-clang-x86_64-{sdl3,fmt,boost,robin-map,xxhash,ffmpeg,zydis} \
  mingw-w64-clang-x86_64-{spirv-tools,spirv-headers,spirv-cross}
```

magic_enum, miniz и xbyak (их нет в пакетах MSYS2) подключены как подмодули в `gpu/third_party/`.
Затем в репозитории:

```bash
git submodule update --init --recursive
bash build.sh                      # out/bb-probe.exe, out/bb-gpu-capabilities.exe
python sotc.py                     # подготавливает образ и запускает игру (SOTC_GAME_DIR или sotc.ini)
python launcher/sotc_launcher_win.py
bash packaging/windows/package_sotc.sh  # dist/SotC-PC/ и dist/SotC-PC.zip: SotC.exe + DLL
```

`sotc.py` запускает скрипты подготовки (`scripts/`), когда меняется игра или скрипты, а затем
`bb-probe.exe`; в его docstring перечислены настройки (переменные окружения или `sotc.ini`). При
запуске из дерева исходников он добавляет `C:\msys64\clang64\bin` (или `$MSYS2_ROOT`) в `PATH`
для DLL. Пакет «замораживает» программу запуска с помощью PyInstaller (`WINPYTHON`: Python для
Windows с PyInstaller и Pillow) в `SotC.exe`, который также запускает `sotc.py` (`--run`), скрипты
подготовки (`--script`) и игру без окна программы запуска (`--play`): игрокам Python не нужен.

Значок (`launcher/sotc.ico`/`.png`) рисуется скриптом `launcher/make_icon.py` и встраивается в
`SotC.exe`, `bb-probe.exe` (окно игры) и `bb-gpu-capabilities.exe`.

### Отличия от Linux

| Linux | Windows |
|---|---|
| `mov rax, fs:[0]` переписывается в `gs:[0]`, база GS задаётся через `arch_prctl` | GS — это TEB, и его нельзя переместить. Загрузчик переписывает смещение в `gs:[0x1480 + 8*slot]` (`TlsSlots[slot]` в TEB, та же 9-байтовая инструкция), и каждый гостевой поток хранит свой TCB в этом слоте TLS. Слот занимается в самом начале `main` (драйверы Vulkan занимают много слотов). |
| Один memfd для прямой и гибкой памяти, представления через `mmap(MAP_FIXED)` | Секция, основанная на файле подкачки; диапазон PS4 64 GiB–1 TiB резервируется как один placeholder, а представления заменяют placeholder (`VirtualAlloc2`/`MapViewOfFile3`, как в shadPS4). Частичное снятие отображения разделяет представления и восстанавливает защиту страниц (отслеживание GPU). |
| `-no-pie`, куча ниже 1 TiB | `--disable-high-entropy-va`: кучи и стеки потоков остаются в нижних адресах, вне диапазона PS4. |
| Обработчик SIGSEGV, восстановление через `sigsetjmp` | Векторный обработчик исключений; `bb_setjmp`/`bb_longjmp` (`src/compat_win.c`) восстанавливают все регистры Win64, сохраняемые вызываемой функцией, без раскрутки SEH (у гостевых кадров нет данных раскрутки; `__builtin_longjmp` в clang восстанавливает неверный указатель кадра в Win64). |
| Стеки гостевых потоков из `runtime_low_map` | Их выделяет Windows с полной фиксацией памяти (`pthread_attr_setstacksize`): в гостевом коде нет проб стека, и он перескочил бы через защитную страницу. Стек главного потока — 16 MiB фиксированной памяти. |
| `nanosleep` из glibc, монотонные условные переменные | Ожидаемые таймеры высокого разрешения (`compat_sleep_ns`; sleep в winpthreads округляет до 15,6 мс); сроки ожидания семафоров используют `CLOCK_REALTIME` (winpthreads не принимает монотонные условные переменные). |
| `libbbgpu.so` | `libbbgpu.a`, компонуется в `bb-probe.exe` средствами CMake (`gpu/CMakeLists.txt`), с манифестом для путей в UTF-8 и длинных путей. |

Диагностика, добавленная по ходу работы: `BB_FRAME_DUMP_TRIGGER=<file>` записывает гостевой буфер
отображения и выведенное изображение следующего кадра в `BB_DUMP_DIR` (необработанные 32-битные
пиксели); это работает и для меню, и для видеороликов.

Не перенесены: дампы потоков watchdog/`--timeout` (сигналы Linux), отслеживание через userfaultfd
(`BB_UFFD`), MangoHud, PGO (флаги GCC), программа запуска на GTK (её заменяет
`launcher/sotc_launcher_win.py` на Tkinter) и программы из `tests/` (некоторые используют API,
доступные только в Linux).
