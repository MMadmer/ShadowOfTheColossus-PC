#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Shadow of the Colossus (CUSA08809 EU 1.01 or CUSA08034 US 1.00) on PC: prepares the game image
and starts it.

    python sotc.py                      (or double-click SotC.bat)

Settings (environment variables, or sotc.ini beside this script as NAME=value lines; the first
start makes it from sotc.ini.example):
    SOTC_GAME_DIR     the dumped game folder (the one with eboot.bin); default ../CUSA08809,
                      else ../CUSA08034
    SOTC_SYS_MODULES  optional: folder with YOUR OWN console's decrypted libSceNgs2.sprx and
                      libSceUlt.sprx (shadPS4's user/sys_modules), used instead of the port's own
                      sound library. These files are Sony firmware: they are never distributed
                      with the port.
    SOTC_USER_DIR     saves, shader/pipeline caches; default ./user
    SOTC_UPDATE_DIR   optional: the game update; by default <game>-UPDATE or <game>-patch next to
                      the dump (as shadPS4); its files take the place of the base game's
    SOTC_DLC_DIR      optional: the game's DLC (one folder per add-on with sce_sys/param.sfo,
                      or shadPS4's addcont folder holding CUSA08809 / CUSA08034); only DLC of the
                      game's own region is listed
    SOTC_MODES        optional, comma-separated: af16 (16x anisotropic filtering),
                      fps60 (removes the 30 FPS cap; experimental, vblank at 120 Hz),
                      fps60game (the game's own 60 FPS target, as the PS4 Pro Performance mode;
                      with SOTC_FPS_CAP=off),
                      res720/res810/res900/res960 (render below 1080p, the game scales up; a
                      render size of another shape, SOTC_RENDER_RES, takes their place);
                      res360/res540/res630 are the DLSS modes' sizes
    SOTC_UPSCALER     dlss: NVIDIA DLSS Super Resolution upscales the render (GeForce RTX; the
                      launcher sets SOTC_RENDER_RES to the output divided by the mode's factor)
    SOTC_PROBE_COMPAT 1 starts bb-probe-compat.exe (x86-64-v2), 0 bb-probe.exe (x86-64-v3); by
                      default the one the CPU runs
    SOTC_RENDER_RES   optional: the game renders at WxH instead of 1920x1080 (e.g. 2560x1080 for
                      21:9, a wider view); the output follows its aspect; PS4 mode only
    SOTC_CVARS        engine settings, NAME=VALUE or NAME(ARGS) separated by spaces (e.g.
                      r_motionBlurEnable=0)
    SOTC_EXTRA_CVARS  more engine settings after SOTC_CVARS, for one run (tools)
    SOTC_FOV          optional: the cameras' field of view in percent of the original (80 to 150)
    BB_GAMEPAD        the gamepad to use (its name or part of it), when several are connected
    key.<input>=Keys, pad.<input>=buttons   remap the controls (README.md, Controles)
The image is prepared again only when the game, the modules or the scripts change.
Extra arguments go to bb-probe.exe. Run from a console, the session is also saved to
<user>/last_run.log (the 4 before it as last_run.1.log ... last_run.4.log; a crash's log is
copied to <user>/crashes). An error exit prints bb-probe's exit code (the NTSTATUS, e.g.
0xc0000409) and, for a crash Windows ended itself, its Application Error event (ID 1000).
"""
import hashlib
import os
from pathlib import Path
import re
import shutil
import subprocess
import sys
import time

PORT = Path(__file__).resolve().parent
SCRIPTS = PORT / 'scripts'
SYS_MODULES = ('libSceUlt.sprx', 'libSceNgs2.sprx')
sys.path.insert(0, str(SCRIPTS))
# The supported title IDs (EU CUSA08809 first, US CUSA08034), the same eboot build.
from sotc_modes import GAMES, game_serial  # noqa: E402


def fail(message):
    print(message, file=sys.stderr)
    sys.exit(1)


def ini_file():
    """sotc.ini, made from the release's sotc.ini.example the first time: the zip no longer carries
    sotc.ini itself, which replaced the player's when extracted over an older copy (#136). The
    template itself when the port folder cannot be written."""
    ini, template = PORT / 'sotc.ini', PORT / 'sotc.ini.example'
    if not ini.exists() and template.is_file():
        try:
            shutil.copyfile(template, ini)
        except OSError:
            return template
    return ini


def load_ini():
    ini = ini_file()
    if ini.is_file():
        for line in ini.read_text(encoding='utf-8', errors='replace').splitlines():
            line = line.strip()
            if line and not line.startswith(('#', ';')) and '=' in line:
                name, value = line.split('=', 1)
                os.environ.setdefault(name.strip(), value.strip())


def no_console():
    """Started without a console (the launcher): console programs must not open their own."""
    if os.name != 'nt':
        return 0
    import ctypes
    return 0 if ctypes.windll.kernel32.GetConsoleWindow() else subprocess.CREATE_NO_WINDOW


def cpu_runs_x86_64_v3():
    """Whether the CPU has x86-64-v3 (AVX2, BMI1/2, FMA, F16C, LZCNT, MOVBE, YMM state saved by
    Windows), which bb-probe.exe is built for; None when CPUID cannot be run."""
    import ctypes
    kernel32 = ctypes.windll.kernel32
    kernel32.VirtualAlloc.restype = ctypes.c_void_p
    kernel32.VirtualAlloc.argtypes = (ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32, ctypes.c_uint32)
    kernel32.VirtualFree.argtypes = (ctypes.c_void_p, ctypes.c_size_t, ctypes.c_uint32)
    # cpuid(leaf, subleaf, u32 out[4]): push rbx; mov eax,ecx; mov ecx,edx; cpuid; mov [r8],eax;
    # mov [r8+4],ebx; mov [r8+8],ecx; mov [r8+12],edx; pop rbx; ret. At +24 xgetbv(0): xor ecx,ecx;
    # xgetbv; shl rdx,32; or rax,rdx; ret.
    code = bytes.fromhex('5389c889d10fa241890041895804418948084189500c5bc3' '31c90f01d048c1e2204809d0c3')
    memory = kernel32.VirtualAlloc(None, len(code), 0x3000, 0x40)  # commit + reserve, RWX
    if not memory:
        return None
    try:
        ctypes.memmove(memory, code, len(code))
        regs = (ctypes.c_uint32 * 4)()
        cpuid = ctypes.CFUNCTYPE(None, ctypes.c_uint32, ctypes.c_uint32, ctypes.c_void_p)(memory)
        xgetbv = ctypes.CFUNCTYPE(ctypes.c_uint64)(memory + 24)

        def leaf(number, sub=0):
            cpuid(number, sub, ctypes.addressof(regs))
            return tuple(regs)

        def bits(value, *positions):
            return all(value >> p & 1 for p in positions)

        top = leaf(0)[0]
        ext_top = leaf(0x80000000)[0]
        if top < 7 or ext_top < 0x80000001:
            return False
        ecx1 = leaf(1)[2]
        ebx7 = leaf(7)[1]
        ecx_ext = leaf(0x80000001)[2]
        # FMA, MOVBE, OSXSAVE, AVX, F16C; then BMI1, AVX2, BMI2; then LZCNT.
        if not (bits(ecx1, 12, 22, 27, 28, 29) and bits(ebx7, 3, 5, 8) and bits(ecx_ext, 5)):
            return False
        return xgetbv() & 6 == 6  # XMM and YMM state
    except OSError:
        return None
    finally:
        kernel32.VirtualFree(memory, 0, 0x8000)  # release


def find_probe():
    if os.name != 'nt':
        names = ['bb-probe']  # Linux: run.sh builds out/bb-probe
    else:
        # bb-probe-compat.exe (x86-64-v2) for older CPUs; SOTC_PROBE_COMPAT=1 or 0 forces the choice.
        forced = os.environ.get('SOTC_PROBE_COMPAT', '')
        v3 = forced != '1' if forced in ('0', '1') else cpu_runs_x86_64_v3()
        if v3 is None:
            import ctypes
            v3 = bool(ctypes.windll.kernel32.IsProcessorFeaturePresent(40))  # PF_AVX2_INSTRUCTIONS_AVAILABLE
        names = ['bb-probe.exe'] if v3 else ['bb-probe-compat.exe', 'bb-probe.exe']
    for name in names:
        for candidate in (PORT / 'bin' / name, PORT / 'out' / name):
            if candidate.is_file():
                if name == 'bb-probe-compat.exe':
                    print('Starting the x86-64-v2 build' + (' (SOTC_PROBE_COMPAT=1)' if forced == '1' else
                                                            ': CPU without x86-64-v3 (AVX2, BMI2, FMA)'))
                elif os.name == 'nt' and not v3:
                    print('bb-probe-compat.exe not found: this CPU lacks x86-64-v3 (AVX2, BMI2, FMA), '
                          'bb-probe.exe may stop at once (build.sh builds both unless BB_COMPAT=0)')
                return candidate
    fail(f'{names[-1]} not found: build it with build.sh (MSYS2 CLANG64 on Windows) or use a packaged build.')


def run_script(name, *args):
    # Inside the packaged SotC.exe (PyInstaller) the executable runs the scripts itself.
    script = ['--script'] if getattr(sys, 'frozen', False) else []
    result = subprocess.run([sys.executable, *script, str(SCRIPTS / name), *map(str, args)], stdin=subprocess.DEVNULL,
                            creationflags=no_console())
    if result.returncode:
        sys.exit(exit_value(result.returncode))


# Exit codes of a process Windows ended itself (a fault the port's own handler did not report).
NTSTATUS_NAMES = {0xC0000005: 'access violation', 0xC000001D: 'illegal instruction',
                  0xC00000FD: 'stack overflow', 0xC0000135: 'a DLL was not found',
                  0xC0000374: 'heap corruption', 0xC0000409: 'fail-fast / stack buffer overrun'}
PROBE_NAMES = ('bb-probe.exe', 'bb-probe-compat.exe')
# The data of an Application Error event in the order of the older unnamed form.
EVENT_FIELDS = ('AppName', 'AppVersion', 'AppTimeStamp', 'ModuleName', 'ModuleVersion', 'ModuleTimeStamp',
                'ExceptionCode', 'FaultingOffset')


def exit_value(code):
    """The sys.exit() value that ends this process with exit code `code`. On Windows a code from
    0x80000000 up goes as its signed 32-bit value: older Pythons exit with -1 (0xffffffff) for a
    larger int, which hid the NTSTATUS of crashes (0xc0000409 logged as 0xffffffff, #61)."""
    if os.name != 'nt':
        return code
    code &= 0xffffffff
    return code - (1 << 32) if code & 0x80000000 else code


def crash_event_line(xml):
    """One line from the XML of a Windows Application Error event when it is bb-probe's, else ''.
    Parsed with re: the packaged SotC.exe runs this script without xml.etree."""
    data = re.findall(r"<Data(?:\s+Name=['\"](\w+)['\"])?\s*(?:/>|>([^<]*)</Data>)", xml)
    values = {name or (EVENT_FIELDS[i] if i < len(EVENT_FIELDS) else ''): text
              for i, (name, text) in enumerate(data)}
    if values.get('AppName', '').lower() not in PROBE_NAMES:
        return ''
    when = re.search(r"SystemTime=['\"]([\d-]+)T([\d:]+)", xml)
    offset = values.get('FaultingOffset', '').lower().removeprefix('0x').lstrip('0') or '0'
    return (f"Windows Application Error (event 1000){f' at {when[1]} {when[2]} UTC' if when else ''}: "
            f"{values['AppName']}, module {values.get('ModuleName', '?')} {values.get('ModuleVersion', '')}, "
            f"exception 0x{values.get('ExceptionCode', '?').lower().removeprefix('0x')}, offset 0x{offset}")


def crash_event(seconds):
    """The newest Windows Application Error event (ID 1000) of bb-probe from the last `seconds`
    as one line, or ''. Best effort: any failure gives ''."""
    if os.name != 'nt':
        return ''
    try:
        # Not ctypes.wintypes: the packaged SotC.exe has only what the launcher imports.
        import ctypes
        from ctypes import byref, c_uint32 as DWORD, c_void_p, c_wchar_p, POINTER
        wevtapi = ctypes.WinDLL('wevtapi')
        wevtapi.EvtQuery.restype = c_void_p
        wevtapi.EvtQuery.argtypes = (c_void_p, c_wchar_p, c_wchar_p, DWORD)
        wevtapi.EvtNext.argtypes = (c_void_p, DWORD, POINTER(c_void_p), DWORD, DWORD, POINTER(DWORD))
        wevtapi.EvtRender.argtypes = (c_void_p, c_void_p, DWORD, DWORD, c_void_p, POINTER(DWORD), POINTER(DWORD))
        wevtapi.EvtClose.argtypes = (c_void_p,)
        query = ("*[System[Provider[@Name='Application Error'] and (EventID=1000) and "
                 f"TimeCreated[timediff(@SystemTime) <= {int(seconds * 1000)}]]]")
        # EvtQueryChannelPath | EvtQueryReverseDirection: the newest first.
        results = wevtapi.EvtQuery(None, 'Application', query, 0x1 | 0x200)
        if not results:
            return ''
        try:
            event, returned = c_void_p(), DWORD()
            while wevtapi.EvtNext(results, 1, byref(event), 1000, 0, byref(returned)):
                try:
                    # EvtRenderEventXml: the first call gives the size in bytes.
                    used, properties = DWORD(), DWORD()
                    wevtapi.EvtRender(None, event, 1, 0, None, byref(used), byref(properties))
                    buffer = ctypes.create_unicode_buffer(used.value // 2 + 1)
                    if wevtapi.EvtRender(None, event, 1, ctypes.sizeof(buffer), buffer, byref(used), byref(properties)):
                        line = crash_event_line(buffer.value)
                        if line:
                            return line
                finally:
                    wevtapi.EvtClose(event)
        finally:
            wevtapi.EvtClose(results)
    except Exception:  # noqa: BLE001 - diagnostics only, never a reason to fail
        pass
    return ''


def exit_report(code, seconds):
    """Lines for the session log after bb-probe ended with an error code: the code as Windows'
    unsigned NTSTATUS and, for a crash Windows handled itself (a fail-fast in ntdll leaves no
    ERROR line in the log: #61, #244, #291, #299, #309), its Application Error event, which
    Windows may write a moment after the exit."""
    if not code:
        return []
    if os.name != 'nt':
        return [f'bb-probe exited with code {code}']
    status = code & 0xffffffff
    name = NTSTATUS_NAMES.get(status)
    lines = [f'bb-probe exited with code {status:#010x}' + (f' ({name})' if name else '')]
    if status & 0x80000000:
        for attempt in range(4):
            if attempt:
                time.sleep(1)
            event = crash_event(seconds + 10 + attempt)
            if event:
                lines.append(event)
                break
    return lines


def fingerprint(game, modules):
    """Inputs of the prepared image: eboot, bundled modules, the user's modules, the scripts."""
    digest = hashlib.sha256()
    files = [game / 'eboot.bin', *sorted((game / 'sce_module').glob('*.prx')), *modules,
             *sorted(SCRIPTS.glob('*.py'))]
    for path in files:
        stat = path.stat()
        digest.update(f'{path.resolve()}|{stat.st_size}|{stat.st_mtime_ns}\n'.encode())
    return digest.hexdigest()


def is_game_patch(folder, serial):
    """True for an update of the game serial (CUSA08809 EU or CUSA08034 US): its param.sfo says
    that TITLE_ID and CATEGORY gp*."""
    try:
        from prepare import sfo
        values = sfo((folder / 'sce_sys' / 'param.sfo').read_bytes())
    except (OSError, ValueError, ImportError):
        return False
    return values.get('TITLE_ID') == serial and str(values.get('CATEGORY', '')).startswith('gp')


def game_update(game):
    """The game update next to the dump, as shadPS4 uses it (<game>-UPDATE or <game>-patch), else
    an update or patch folder in the dump or next to it that is a patch of the game (#66), or
    SOTC_UPDATE_DIR; None without one."""
    named = os.environ.get('SOTC_UPDATE_DIR')
    if named:
        return Path(named).resolve()
    for suffix in ('-UPDATE', '-patch'):
        folder = game.with_name(game.name + suffix)
        if folder.is_dir():
            return folder
    serial = game_serial(game)
    for base in (game, game.parent):
        for name in ('update', 'patch'):
            folder = base / name
            if folder.is_dir() and is_game_patch(folder, serial):
                return folder
    return None


def merged_source(game, update, out):
    """The files the image is prepared from when an update is present: its eboot.bin, the base
    modules with the update's on top, its param.sfo (out/app-update, refreshed when they change)."""
    merged = out / 'app-update'
    pairs = [(update / 'eboot.bin', merged / 'eboot.bin')]
    for folder in (game / 'sce_module', update / 'sce_module'):
        if folder.is_dir():
            pairs += [(p, merged / 'sce_module' / p.name) for p in folder.iterdir() if p.is_file()]
    for folder in (game / 'sce_sys', update / 'sce_sys'):
        if (folder / 'param.sfo').is_file():
            pairs.append((folder / 'param.sfo', merged / 'sce_sys' / 'param.sfo'))
    for source, target in pairs:
        stat = source.stat()
        current = target.stat() if target.is_file() else None
        if not current or current.st_size != stat.st_size or current.st_mtime_ns != stat.st_mtime_ns:
            target.parent.mkdir(parents=True, exist_ok=True)
            shutil.copy2(source, target)
    return merged


def engine_cvars(env):
    """The engine cvars for sotc_modes.py: SOTC_CVARS (the launcher's settings) or, without it,
    the culling settings of Pink-shadPS4 (same image, fewer GPU waits), then SOTC_EXTRA_CVARS,
    which tools add for one run without touching the settings (tabnul, PR #345)."""
    cvars = env.get('SOTC_CVARS')
    if cvars is None:
        cvars = 'doIndexBufferCulling=0 doSunShadowIndexBufferCulling=0'
    return ' '.join(filter(None, (cvars.strip(), env.get('SOTC_EXTRA_CVARS', '').strip())))


def main():
    load_ini()
    env = os.environ
    os.chdir(PORT)
    out = PORT / 'out'
    out.mkdir(exist_ok=True)
    # The EU dump (CUSA08809) next to the port, else the US one (CUSA08034).
    default = next((PORT.parent / g for g in GAMES if (PORT.parent / g / 'eboot.bin').is_file()),
                   PORT.parent / next(iter(GAMES)))
    game = Path(env.get('SOTC_GAME_DIR', default)).resolve()
    if not (game / 'eboot.bin').is_file():
        fail(f'No eboot.bin in {game}: set SOTC_GAME_DIR (environment or sotc.ini) to the CUSA08809 (EU) '
             'or CUSA08034 (US) dump.')

    modules = []
    sys_modules = env.get('SOTC_SYS_MODULES')
    if sys_modules:
        modules = [Path(sys_modules) / name for name in SYS_MODULES]
        missing = [m.name for m in modules if not m.is_file()]
        if missing:
            print(f'SOTC_SYS_MODULES: {", ".join(missing)} not found in {sys_modules}; '
                  "the port's own sound library is used", file=sys.stderr)
            modules = []

    # A complete update (shadPS4's <game>-UPDATE): the image comes from its eboot, and its files
    # take the place of the base game's while the game runs (src/runtime_file.c).
    update = game_update(game)
    for folder in (game, update):
        if folder and (folder / 'eboot.bin').is_file() and (folder / 'eboot.bin').stat().st_size == 0:
            fail(f'{folder / "eboot.bin"} is empty (0 bytes): the copy was interrupted. Copy the file again.')
    source = game
    if update:
        print(f'Game update: {update}')
        env['SOTC_UPDATE_DIR'] = str(update)
        if (update / 'eboot.bin').is_file():
            source = merged_source(game, update, out)
    stamp = out / 'prepared.txt'
    wanted = fingerprint(source, modules)
    images = [out / name for name in ('boot-linked.bin', 'content.bin', 'sotc-cpu.bin')]
    if not stamp.is_file() or stamp.read_text().strip() != wanted or not all(p.is_file() for p in images):
        print('Preparing the game image...')
        run_script('prepare.py', source, '--out', out)
        link = ['link_modules.py', source, '--out', out]
        if modules:
            link += ['--sys-modules', modules[0].parent]
        run_script(*link)
        run_script('content_profile.py', source, '--out', out)
        run_script('sotc_cpu.py', source, '--out', out)
        stamp.write_text(wanted + '\n')

    modes = env.get('SOTC_MODES', '')
    cvars = engine_cvars(env)
    if env.get('SOTC_NOTE'):
        print(f"sotc: {env['SOTC_NOTE']}", file=sys.stderr)
    render = env.get('SOTC_RENDER_RES', '').strip()
    if render == '1920x1080':
        # A 16:9 output above 1080p renders at 1080p as before: no patches, no extra memory.
        render = ''
    if render:
        env['SOTC_RENDER_RES'] = render  # the GPU library reads it (bilinear 2D art above 1080p)
    else:
        env.pop('SOTC_RENDER_RES', None)
    fov = env.get('SOTC_FOV', '').strip().rstrip('%')
    if fov and not (fov.isdigit() and 80 <= int(fov) <= 150):
        print(f'sotc: SOTC_FOV={fov} ignored (a percentage from 80 to 150)', file=sys.stderr)
        fov = ''
    run_script('sotc_modes.py', source, '--out', out, '--modes', modes, '--cvars', cvars,
               *(['--render', render] if render else []), *(['--fov', fov] if fov else []))
    if render:
        # The output keeps the render size's aspect (a 16:9 output would stretch a 21:9 image).
        try:
            rw, rh = map(int, render.split('x'))
            ow, oh = map(int, env.get('SOTC_OUTPUT_RES', '').split('x'))
            same_aspect = abs(ow * rh - oh * rw) <= oh * rw // 100
        except ValueError:
            same_aspect = False
        if not same_aspect:
            env['SOTC_OUTPUT_RES'] = render
        # Larger render targets: the game's heaps take what direct memory there is (run.sh).
        env.setdefault('BB_DMEM_MB', '9152')
    # The game's direct memory allocator (a 64 KiB page bitmap over all of it) filled up and read
    # past the bitmap (guest offset 0x1a371, read of -1): in PS4 Pro mode with 5696 MiB when a save
    # loaded, and in PS4 mode with 5056 MiB on loading the last colossus' area (user logs, 19
    # Oct; high quality textures keep more fine mips resident than on a PS4). Every mode gets the
    # 9152 MiB the wide render sizes already used.
    env.setdefault('BB_DMEM_MB', '9152')
    # The game waits for vblanks: at 60 Hz an uncapped frame (fps60, or F7 in game) still rounds
    # up to 30 FPS. With the cap, 120 Hz only shows a late frame sooner (and SOTC_FPS_LIMIT counts
    # in 120 Hz vblanks).
    env.setdefault('BB_VBLANK_HZ', '120')

    probe = find_probe()
    # Development builds run from the MSYS2 tree: its CLANG64 DLLs (SDL3, FFmpeg, ...).
    if os.name == 'nt' and not (probe.parent / 'SDL3.dll').is_file():
        clang64 = Path(env.get('MSYS2_ROOT', r'C:\msys64')) / 'clang64' / 'bin'
        if clang64.is_dir():
            env['PATH'] = f'{clang64}{os.pathsep}{env.get("PATH", "")}'
    user = Path(env.get('SOTC_USER_DIR', PORT / 'user')).resolve()
    user.mkdir(parents=True, exist_ok=True)
    env.setdefault('BB_GPU_USER_DIR', str(user))
    # Controls: key.<input>= / pad.<input>= lines of sotc.ini remap keys and buttons (src/runtime_pad.c).
    env.setdefault('BB_CONFIG', str(PORT / 'sotc.ini'))
    command = [str(probe), str(out / 'boot-linked.bin'), '--content-profile', str(out / 'content.bin'),
               '--sotc-cpu', str(out / 'sotc-cpu.bin'), '--patches', str(out / 'patches.bin'),
               *(['--sotc-cmdline', str(out / 'cmdline.bin')] if (out / 'cmdline.bin').is_file() else []),
               '--app0', str(game), '--user', str(user), *sys.argv[1:]]
    # SOTC_WRAP: a JSON list put in front of the game, e.g. RenderDoc
    # ["C:/Program Files/RenderDoc/renderdoccmd.exe", "capture", "-w", "-c", "<file>"]; F12 then
    # captures the frame (SOTC_RDOC_SUBMITS submissions).
    if env.get('SOTC_WRAP'):
        import json
        command = json.loads(env['SOTC_WRAP']) + command
    # In a console the session also goes to user/last_run.log (as the launcher does; the launcher
    # reads this script's output into its own log, so a pipe means it is not written here).
    started = time.time()
    if not sys.stdout.isatty():
        code = subprocess.run(command, stdin=subprocess.DEVNULL, creationflags=no_console()).returncode
        for line in exit_report(code, time.time() - started):
            print(line, flush=True)
        return code
    with open_session_log(user) as log:
        game = subprocess.Popen(command, stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                stderr=subprocess.STDOUT)
        try:
            for line in game.stdout:
                sys.stdout.buffer.write(line)
                sys.stdout.buffer.flush()
                log.write(line)
        finally:
            code = game.wait()
        for line in exit_report(code, time.time() - started):
            print(line, flush=True)
            log.write(f'{line}\n'.encode())
    keep_crash_log(user, code)
    return code


LOGS_KEPT = 5        # last_run.log and last_run.1.log ... last_run.4.log
CRASH_LOGS_KEPT = 20


def open_session_log(user):
    """A fresh user/last_run.log; the previous sessions' logs move to last_run.1.log (the one
    before) ... last_run.4.log. Same files as the launcher (launcher/sotc_launcher_win.py)."""
    for i in range(LOGS_KEPT - 1, 0, -1):
        older = user / ('last_run.log' if i == 1 else f'last_run.{i - 1}.log')
        if older.is_file():
            try:
                os.replace(older, user / f'last_run.{i}.log')
            except OSError:
                pass
    return open(user / 'last_run.log', 'wb', buffering=0)


def keep_crash_log(user, code):
    """After a session that ended with an error code, copies its log to
    user/crashes/<date>_<time>_<code>.log (the last 20, and the last 5 minidumps, are kept)."""
    if code == 0:
        return
    crashes = user / 'crashes'
    try:
        crashes.mkdir(parents=True, exist_ok=True)
        stamp = time.strftime('%Y-%m-%d_%H-%M-%S')
        shutil.copy2(user / 'last_run.log', crashes / f'{stamp}_{code & 0xffffffff:#x}.log')
        for old in sorted(crashes.glob('*.log'))[:-CRASH_LOGS_KEPT]:
            old.unlink()
        for old in sorted(crashes.glob('*.dmp'))[:-5]:
            old.unlink()
    except OSError:
        pass


if __name__ == '__main__':
    try:
        sys.exit(exit_value(main()))
    except KeyboardInterrupt:
        sys.exit(130)
