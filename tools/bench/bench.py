#!/usr/bin/env python3
"""Benchmark driver for a built SotC-PC folder, optionally emulating a weaker PC.

    python tools/bench/bench.py NAME [--scenario attract|shrine|newgame] [--boss N] [--seconds 300]
                         [--cores 4] [--vram 2048] [--env NAME=VALUE ...] [--modes af16] [--cvars "..."]

Paths (environment): SOTC_BENCH_PORT, the built port (default dist/SotC-PC from
`build.cmd -Package`); SOTC_GAME_DIR, the game dump (default ../CUSA08809 next to the repository),
SOTC_UPDATE_DIR (default <game>-UPDATE); SOTC_BENCH_DIR, where its saves, logs and captures go
(default tools/bench/work); RENDERDOC_DIR for --rdoc. The scenarios start from your own saves in
<bench dir>/templates/profile/savedata (a profile with the language chosen) and
<bench dir>/templates/shrine/savedata (a save at the Shrine of Worship): no game data is shipped.

- Restores the save template into its own user folder (never the player's), keeps the caches.
- Starts bb-probe.exe suspended, pins it to --cores physical cores (one logical CPU each, like an
  i5-7300HQ without SMT), then resumes it.
- --vram N sets SOTC_VRAM_LIMIT_MB=N (emulated card size).
- Scenarios: attract (title-screen attract demo, no input), newgame (new game up to Wander at the
  shrine, then idle and a short walk).
- Writes logs/bench_NAME.log and prints a summary from the SOTC FPS lines.
"""
import argparse
import ctypes
import os
import re
import shutil
import subprocess
import sys
import threading
import time
from pathlib import Path

REPO = Path(__file__).resolve().parents[2]
H = Path(os.environ.get('SOTC_BENCH_DIR') or REPO / 'tools' / 'bench' / 'work')
H.mkdir(parents=True, exist_ok=True)
GAME_PORT = Path(os.environ.get('SOTC_BENCH_PORT') or REPO / 'dist' / 'SotC-PC')
GAME_DIR = Path(os.environ.get('SOTC_GAME_DIR') or REPO.parent / 'CUSA08809')
UPDATE_DIR = Path(os.environ.get('SOTC_UPDATE_DIR') or str(GAME_DIR) + '-UPDATE')
PY = Path(sys.executable)
FPS_LINE = re.compile(r'SOTC FPS ([\d.]+) at (\d+)s; (\d+) cmds/frame')
THR_LINE = re.compile(r'draw busy ([\d.]+) ms, command parse ([\d.]+) ms')
THR_FULL = re.compile(r'draw busy ([\d.]+) ms, command parse ([\d.]+) ms, GPU busy ([\d.]+) ms')
FRAMES_LINE = re.compile(r'SOTC FRAMES p50 ([\d.]+) ms, p99 ([\d.]+) ms .*?worst ([\d.]+) ms')

CREATE_SUSPENDED = 0x4


def start_suspended(command, env, log, mask):
    process = subprocess.Popen(command, env=env, stdout=log, stderr=subprocess.STDOUT,
                               stdin=subprocess.DEVNULL, creationflags=CREATE_SUSPENDED,
                               cwd=str(GAME_PORT))
    handle = int(process._handle)
    if mask:
        ctypes.windll.kernel32.SetProcessAffinityMask(handle, ctypes.c_size_t(mask))
    ctypes.windll.ntdll.NtResumeProcess(handle)
    return process


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument('name')
    ap.add_argument('--scenario', default='attract')
    ap.add_argument('--seconds', type=float, default=300)
    ap.add_argument('--cores', type=int, default=0, help='0 = all logical CPUs')
    ap.add_argument('--vram', type=int, default=0, help='emulated MB, 0 = real card')
    ap.add_argument('--env', action='append', default=[])
    ap.add_argument('--modes', default='af16')
    ap.add_argument('--cvars', default='doIndexBufferCulling=0 doSunShadowIndexBufferCulling=0 nointro attractMovieEnabled=0')
    ap.add_argument('--res', default='1920x1080')
    ap.add_argument('--fresh-cache', action='store_true', help='delete the shader cache first')
    ap.add_argument('--shots', default='', help='comma list of seconds for frame dumps')
    ap.add_argument('--attract-skip-at', type=float, default=45)
    ap.add_argument('--title-presses', type=int, default=6)
    ap.add_argument('--watch-intro', type=float, default=0, help='seconds of intro before skipping')
    ap.add_argument('--idle', type=float, default=40, help='seconds idle at the shrine')
    ap.add_argument('--rdoc', type=float, default=0, help='RenderDoc capture N s into gameplay')
    ap.add_argument('--gshot', type=float, default=0, help='frame dump N s into gameplay')
    ap.add_argument('--boss', type=int, default=0,
                    help='boss scenario: the shrine save jumps to that colossus (no damage), then runs '
                         'around with the camera turning for --idle seconds')
    ap.add_argument('--smi', action='store_true', help='log GPU clocks with nvidia-smi')
    args = ap.parse_args()
    if args.boss:
        # The game's debug cvars: jump to the colossus' arena; the player takes no damage (the
        # colossus keeps its AI: frozen ones do not appear).
        args.scenario = 'shrine'
        args.cvars += f' shadow_jumpToBoss={args.boss} shadow_noDamage=1'
    if args.rdoc and 'gpuMarkerLevel' not in args.cvars:
        args.cvars += ' gpuMarkerLevel=3'  # the engine's GPU markers name the passes

    # Its own user folder: the player's saves, settings and cache in SotC-PC/user stay untouched.
    user = H / 'bench_user'
    user.mkdir(exist_ok=True)
    target = user / 'savedata'
    shutil.rmtree(target, ignore_errors=True)
    template = H / 'templates' / ('shrine' if args.scenario == 'shrine' else 'profile') / 'savedata'
    if not template.is_dir():
        sys.exit(f'No save template in {template} (see the docstring)')
    shutil.copytree(template, target)
    if args.fresh_cache:
        shutil.rmtree(user / 'cache', ignore_errors=True)
    out = GAME_PORT / 'out'
    # Patches and engine command line for the chosen modes (as sotc.py does on every launch).
    source = out / 'app-update' if (out / 'app-update' / 'eboot.bin').is_file() else GAME_DIR
    subprocess.run([str(PY), str(GAME_PORT / 'scripts' / 'sotc_modes.py'), str(source), '--out', str(out),
                    '--modes', args.modes, '--cvars', args.cvars], check=True, stdout=subprocess.DEVNULL)

    pad = H / 'pad_bench.txt'
    pad.write_text('')
    trigger = H / 'shot_bench.trigger'
    env = dict(os.environ)
    env.update(SOTC_GAME_DIR=str(GAME_DIR), SOTC_UPDATE_DIR=str(UPDATE_DIR),
               BB_PAD_FILE=str(pad), BB_FRAME_DUMP_TRIGGER=str(trigger), SOTC_FPS='1',
               SOTC_SAVE_PROMPT='auto', BB_FULLSCREEN='0', SOTC_OUTPUT_RES=args.res,
               BB_GPU_USER_DIR=str(user), BB_CONFIG=str(GAME_PORT / 'sotc.ini'),
               SOTC_SHADER_WINDOW='1', BB_LANGUAGE=os.environ.get('BB_LANGUAGE', '1'))
    if args.vram:
        env['SOTC_VRAM_LIMIT_MB'] = str(args.vram)
    for item in args.env:
        k, v = item.split('=', 1)
        env[k] = v
    command = [str(GAME_PORT / 'bin' / 'bb-probe.exe'), str(out / 'boot-linked.bin'),
               '--content-profile', str(out / 'content.bin'), '--sotc-cpu', str(out / 'sotc-cpu.bin'),
               '--patches', str(out / 'patches.bin'), '--sotc-cmdline', str(out / 'cmdline.bin'),
               '--app0', str(GAME_DIR), '--user', str(user)]
    mask = 0
    if args.cores:
        for i in range(args.cores):
            mask |= 1 << (2 * i)
    subprocess.run(['taskkill', '/F', '/IM', 'bb-probe.exe'], capture_output=True)
    logs = H / 'logs'
    logs.mkdir(exist_ok=True)
    log_path = logs / f'bench_{args.name}.log'
    log = open(log_path, 'wb')
    if args.rdoc:
        # RenderDoc (portable, in build/dl): its Vulkan layer through the environment, the
        # capture of SOTC_RDOC_SUBMITS submissions when the trigger file appears.
        rd_dir = Path(os.environ.get('RENDERDOC_DIR', ''))
        # The Vulkan loader loads renderdoc.dll as a layer at instance creation (no injection:
        # bb-probe reserves the PS4 address range first).
        # Whole frames at flips; one thread records everything in order (no draw thread, no
        # recorder thread); the engine's GPU markers name the passes.
        env.update(VK_LAYER_PATH=str(rd_dir), VK_INSTANCE_LAYERS='VK_LAYER_RENDERDOC_Capture',
                   ENABLE_VULKAN_RENDERDOC_CAPTURE='1', SOTC_RDOC_TRIGGER=str(H / 'rdoc.trigger'),
                   SOTC_RDOC_FRAMES='1', SOTC_FLUSH_EVERY='0', SOTC_CP_THREADS='0',
                   SOTC_RECORD_THREAD='0', BB_VK_MARKERS='1',
                   SOTC_RDOC_PATH=str(H / 'captures' / args.name))

        (H / 'captures').mkdir(exist_ok=True)
    smi = None
    if args.smi:
        smi_log = open(logs / f'smi_{args.name}.csv', 'w')
        smi = subprocess.Popen(['nvidia-smi', '--query-gpu=clocks.gr,utilization.gpu,power.draw',
                                '--format=csv,noheader,nounits', '-l', '1'], stdout=smi_log,
                               stderr=subprocess.DEVNULL)
    process = start_suspended(command, env, log, mask)
    t0 = time.monotonic()
    state = {'stop': False, 'starts': 0, 'gameplay_at': None}

    def text():
        try:
            return log_path.read_text(encoding='utf-8', errors='replace')
        except OSError:
            return ''

    def alive():
        return process.poll() is None and not state['stop'] and time.monotonic() - t0 < args.seconds

    def wait(t):
        while alive() and time.monotonic() - t0 < t:
            time.sleep(0.05)
        return alive()

    def press(what, hold=0.25):
        pad.write_text(what)
        time.sleep(hold)
        pad.write_text('')
        time.sleep(0.15)

    def shot(timeout=10):
        shots_dir = user / 'screenshots'
        before = set(shots_dir.glob('*.png')) if shots_dir.is_dir() else set()
        trigger.touch()
        end = time.monotonic() + timeout
        while time.monotonic() < end and alive():
            now = set(shots_dir.glob('*.png')) if shots_dir.is_dir() else set()
            new = now - before
            if new:
                time.sleep(1.5)
                return max(new, key=lambda p: p.stat().st_mtime)
            time.sleep(0.05)
        return None

    def cursor_row():
        from PIL import Image
        path = shot()
        if not path:
            return -1
        im = Image.open(path).convert('L').resize((960, 540))
        rows = [237, 259, 281, 304]

        def mean(x0, x1, y):
            vals = [im.getpixel((x, yy)) for x in range(x0, x1) for yy in range(y - 3, y + 3)]
            return sum(vals) / len(vals)
        # The arrow stands out from the background next to it, on dark and bright scenes alike.
        contrast = [abs(mean(152, 158, y) - mean(128, 140, y)) for y in rows]
        best = max(range(4), key=lambda i: contrast[i])
        others = sorted(contrast)[-2]
        return best if contrast[best] > 20 and others < contrast[best] * 0.5 else -1

    def menu_to(row):
        for _ in range(10):
            r = cursor_row()
            if r == -1:
                return False
            if r == row:
                press('cross')
                return True
            press('down')
            time.sleep(1.2)
        return False

    def say(msg):
        print(f'[{time.monotonic() - t0:6.1f}s] {msg}', flush=True)

    def skip_via_pause_menu():
        """Options, then the last pause-menu row ("skip cutscene"). False: no menu came up."""
        press('options')
        time.sleep(2.5)
        if cursor_row() == -1:
            return False
        return menu_to(3)

    def newgame():
        # Logos, then the attract demo: options -> pause menu -> "skip" (last row) -> title.
        if not wait(args.attract_skip_at):
            return
        while alive() and not skip_via_pause_menu():
            say('no pause menu yet')
            wait(time.monotonic() - t0 + 5)
        say('attract skipped')
        # Title: any button, then "New game", then difficulty (Normal is preselected).
        # Title (any button) -> main menu (New game) -> difficulty (Normal) -> confirmation.
        for _ in range(args.title_presses):
            if not alive():
                return
            wait(time.monotonic() - t0 + 3)
            press('cross')
        say('new game loading')
        if args.watch_intro:
            # Harvest: the whole opening cinematic plays (its shaders are recorded too). Nothing is
            # pressed afterwards: once the cinematic is over, the pause menu's last row is "quit
            # to the main menu", not "skip".
            wait(time.monotonic() - t0 + args.watch_intro)
            state['gameplay_at'] = time.monotonic() - t0
            say('intro watched')
            return
        # The opening cinematic: options -> skip (last row), until gameplay starts.
        wait(time.monotonic() - t0 + 10)
        skipped_at = None
        while alive() and state['gameplay_at'] is None:
            if text().count('st_gameflow() starting game') >= 2:
                state['gameplay_at'] = time.monotonic() - t0
                break
            if skipped_at is None:
                if skip_via_pause_menu():
                    skipped_at = time.monotonic()
                    say('intro skipped')
                else:
                    wait(time.monotonic() - t0 + 4)
            elif time.monotonic() - skipped_at > 25:
                state['gameplay_at'] = time.monotonic() - t0
            else:
                wait(time.monotonic() - t0 + 1)
        say('gameplay')
        # Idle at the shrine, then walk forward for a while (left stick up).
        if not wait(state['gameplay_at'] + args.idle):
            return
        say('walking')
        pad.write_text('ly=0')
        wait(state['gameplay_at'] + args.idle + 30)
        pad.write_text('')
        say('walk done')

    def letterboxed():
        """True when the frame has the black bars of a cinematic (top and bottom, picture between)."""
        from PIL import Image
        path = shot()
        if not path:
            return False
        im = Image.open(path).convert('L').resize((192, 108))
        def band(y0, y1):
            vals = [im.getpixel((x, y)) for x in range(0, 192, 2) for y in range(y0, y1)]
            return sum(vals) / len(vals)
        return band(1, 9) < 3 and band(99, 107) < 3 and band(40, 68) > 10

    def skip_cinematics(rounds=4):
        """Skips the colossus' opening cinematic (every colossus has one) by holding cross, the
        game's hold-to-skip, while the frame is letterboxed."""
        for _ in range(rounds):
            if not alive() or not letterboxed():
                return
            pad.write_text('cross')
            wait(time.monotonic() - t0 + 3.5)
            pad.write_text('')
            say('cinematic skipped')
            wait(time.monotonic() - t0 + 2)

    # Boss scenario: look around, run with the camera turning, look up; cinematics skipped.
    BOSS_MOVES = [(5, 'rx=255'), (7, 'ly=0'), (6, 'ly=0 rx=190'), (5, 'ry=200 rx=60'), (7, 'ly=0 lx=40'),
                  (5, 'rx=0 ry=170'), (7, 'ly=0 rx=70'), (5, 'lx=0 rx=255')]

    def boss_moves():
        end = state['gameplay_at'] + args.idle
        step = 0
        while alive() and time.monotonic() - t0 < end:
            if step % 3 == 0:
                skip_cinematics()
            if step == 2 and args.gshot:
                path = shot()
                if path:
                    shutil.copy(path, H / 'logs' / f'gshot_{args.name}.png')
                    say('shot taken')
            seconds, move = BOSS_MOVES[step % len(BOSS_MOVES)]
            pad.write_text(move)
            wait(min(time.monotonic() - t0 + seconds, end))
            step += 1
        pad.write_text('')
        say('boss moves done')

    def shrine():
        # A save at the Shrine of Worship (shrine-template): skip the attract demo, then
        # "press any button" and "Continue"; Wander stands at the shrine once it has loaded.
        # +nointro +attractMovieEnabled=0: no logos, no attract demo; the title comes up at once.
        while alive() and not FPS_LINE.search(text()):
            wait(time.monotonic() - t0 + 1)
        wait(time.monotonic() - t0 + 4)
        say('title')
        # Title (any button), then "Continue" (preselected) until the save starts loading.
        for _ in range(args.title_presses):
            if not alive():
                return
            fps = [int(m.group(3)) for m in FPS_LINE.finditer(text())]
            if fps and fps[-1] > 1500:
                break
            press('cross')
            wait(time.monotonic() - t0 + 2.5)
        say('continue pressed')
        seen_at_continue = len(FPS_LINE.findall(text()))
        # Loading ends when the draw count settles at the shrine level.
        while alive():
            fps = [(int(m.group(2)), int(m.group(3))) for m in FPS_LINE.finditer(text())]
            recent = [c for s, c in fps[-3:]]
            # A colossus' arena loads into its cinematic: moving starts at the first busy sample.
            if args.boss and fps and fps[-1][1] > 2000 and len(fps) > seen_at_continue:
                state['gameplay_at'] = time.monotonic() - t0
                break
            if len(recent) >= 3 and min(recent) > 3000 and len(fps) > seen_at_continue + 2:
                state['gameplay_at'] = time.monotonic() - t0
                break
            wait(time.monotonic() - t0 + 2)
        if state['gameplay_at'] is None:
            return
        say('gameplay')
        if args.boss:
            boss_moves()
            return
        if args.gshot:
            if not wait(state['gameplay_at'] + args.gshot):
                return
            path = shot()
            if path:
                shutil.copy(path, H / 'logs' / f'gshot_{args.name}.png')
                say('shot taken')
        if args.rdoc:
            if not wait(state['gameplay_at'] + args.rdoc):
                return
            (H / 'rdoc.trigger').touch()
            say('renderdoc capture requested')
        if not wait(state['gameplay_at'] + args.idle):
            return
        say('walking')
        pad.write_text('ly=0')
        wait(state['gameplay_at'] + args.idle + 30)
        pad.write_text('')
        say('walk done')

    if args.scenario == 'newgame':
        threading.Thread(target=newgame, daemon=True).start()
    elif args.scenario == 'shrine':
        threading.Thread(target=shrine, daemon=True).start()
    shot_times = [float(s) for s in args.shots.split(',') if s]

    def shots():
        for st in shot_times:
            if not wait(st):
                return
            shot()
    threading.Thread(target=shots, daemon=True).start()

    while process.poll() is None and time.monotonic() - t0 < args.seconds:
        time.sleep(0.5)
    state['stop'] = True
    subprocess.run(['taskkill', '/F', '/IM', 'bb-probe.exe'], capture_output=True)
    if smi:
        smi.kill()
    process.wait()
    log.close()

    t = text()
    fps = [(float(m.group(1)), int(m.group(2)), int(m.group(3))) for m in FPS_LINE.finditer(t)]
    thr = [(float(m.group(1)), float(m.group(2))) for m in THR_LINE.finditer(t)]
    print(f'{args.name}: {len(fps)} samples, rc={process.returncode}')
    print(' '.join(f'{f:.0f}@{s}' for f, s, _ in fps))
    meas = [f for f, s, _ in fps if s >= 60]
    if meas:
        print(f'avg {sum(meas)/len(meas):.1f} FPS, min {min(meas):.1f} (from 60 s, {len(meas)} samples)')
    if state['gameplay_at']:
        g = state['gameplay_at']
        print(f'gameplay at {g:.0f}s')
        # Idle window at the shrine: FPS, draws and the per-frame busy times of the threads.
        rows = []
        lines = t.splitlines()
        for i, line in enumerate(lines):
            m = FPS_LINE.search(line)
            if not m or not (g + 8 <= int(m.group(2)) <= g + args.idle):
                continue
            busy = next((THR_FULL.search(l) for l in lines[i + 1:i + 12] if THR_FULL.search(l)), None)
            if busy:
                rows.append((float(m.group(1)), int(m.group(3)), float(busy.group(1)),
                             float(busy.group(2)), float(busy.group(3))))
        if rows:
            n = len(rows)
            avg = [sum(r[k] for r in rows) / n for k in range(5)]
            print(f'{"ACTIVE" if args.boss else "IDLE"}: {avg[0]:.1f} FPS, {avg[1]:.0f} cmds/frame, draw busy {avg[2]:.1f} ms, '
                  f'parse {avg[3]:.1f} ms, GPU {avg[4]:.1f} ms ({n} samples)')
            if args.smi:
                # nvidia-smi samples once a second from the start: the same idle window.
                rows_smi = [l.split(',') for l in (logs / f'smi_{args.name}.csv').read_text().splitlines() if l.count(',') == 2]
                win = rows_smi[int(g + 8):int(g + args.idle)]
                if win:
                    clk = sum(float(r[0]) for r in win) / len(win)
                    util = sum(float(r[1]) for r in win) / len(win)
                    print(f'GPU clock {clk:.0f} MHz, util {util:.0f}%: {avg[4] * clk / 2100:.1f} ms at 2100 MHz')
    crashes = [l for l in t.splitlines() if re.search(r'Assertion|UNREACHABLE|fatal|Unhandled', l)]
    for l in crashes[:5]:
        print('!!', l[:200])


if __name__ == '__main__':
    main()
