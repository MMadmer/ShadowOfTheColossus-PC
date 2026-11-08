#!/usr/bin/env python3
# SPDX-License-Identifier: GPL-2.0-or-later
"""Shadow of the Colossus launcher for Windows (Tkinter), derived from the bbport launcher.

One window for the port's settings: the game dump, the user's own sound modules, saves, modes
(16x AF, 60 FPS), display and diagnostics. They go to sotc.py as environment variables.
Launcher options live in user/launcher_settings.json of the port folder.

Frozen with PyInstaller (packaging/windows/package_sotc.sh) the same SotC.exe also runs the
game without the window (`--play`), sotc.py (`--run`) and the preparation scripts (`--script`),
so a packaged port needs no Python installation.
"""
import ctypes
import json
import math
import os
import platform
from pathlib import Path
import queue
import re
import runpy
import shutil
import subprocess
import sys
import threading
import time
import zipfile

from sotc_i18n import translate
import sotc_frame
import sotc_hardware
import sotc_pad
import sotc_search
import sotc_theme as theme
import sotc_update

FROZEN = getattr(sys, 'frozen', False)
PORT_DIR = Path(sys.executable).resolve().parent if FROZEN else Path(__file__).resolve().parent.parent
sys.path.insert(0, str(PORT_DIR / 'scripts'))
# Settings of the copy the launcher runs from, in its user folder (kept by updates): each copy
# of the port has its own saves, caches and logs. Before 0.59.2 every copy shared
# %APPDATA%\sotc-launcher\settings.json, so a new copy went on using the data folder of the one
# set up first.
LEGACY_CONFIG_DIR = Path(os.environ.get('APPDATA', Path.home())) / 'sotc-launcher'
LEGACY_CONFIG_FILE = LEGACY_CONFIG_DIR / 'settings.json'
CONFIG_DIR = PORT_DIR / 'user'
CONFIG_FILE = CONFIG_DIR / 'launcher_settings.json'
# The settings file in use: the port's, or the shared one while the port folder cannot be written
# (Program Files). The game writes the in-game menu's changes into it.
settings_in_use = CONFIG_FILE
# The supported releases (title ID -> region, version) share one eboot build; scripts/ is on
# sys.path above. The EU one (CUSA08809) is the default title ID.
from sotc_modes import GAMES  # noqa: E402
GAME_ID = next(iter(GAMES))
# The default game folder: the EU dump next to the port folder, else the US one (as sotc.py).
DEFAULT_GAME_DIR = next((PORT_DIR.parent / g for g in GAMES if (PORT_DIR.parent / g / 'eboot.bin').is_file()),
                        PORT_DIR.parent / GAME_ID)
SYS_MODULES = ('libSceUlt.sprx', 'libSceNgs2.sprx')
VERSION = '1.3.0'
MAX_LOG_LINES = 6000
NO_WINDOW = getattr(subprocess, 'CREATE_NO_WINDOW', 0)


# ---------------------------------------------------------------------------------------------
# Command line roles of the frozen executable.

def attach_stdio():
    """A windowed executable starts without sys.stdout; inherited pipes or a console still exist."""
    import msvcrt
    for name, std in (('stdout', -11), ('stderr', -12)):
        if getattr(sys, name) is not None:
            continue
        stream = None
        handle = ctypes.windll.kernel32.GetStdHandle(std)
        if handle and handle != ctypes.c_void_p(-1).value:
            try:
                stream = open(msvcrt.open_osfhandle(handle, os.O_WRONLY), 'w', encoding='utf-8',
                              errors='replace', buffering=1)
            except OSError:
                stream = None
        setattr(sys, name, stream or open(os.devnull, 'w'))
    # An inherited handle can exist and still refuse writes ("[Errno 22] Invalid argument" from a
    # print in link_modules.py, issue #104 upstream): this output is informational, so a failed
    # write drops the stream instead of aborting the preparation.
    for name in ('stdout', 'stderr'):
        setattr(sys, name, _TolerantStream(getattr(sys, name)))


class _TolerantStream:
    def __init__(self, stream):
        self._stream = stream

    def write(self, text):
        try:
            return self._stream.write(text)
        except (OSError, ValueError):
            self._stream = open(os.devnull, 'w')
            return len(text)

    def flush(self):
        try:
            self._stream.flush()
        except (OSError, ValueError):
            pass

    def __getattr__(self, attr):
        return getattr(self._stream, attr)


def run_role(argv):
    """--run [args]: sotc.py; --script <file> [args]: a preparation script. Returns an exit code."""
    attach_stdio()
    for stream in (sys.stdout, sys.stderr):
        try:
            # UTF-8 whatever the Windows code page (gbk etc. cannot print the game title's ™).
            stream.reconfigure(line_buffering=True, encoding='utf-8', errors='replace')
        except (AttributeError, ValueError):
            pass
    if argv[0] == '--script':
        path, sys.argv = argv[1], argv[1:]
    else:
        path = str(PORT_DIR / 'sotc.py')
        sys.argv = [path, *argv[1:]]
    try:
        runpy.run_path(path, run_name='__main__')
    except SystemExit as stop:
        if isinstance(stop.code, int):
            return stop.code
        if stop.code is not None:
            # As the Python executable does: the message of SystemExit('...') goes to stderr
            # (a cvar sotc_modes rejects, a missing file), or the game stopped with no reason.
            print(stop.code, file=sys.stderr)
            return 1
        return 0
    return 0


def run_command():
    """The command that starts sotc.py: this executable when frozen, else Python."""
    return [sys.executable, '--run'] if FROZEN else [sys.executable, str(PORT_DIR / 'sotc.py')]


def allow_game_foreground():
    """Lets the game (two processes down) take the foreground from the launcher: a borderless
    window that is not in front keeps the taskbar over it."""
    try:
        ctypes.windll.user32.AllowSetForegroundWindow(-1)  # ASFW_ANY
    except (AttributeError, OSError):
        pass


def exit_value(code):
    """The sys.exit() value that ends this process with exit code `code` (as sotc.py's): from
    0x80000000 up its signed 32-bit value, since older Pythons exit with -1 (0xffffffff) for a
    larger int, which hid the NTSTATUS of crashes (#61)."""
    code &= 0xffffffff
    return code - (1 << 32) if code & 0x80000000 else code


def exit_text(code):
    """An exit code for messages: small ones in decimal, NTSTATUS values as 0xc0000409."""
    status = code & 0xffffffff
    return str(status) if status < 0x10000 else f'{status:#010x}'


# ---------------------------------------------------------------------------------------------
# Texts: Portuguese and English next to the code; Spanish, Russian, Chinese (Simplified),
# Japanese and Korean in sotc_i18n.py, looked up by the Portuguese text. English for every other
# Windows language.

LANG = 'pt'
# Segoe UI and Georgia have no Chinese, Japanese or Korean characters.
CJK_FONTS = {'zh': 'Microsoft YaHei UI', 'ja': 'Yu Gothic UI', 'ko': 'Malgun Gothic'}


def _(pt, en):
    return translate(pt, en, LANG)


def set_language(lang):
    global LANG
    LANG = lang


sotc_search._ = sotc_pad._ = _  # their texts in the launcher's language


def ui_font():
    """Interface font: Inter (sotc_theme); Chinese, Japanese and Korean have their own."""
    return CJK_FONTS.get(LANG, theme.BODY)


def title_font():
    """Titles: Cinzel (sotc_theme), which has no Cyrillic: Russian keeps Georgia."""
    return 'Georgia' if LANG == 'ru' else CJK_FONTS.get(LANG, theme.HEAD)


def windows_language():
    try:
        primary = ctypes.windll.kernel32.GetUserDefaultUILanguage() & 0x3ff
    except (AttributeError, OSError):
        primary = 0
    return {0x16: 'pt', 0x0a: 'es', 0x19: 'ru', 0x04: 'zh', 0x11: 'ja', 0x12: 'ko'}.get(primary, 'en')


# ---------------------------------------------------------------------------------------------
# Settings, passed to sotc.py as environment variables.

APP_DEFAULTS = {'ui_language': '', 'game_dir': str(DEFAULT_GAME_DIR), 'sys_modules': '',
                'user_dir': '', 'af16': True, 'textures': 'high', 'skip_intro': 'off', 'hdr': False, 'window_icon_file': '', 'fps60': False,
                'fps_mode': '30', 'window_mode': 'windowed',
                'present_mode': 'Mailbox', 'language': '', 'player_name': '', 'show_fps': False,
                'readbacks': '', 'extra_env': '', 'debug_menu': False, 'debug_watch': False, 'rb_hot': True,
                'close_on_play': False, 'gamepad': '', 'pad_glyphs': '',
                'output_res': '1920x1080', 'render_res': '', 'upscaler': 'fsr1', 'sharpness': '250', 'mouse': True,
                'mouse_sens': '1.0', 'mouse_invert': False, 'neo': False, 'fx_vignette': True,
                'fx_fog': True, 'fx_ssao': True, 'fx_shadows': True, 'fx_motionblur': False, 'engine_cvars': '', 'perf_culling': True,
                'perf_record_thread': True, 'perf_dma': True, 'amd_gcn_compat': False, 'gpu_submit': 'auto', 'vram_use': 'normal', 'vram_profile': 'auto', 'gpu_vram_mb': 0,
                'volume': '100', 'shadow_reuse': '0',
                'grass_shadows': '2', 'grass_distance': '1', 'grass_height': '1', 'weed_distance': '1', 'weed_height': '1',
                'grass_fade': '1', 'grass_detail': '1', 'library_dirs': [], 'keys': {},
                'auto_setup': True, 'dlc_dir': '', 'framegen': 'off', 'dlssg_mult': '2',
                'check_updates': True, 'skip_version': '', 'dlss': 'off', 'fsr4_mode': 'quality', 'dlss_compare': False, 'vibration': '100',
                'mouse_buttons': {}, 'gpu_name': '', 'fov': '100', 'play_mode': 'custom', 'exit_backup': True, 'news_seen': '',
                'expert_view': False, 'windows_title_bar': False, 'game_monitor': '', 'minimize_to_tray': False}
# sotc: improvements that changed a default, by the version that changed it. Settings saved by an
# older launcher get them once when this one first loads them (load_settings). Every release that
# changes a default for the better adds its entry here.
DEFAULT_UPGRADES = [
    # Fast GPU readbacks (safe since the page pool's double free is fixed) and no motion blur
    # trail in dark areas.
    ('1.3.0', {'rb_hot': True, 'fx_motionblur': False}),
]
# Keyboard bindings (src/runtime_pad.c): PS4 input, its default SDL key name, label.
KEY_INPUTS = [
    ('move_up', 'W', ('Andar para frente', 'Move forward')), ('move_down', 'S', ('Andar para trás', 'Move back')),
    ('move_left', 'A', ('Andar para a esquerda', 'Move left')), ('move_right', 'D', ('Andar para a direita', 'Move right')),
    ('look_up', 'Up', ('Câmera para cima', 'Camera up')), ('look_down', 'Down', ('Câmera para baixo', 'Camera down')),
    ('look_left', 'Left', ('Câmera para a esquerda', 'Camera left')), ('look_right', 'Right', ('Câmera para a direita', 'Camera right')),
    ('cross', 'Space', ('✕', '✕')), ('circle', 'Left Shift', ('○', '○')), ('square', 'E', ('□', '□')),
    ('triangle', 'Q', ('△', '△')), ('l1', '1', ('L1', 'L1')), ('r1', '3', ('R1', 'R1')), ('l2', 'R', ('L2', 'L2')),
    ('r2', 'F', ('R2', 'R2')), ('l3', 'Z', ('L3', 'L3')), ('r3', 'C', ('R3', 'R3')),
    ('options', 'Return', ('Options', 'Options')), ('touchpad', 'Tab', ('Touchpad (esquerda)', 'Touchpad (left)')),
    ('touchpad_right', 'Backspace', ('Touchpad (direita)', 'Touchpad (right)')),
    ('up', 'I', ('Direcional ↑', 'D-pad ↑')), ('down', 'K', ('Direcional ↓', 'D-pad ↓')),
    ('left', 'J', ('Direcional ←', 'D-pad ←')), ('right', 'L', ('Direcional →', 'D-pad →')),
]
# Mouse buttons (mouse.<input>= of sotc.ini, src/runtime_pad.c): its defaults, and the inputs that
# take one (buttons only, not the move / look inputs).
MOUSE_DEFAULTS = {'r1': 'left', 'square': 'right', 'l1': 'middle'}
MOUSE_INPUTS = tuple(name for name, _d, _l in KEY_INPUTS if not name.startswith(('move_', 'look_')))
MOUSE_BUTTONS = [('left', ('Mouse esquerdo', 'Left mouse')), ('right', ('Mouse direito', 'Right mouse')),
                 ('middle', ('Mouse do meio', 'Middle mouse')), ('x1', ('Mouse X1', 'Mouse X1')),
                 ('x2', ('Mouse X2', 'Mouse X2'))]
# Tk button numbers: X1/X2 are 4/5 on Windows and 8/9 on X11 (where 4/5 are the wheel).
TK_MOUSE_BUTTONS = {1: 'left', 2: 'middle', 3: 'right', 4: 'x1', 5: 'x2', 8: 'x1', 9: 'x2'}
# Tk key symbols whose SDL scancode name differs (letters, digits, F-keys and arrows match).
TK_TO_SDL = {
    'space': 'Space', 'Return': 'Return', 'Tab': 'Tab', 'BackSpace': 'Backspace', 'Escape': 'Escape',
    'Shift_L': 'Left Shift', 'Shift_R': 'Right Shift', 'Control_L': 'Left Ctrl', 'Control_R': 'Right Ctrl',
    'Alt_L': 'Left Alt', 'Alt_R': 'Right Alt', 'Caps_Lock': 'CapsLock', 'Prior': 'PageUp', 'Next': 'PageDown',
    'Home': 'Home', 'End': 'End', 'Insert': 'Insert', 'Delete': 'Delete', 'comma': ',', 'period': '.',
    'slash': '/', 'semicolon': ';', 'apostrophe': "'", 'bracketleft': '[', 'bracketright': ']',
    'minus': '-', 'equal': '=', 'grave': '`', 'backslash': '\\', 'KP_Enter': 'Keypad Enter',
    'KP_Add': 'Keypad +', 'KP_Subtract': 'Keypad -', 'KP_Multiply': 'Keypad *', 'KP_Divide': 'Keypad /',
    'KP_Decimal': 'Keypad .',
}
# Automatic: on with NVIDIA, off on AMD/Intel (game_environment).
GPU_SUBMIT = [('auto', ('Automático', 'Automatic')),
              ('on', ('Sempre ligado', 'Always on')),
              ('off', ('Desligado', 'Off'))]
# Video memory use (#143): (texture collection from % of the card's budget, arena release from MB; 0 = never).
VRAM_USE = [('low', ('Baixo (4–6 GB)', 'Low (4–6 GB)')),
            ('normal', ('Padrão', 'Default')),
            ('high', ('Alto (8–12 GB)', 'High (8–12 GB)')),
            ('max', ('Máximo (16 GB ou mais)', 'Maximum (16 GB or more)'))]
# Skip intro at start (game_environment): the logos only, or the logos and the opening movie.
SKIP_INTRO = [('off', ('Desligado', 'Off')),
              ('logos', ('Só as vinhetas', 'Logos only')),
              ('all', ('Vinhetas e vídeo de abertura', 'Logos and opening movie'))]
# Video memory profile (game_environment): the 0.72 texture improvements (Maximum at mip 0 with
# banks kept 3 s and no page pool switch, High at mip 1) used 5-7 GB on an 8 GB card; cards with
# less than 8 GB get the lighter set. Auto decides from the card the launcher found (gpu_vram_mb).
VRAM_PROFILES = [('auto', ('Automático (pela placa)', 'Automatic (by the card)')),
                 ('light', ('Leve (placas de até 6 GB)', 'Light (cards up to 6 GB)')),
                 ('full', ('Completo (placas de 8 GB ou mais)', 'Full (8 GB cards or more)'))]
LIGHT_VRAM_BELOW_MB = 7500


def light_vram(s):
    """The lighter texture set: chosen, or Auto on a card with less than 8 GB."""
    profile = s.get('vram_profile') or 'auto'
    if profile != 'auto':
        return profile == 'light'
    mb = int(s.get('gpu_vram_mb') or 0)
    return 0 < mb < LIGHT_VRAM_BELOW_MB


# Far sun shadows drawn on alternate frames (SOTC_SHADOW_REUSE): more FPS, but they may blink (#178).
# Automatic: on with NVIDIA, off on AMD/Intel.
SHADOW_REUSE = [('0', ('Desligado', 'Off')),
                ('1', ('Ligado', 'On')),
                ('auto', ('Automático', 'Automatic'))]
# The cameras' field of view (SOTC_FOV, scripts/sotc_modes.py --fov), cutscenes included.
FOV = [('100', ('100% (original)', '100% (original)')), ('110', ('110%', '110%')),
       ('120', ('120%', '120%')), ('130', ('130%', '130%'))]
# Sun shadow cascades the grass casts shadows in (r_virtualGeometryNumShadowCascades): the PS4
# value 2 ends them at 8 m, a band of grass darkens ~10 m ahead while riding; 3 reaches 24 m (4 would
# reach 100 m, but there a blade is far below a shadow-map texel: no visible gain, only cost); 1 = 2.6 m.
GRASS_SHADOWS = [('1', ('Perto (até ~3 m)', 'Near (to ~3 m)')),
                 ('2', ('Como no PS4 (até ~8 m)', 'As on PS4 (to ~8 m)')),
                 ('3', ('Longe (até ~24 m)', 'Far (to ~24 m)'))]
# Grass and weed view distance: the grass shaders thin the grass out in distance bands; these
# multiply the band distances (more grass far away, blades the same height).
VEG_DISTANCE = [('1', ('Como no PS4', 'As on PS4')),
                ('2', ('2x mais longe', '2x farther')),
                ('3', ('3x mais longe', '3x farther')),
                ('5', ('5x mais longe', '5x farther'))]
# The weeds take much larger multipliers without bands (10x tested): denser fields, like early autumn.
WEED_DISTANCE = VEG_DISTANCE + [('10', ('10x mais longe', '10x farther'))]
VEG_HEIGHT = [('0.75', ('75%', '75%')),
              ('1', ('Como no PS4', 'As on PS4')),
              ('1.5', ('150%', '150%')),
              ('2', ('200%', '200%'))]
# Fade (r_virtualGeometryFadeMultiplier) does not change the height: away from 1 it thins the grass,
# and 0.3 also drops its shadows.
GRASS_FADE = [('0.5', ('0.5', '0.5')),
              ('0.75', ('0.75', '0.75')),
              ('1', ('Como no PS4', 'As on PS4')),
              ('2', ('2', '2')),
              ('3', ('3', '3'))]
# Grass detail: scales the distance at which a grass patch gets its full blade count (the shader's LOD
# scale; smaller = full grass farther out). On PS4 the grass near the camera fills in late, and its
# shadow pops in with it; 0.01 distorts.
GRASS_DETAIL = [('1', ('Como no PS4', 'As on PS4')),
                ('0.25', ('Alto', 'High')),
                ('0.15', ('Mais alto', 'Higher')),
                ('0.1', ('Muito alto', 'Very high'))]
VOLUME = [('100', ('100% (original)', '100% (original)')), ('150', ('150%', '150%')),
          ('200', ('200%', '200%')), ('300', ('300%', '300%'))]
# Controller rumble strength (SOTC_VIBRATION, src/runtime_pad.c).
VIBRATION = [('0', ('Desligada', 'Off')), ('25', ('25%', '25%')), ('50', ('50%', '50%')),
             ('75', ('75%', '75%')), ('100', ('100% (padrão)', '100% (default)'))]
VRAM_LIMITS = {'low': ('50', '1024'), 'normal': ('60', '1536'), 'high': ('75', '4096'), 'max': ('85', '0')}
# Play modes (Jogar tab): each one sets the settings below; changing one of them by hand afterwards
# makes the mode Custom. Extreme's upscaler is DLSS on an NVIDIA RTX card, FSR 1 on the others.
PLAY_MODES = [('normal', ('Normal (PS4 puro)', 'Normal (plain PS4)')),
              ('pro', ('Pro (PS4 Pro + qualidade)', 'Pro (PS4 Pro + quality)')),
              ('extreme', ('Extremo (tudo no máximo)', 'Extreme (everything maxed)')),
              ('custom', ('Personalizado', 'Custom'))]
# The play modes keep the grass as on PS4 (user's choice, 3 Nov): the grass options are left to Custom.
_PS4_GRASS = {'grass_shadows': '2', 'grass_distance': '1', 'grass_height': '1', 'weed_distance': '1',
              'weed_height': '1', 'grass_fade': '1', 'grass_detail': '1'}
PLAY_MODE_VALUES = {
    'normal': {'neo': False, 'fps_mode': '30', 'textures': 'high', 'af16': False, 'upscaler': 'fsr1', 'dlss': 'off',
               'sharpness': '250', 'vram_use': 'normal', **_PS4_GRASS},
    'pro': {'neo': True, 'fps_mode': '30', 'textures': 'high', 'af16': True, 'vram_use': 'high',
            **_PS4_GRASS},
    'extreme': {'neo': True, 'fps_mode': '60', 'textures': 'max', 'af16': True, 'vram_use': 'max',
                **_PS4_GRASS},
}
PLAY_MODE_HINTS = {
    'normal': ('Como no PS4: 30 FPS, texturas altas, grama e sombras do console, sem o modo Pro.',
               "As on PS4: 30 FPS, high textures, the console's grass and shadows, no Pro mode."),
    'pro': ('Modo PS4 Pro a 30 FPS com texturas altas e filtro 16x; grama como no PS4.',
            'PS4 Pro mode at 30 FPS with high textures and 16x filtering; grass as on PS4.'),
    'extreme': ('Modo Pro a 60 FPS, texturas máximas, DLSS na RTX; grama como no PS4. '
                'Pede uma placa forte (8 GB ou mais).',
                'Pro mode at 60 FPS, maximum textures, DLSS on RTX; grass as on PS4. '
                'Needs a strong card (8 GB or more).'),
    'custom': ('Os ajustes abaixo e as outras abas ficam como você deixar.',
               'The settings below and the other tabs stay as you set them.'),
}


def sdl_key_name(keysym):
    """SDL scancode name of a Tk key symbol, or None for keys the game cannot use."""
    if keysym in TK_TO_SDL:
        return TK_TO_SDL[keysym]
    if len(keysym) == 1 and keysym.isalnum():
        return keysym.upper()
    if keysym in ('Up', 'Down', 'Left', 'Right') or (keysym[:1] == 'F' and keysym[1:].isdigit()):
        return keysym
    if keysym.startswith('KP_') and keysym[3:].isdigit():
        return 'Keypad ' + keysym[3:]
    return None


def ensure_ini(ini):
    """sotc.ini made from the release's sotc.ini.example when missing (as sotc.py does): the zip
    no longer carries sotc.ini, which replaced the player's when extracted over an older copy (#136)."""
    template = ini.with_name(ini.name + '.example')
    if not ini.exists() and template.is_file():
        try:
            shutil.copyfile(template, ini)
        except OSError:
            pass


def write_key_bindings(keys, mouse=None, ini=None):
    """The launcher's key and mouse button bindings as key.<input>= and mouse.<input>= lines of
    sotc.ini (BB_CONFIG), replacing its earlier ones; other lines stay. An empty value unbinds."""
    ini = Path(ini or PORT_DIR / 'sotc.ini')
    ensure_ini(ini)
    try:
        lines = ini.read_text(encoding='utf-8').splitlines()
    except OSError:
        lines = []
    lines = [line for line in lines if not line.startswith(('key.', 'mouse.'))
             and line not in ('# key bindings (launcher)', '# mouse buttons (launcher)')]
    custom = dict(keys or {})
    if custom:
        lines.append('# key bindings (launcher)')
        lines += [f'key.{name}={value}' for name, value in sorted(custom.items())]
    buttons = {name: value for name, value in dict(mouse or {}).items() if name in MOUSE_INPUTS}
    if buttons:
        lines.append('# mouse buttons (launcher)')
        lines += [f'mouse.{name}={value}' for name, value in sorted(buttons.items())]
    ini.write_text('\n'.join(lines) + '\n', encoding='utf-8')


def ini_mouse_buttons(ini=None):
    """mouse.<input>= lines of sotc.ini written by hand before the launcher bound mouse buttons
    itself: its first settings take them over instead of dropping them."""
    found = {}
    try:
        lines = Path(ini or PORT_DIR / 'sotc.ini').read_text(encoding='utf-8').splitlines()
    except OSError:
        return found
    for line in lines:
        name, sep, value = line.partition('=')
        if line.startswith('mouse.') and sep and name[6:] in MOUSE_INPUTS:
            found[name[6:]] = ','.join(b for b in value.replace(' ', ',').lower().split(',') if b)
    return found


def mouse_binding(custom, name):
    """The mouse buttons of an input ('left', 'x1,x2', '' for none) with the custom bindings."""
    return custom[name] if name in custom else MOUSE_DEFAULTS.get(name, '')


def bind_mouse_button(custom, name, button):
    """The custom mouse bindings after giving button to input name: the input that had it loses
    it (an empty binding), and bindings equal to the defaults are dropped."""
    custom = dict(custom or {})
    for other in MOUSE_INPUTS:
        buttons = [b for b in mouse_binding(custom, other).split(',') if b]
        if other != name and button in buttons:
            buttons.remove(button)
            custom[other] = ','.join(buttons)
    custom[name] = button
    return {k: v for k, v in custom.items() if v != MOUSE_DEFAULTS.get(k, '')}
# Engine effects verified to change the image when set on the command line (A/B, 08/10/2026).
EFFECTS = [('fx_vignette', ('Vinheta', 'Vignette'),
            'r_vignetteOverrideEnable=1 r_vignetteEnable=0'),
           ('fx_fog', ('Névoa atmosférica', 'Atmospheric fog'), 'fog_enable=0'),
           ('fx_ssao', ('Oclusão ambiente (SSAO)', 'Ambient occlusion (SSAO)'), 'AO_Mode=0'),
           # Off: no sun shadow cascades, +34% FPS at 720p on a CPU-bound PC (AjncSystem #294).
           ('fx_shadows', ('Sombras do sol', 'Sun shadows'), 'r_enableCascadeShadows=0'),
           ('fx_motionblur', ('Motion blur (borrão de movimento)', 'Motion blur'), 'r_motionBlurEnable=0')]
EFFECT_HINTS = {'fx_shadows': ('Desativar aumenta o FPS quando o processador limita o desempenho.',
                               'Turning it off raises the FPS when the processor limits performance.')}
# Common screen sizes: 16:9, 16:10, 21:9 (ultrawide) and 32:9 (super ultrawide).
# 5120x2160 is left out: the game faults at start (memory), 3840x2160 runs.
SCREENS = [('1920x1080', '16:9, 1080p'), ('2560x1440', '16:9, 1440p'), ('3840x2160', '16:9, 4K'),
           ('1920x1200', '16:10'), ('2560x1600', '16:10'),
           ('2560x1080', '21:9'), ('3440x1440', '21:9'), ('3840x1600', '21:9'),
           ('3840x1080', '32:9'), ('5120x1440', '32:9')]
OUTPUTS = [('1280x720', ('1280 × 720 (16:9, 720p)', '1280 × 720 (16:9, 720p)'))] + [
    (size, (f"{size.replace('x', ' × ')} ({label})",) * 2) for size, label in SCREENS]
# Engine render resolution (scripts/sotc_modes.py res<height>): the frame is scaled up to the 1080p
# display buffer by the game itself, as the PS4 Pro does from 1440p to 4K. A render size of another
# shape (SOTC_RENDER_RES, outputs other than 16:9) takes its place.
RENDER_RES = [('', ('1080p (nativa)', '1080p (native)')),
              ('res900', ('900p', '900p')),
              ('res810', ('810p', '810p')),
              ('res720', ('720p', '720p'))]


def render_height(key):
    """'res900' -> '900p'; the native one is 1080p."""
    return key[3:] + 'p' if key.startswith('res') else '1080p'


def weak_gpu_render_res(gpu_line):
    """The render resolution a card starts with ('' = native 1080p), from bb-gpu-capabilities'
    "GPU: <name>, <kind>, <N> MiB" line. A GTX 1050 is ~4.5x slower than the RTX 3070 Laptop the
    port is measured on (11.3 ms of GPU at 1080p in the shrine): 810p for it, the MX cards and
    other 2 GB cards; 900p for the 1050 Ti class and cards up to 3 GB; 720p for integrated
    graphics and cards under 1.5 GB. Textures, fog and effects stay; only the 3D scene is smaller."""
    found = re.match(r'(.*), (discrete|integrated or other), (\d+) MiB', gpu_line)
    if not found:
        return ''
    name, kind, mib = found[1], found[2], int(found[3])
    if kind != 'discrete' or mib < 1536:
        return 'res720'
    if mib <= 2560 or re.search(r'GTX 10[35]0(?! Ti)|GT \d{3,4}\b|MX ?\d{3}|RX (46|5[45])0', name):
        return 'res810'
    if mib <= 3072 or re.search(r'GTX (7\d\d|9[0-6]0|10[35]0)', name):
        return 'res900'
    return ''


# NVIDIA DLSS Super Resolution (bb-probe: SOTC_UPSCALER=dlss): the game renders at the output size
# divided by the mode's factor (SOTC_RENDER_RES, dlss_render) and DLSS rebuilds the output image,
# also in PS4 Pro mode (from the Pro's own render size) and at any aspect. The game checks the
# support again at start (Streamline) and upscales with FSR 1 without it.
DLSS_MODES = [('off', ('Desligado', 'Off')),
              ('dlaa', ('DLAA (resolução nativa)', 'DLAA (native resolution)')),
              ('quality', ('DLSS Qualidade (1,5×)', 'DLSS Quality (1.5×)')),
              ('balanced', ('DLSS Equilibrado (1,72×)', 'DLSS Balanced (1.72×)')),
              ('performance', ('DLSS Desempenho (2×)', 'DLSS Performance (2×)')),
              ('ultra', ('DLSS Ultra Desempenho (3×)', 'DLSS Ultra Performance (3×)'))]
# DLAA (#34): DLSS anti-aliasing at the output size.
DLSS_FACTORS = {'dlaa': 1.0, 'quality': 1.5, 'balanced': 1.724, 'performance': 2.0, 'ultra': 3.0}


# The game's memory (9152 MiB of direct memory and 1 GiB of flexible memory, committed at start)
# and the room the rest of the game needs (Vulkan, textures in system memory). With less free
# commit the game gets a smaller pool and runs out of memory after some minutes (bb-probe falls
# back as far as Windows allows).
GAME_COMMIT_MB = 9152 + 1024 + 2560


def commit_free_mb():
    """Free commit (RAM plus page file) in MiB, or None when Windows does not say."""
    class MemoryStatus(ctypes.Structure):
        _fields_ = [('length', ctypes.c_ulong), ('load', ctypes.c_ulong),
                    ('total_phys', ctypes.c_ulonglong), ('avail_phys', ctypes.c_ulonglong),
                    ('total_page_file', ctypes.c_ulonglong), ('avail_page_file', ctypes.c_ulonglong),
                    ('total_virtual', ctypes.c_ulonglong), ('avail_virtual', ctypes.c_ulonglong),
                    ('avail_extended_virtual', ctypes.c_ulonglong)]
    status = MemoryStatus(length=ctypes.sizeof(MemoryStatus))
    try:
        return status.avail_page_file >> 20 if ctypes.windll.kernel32.GlobalMemoryStatusEx(ctypes.byref(status)) else None
    except (AttributeError, OSError):
        return None


def gpu_has_dlss(gpu_name):
    """DLSS Super Resolution: NVIDIA GeForce RTX cards."""
    return 'RTX' in str(gpu_name).upper()


def gpu_has_dlss_g(gpu_name):
    """DLSS frame generation: RTX 40 and 50 series."""
    return bool(re.search(r'RTX\s*(40|50)\d\d', str(gpu_name), re.I))


def gpu_has_mfg(gpu_name):
    """DLSS multi frame generation (x3, x4): RTX 50 series."""
    return bool(re.search(r'RTX\s*50\d\d', str(gpu_name), re.I))


def dlss_blocked(s, gpu_name=None):
    """Why DLSS cannot apply with settings s: 'gpu' (not an RTX card; an unknown card is not held
    against it), or ''."""
    gpu_name = s.get('gpu_name', '') if gpu_name is None else gpu_name
    return 'gpu' if gpu_name and not gpu_has_dlss(gpu_name) else ''


def dlss_mode(s):
    """The DLSS mode the game gets ('quality', ...), or '' (off or not possible)."""
    mode = s.get('dlss')
    return mode if mode in DLSS_FACTORS and not dlss_blocked(s) else ''


# Button icons in the game (SOTC_PAD_GLYPHS, gpu texture_cache/sotc_pad_glyphs.cpp).
# Automatic: the icons of the device in use.
PAD_GLYPHS = [('', ('Automático', 'Automatic')),
              ('xbox', ('Xbox', 'Xbox')),
              ('ps', ('PlayStation', 'PlayStation')),
              ('keyboard', ('Teclado e mouse', 'Keyboard and mouse'))]
# AMD FSR SDK 2.4 (bb-probe: SOTC_UPSCALER=fsr4, vk_fsr4.cpp): FSR 4.1 on Radeon RX 6000/7000/9000,
# FSR 3.1 on the other cards. Render sizes as DLSS's; DLSS wins when both could apply.
FSR4_MODES = [('dlaa', ('Resolução nativa', 'Native resolution')),
              ('quality', ('Qualidade (1,5×)', 'Quality (1.5×)')),
              ('balanced', ('Equilibrado (1,72×)', 'Balanced (1.72×)')),
              ('performance', ('Desempenho (2×)', 'Performance (2×)')),
              ('ultra', ('Ultra Desempenho (3×)', 'Ultra Performance (3×)'))]


def fsr4_mode(s):
    """The FSR 4 mode the game gets ('quality', ...), or '' (another upscaler)."""
    return (s.get('fsr4_mode') if s.get('fsr4_mode') in DLSS_FACTORS else 'quality') if s.get('upscaler') == 'fsr4' else ''


UPSCALERS = [('fsr1', ('AMD FSR 1', 'AMD FSR 1')),
             ('fsr4', ('AMD FSR 4.1 (RX 6000+; FSR 3.1 nas outras)', 'AMD FSR 4.1 (RX 6000+; FSR 3.1 on others)')),
             ('bilinear', ('Bilinear', 'Bilinear'))]
if not (PORT_DIR / 'bin' / 'amd_fidelityfx_loader_vk.dll').is_file():
    UPSCALERS = [item for item in UPSCALERS if item[0] != 'fsr4']
# Frame rate: 30 (the game's cap, as on PS4), 60 (the game's own 60 FPS mode, that of the PS4 Pro
# Performance mode, at most one frame per two 120 Hz vblanks: SOTC_FPS_LIMIT=60) or unlimited.
FPS_MODES = [('30', ('30 FPS (como no PS4)', '30 FPS (as on PS4)')),
             ('60', ('60 FPS (modo 60 do jogo)', "60 FPS (the game's 60 FPS mode)")),
             ('unlimited', ('Ilimitado (acima de 60)', 'Unlimited (above 60)'))]
# Texture level (#235): off (the game's 256x256 mips), high (fine mips streamed, coarser while the
# game's page pool or video memory is short) or maximum (always fine; may close in very wide views,
# not offered in PS4 Pro mode).
TEXTURES = [('off', ('Desligada', 'Off')),
            ('high', ('Alta (estável)', 'High (stable)')),
            ('max', ('Máxima (nítida, experimental no modo Pro)', 'Maximum (sharp, experimental in PS4 Pro mode)'))]
# Frame generation (bb-probe: SOTC_FRAMEGEN; F8 in game switches off / DLSS-G / FSR 3.1).
# Automatic: DLSS-G on an RTX 40/50, else FSR 3.1.
FRAMEGEN = [('off', ('Desligada', 'Off')),
            ('auto', ('Automática', 'Automatic')),
            ('dlss', ('NVIDIA DLSS-G', 'NVIDIA DLSS-G')),
            ('fsr', ('AMD FSR 3.1', 'AMD FSR 3.1'))]
# Without DLSS-G (a card known not to be an RTX 40/50): automatic means FSR 3.1.
FRAMEGEN_FSR = [FRAMEGEN[0], FRAMEGEN[1], FRAMEGEN[3]]
# DLSS-G frames shown per game frame (SOTC_DLSSG_MULT): x3 to x6 and Dynamic MFG need an RTX 50
# (multi frame generation; the game keeps what the card and driver do).
DLSSG_MULT = [('2', ('x2', 'x2')), ('3', ('x3', 'x3')), ('4', ('x4', 'x4')), ('5', ('x5', 'x5')),
              ('6', ('x6', 'x6')), ('dynamic', ('Dinâmico', 'Dynamic'))]
SHARPNESS = [('0', ('Alta', 'High')), ('250', ('Média (padrão)', 'Medium (default)')), ('600', ('Baixa', 'Low')),
             ('off', ('Desligada', 'Off'))]
MOUSE_SENS = [('0.5', ('0,5 (lenta)', '0.5 (slow)')), ('0.75', ('0,75', '0.75')), ('1.0', ('1,0 (padrão)', '1.0 (default)')),
              ('1.5', ('1,5', '1.5')), ('2.0', ('2,0 (rápida)', '2.0 (fast)'))]
# How the game's window shows (SOTC_WINDOW_MODE, gpu/shim/window.cpp).
WINDOW_MODES = [('windowed', ('Janela', 'Windowed')),
                ('borderless', ('Tela cheia em janela', 'Borderless fullscreen')),
                ('fullscreen', ('Tela cheia', 'Fullscreen'))]
PRESENT_MODES = [('Mailbox', ('Mailbox (baixa latência)', 'Mailbox (low latency)')),
                 ('Fifo', ('FIFO (VSync)', 'FIFO (VSync)')),
                 ('Immediate', ('Immediate (sem VSync)', 'Immediate (no VSync)'))]
# System language (BB_LANGUAGE, PS4 ids); '' keeps the game's own choice.
LANGUAGES = [('', ('Automático (o do Windows)', 'Automatic (Windows\' language)')),
             ('7', ('Português (Portugal)', 'Portuguese (Portugal)')), ('15', ('Português (Brasil)', 'Portuguese (Brazil)')),
             ('1', ('Inglês', 'English')), ('3', ('Espanhol', 'Spanish')), ('2', ('Francês', 'French')),
             ('4', ('Alemão', 'German')), ('5', ('Italiano', 'Italian')), ('8', ('Russo', 'Russian')),
             ('0', ('Japonês', 'Japanese'))]
READBACKS = [('', ('Precisas (padrão)', 'Precise (default)')),
             ('1', ('Relaxadas (pode travar)', 'Relaxed (may hang)'))]
# Each language by its own name (a plain string: not translated).
UI_LANGUAGES = [('', ('Automático', 'Automatic')), ('en', 'English'), ('es', 'Español'), ('pt', 'Português'),
                ('ru', 'Русский'), ('zh', '中文（简体）'), ('ja', '日本語'), ('ko', '한국어')]
CONTROLS = [('W A S D', ('analógico esquerdo', 'left stick')), (('setas', 'arrows'), ('analógico direito (câmera)', 'right stick (camera)')),
            ('Espaço / Space', '✕'), ('Shift', '○'), ('E', '□'), ('Q', '△'), ('1 / 3', 'L1 / R1'), ('R / F', 'L2 / R2'),
            ('Z / C', 'L3 / R3'), ('Enter', 'Options'), ('I J K L', ('direcional', 'd-pad')),
            ('Tab / Backspace', ('touchpad (esquerda / direita)', 'touchpad (left / right)'))]


def load_json(path, default):
    try:
        return json.loads(Path(path).read_text(encoding='utf-8'))
    except (OSError, ValueError):
        return default


_EBOOT_SUPPORTED = {}


def eboot_supported(eboot):
    """True when the eboot holds the code the modes patch (EU 1.01 and US 1.00 are the same
    build; dumps often keep the base game's param.sfo next to the updated eboot). Cached per file
    state."""
    try:
        stat = eboot.stat()
    except OSError:
        return False
    key = (str(eboot), stat.st_size, stat.st_mtime_ns)
    if key not in _EBOOT_SUPPORTED:
        try:
            from prepare import parse_self
            from sotc_modes import is_supported
            elf, _h, ph, _s, _m = parse_self(eboot.read_bytes())
            _EBOOT_SUPPORTED[key] = is_supported(elf, ph)
        except (OSError, ValueError, ImportError):
            _EBOOT_SUPPORTED[key] = False
    return _EBOOT_SUPPORTED[key]


def game_info(folder):
    """(title id, game version) of a game folder, or None without eboot.bin. A supported release
    shows its version (EU 01.01, US 01.00) whatever its param.sfo says."""
    folder = Path(folder or '.')
    if not (folder / 'eboot.bin').is_file():
        return None
    try:
        from prepare import sfo
        values = sfo((folder / 'sce_sys/param.sfo').read_bytes())
        title, version = values.get('TITLE_ID', '?'), values.get('APP_VER', '?')
    except (OSError, ValueError, ImportError):
        title, version = '?', '?'
    if title in GAMES and eboot_supported(folder / 'eboot.bin'):
        version = GAMES[title][1]
    return title, version


def game_supported(folder):
    """True for a supported release (EU or US) whose eboot the modes patch."""
    info = game_info(folder)
    return bool(info) and info[0] in GAMES and eboot_supported(Path(folder) / 'eboot.bin')


def game_serial(folder):
    """The title ID of the game folder, for its saves and DLC; CUSA08809 (EU) when unknown."""
    info = game_info(folder) if folder else None
    return info[0] if info and info[0] in GAMES else GAME_ID


def supported_games():
    """'CUSA08809 01.01, CUSA08034 01.00' for messages."""
    return ', '.join(f'{serial} {version}' for serial, (_region, version) in GAMES.items())


# The game's archives and their sizes, the same in a CUSA08809 (EU) and a CUSA08034 (US) dump (the
# .psarc files are byte-identical): an incomplete copy (a download or a copy cut short) only showed
# as a crash at boot (#30, #56). The 1.01 update's cachedFiles.psarc (in the update folder, or in a
# dump merged with it) is the larger size.
GAME_ARCHIVES = {'cachedFiles.psarc': (37159164, 37274125), 'pak0.psarc': (364098725,),
                 'pak1.psarc': (464260479,), 'pak2.psarc': (16557548,), 'pak3.psarc': (720294074,),
                 'pakN.psarc': (12838532222,)}


def damaged_archives(folder, update=None):
    """The archives of a game folder (EU or US) that are missing or of another size, then those of
    its update folder that are of another size, as <update folder>/<name>: the game reads them in
    place of the base game's (#112). update: SOTC_UPDATE_DIR or the folder sotc_discovery finds
    when not given."""
    folder = Path(folder or '.')
    sizes = {name.lower(): (name, allowed) for name, allowed in GAME_ARCHIVES.items()}

    def wrong_size(path):
        try:
            return path.stat().st_size not in sizes[path.name.lower()][1]
        except OSError:
            return True

    present = {p.name.lower(): p for p in folder.glob('*.psarc')}
    bad = [name for key, (name, _s) in sizes.items() if key not in present or wrong_size(present[key])]
    if update is None:
        from sotc_discovery import find_update
        update = os.environ.get('SOTC_UPDATE_DIR') or (find_update(folder) if folder.name else '')
    if update:
        update = Path(update)
        bad += [str(Path(update.name, p.name)) for p in sorted(update.glob('*.psarc'))
                if p.name.lower() in sizes and wrong_size(p)]
    return bad


def save_param(path):
    """The title, subtitle and detail the port keeps for a save (<name>.sce_sys/param.bin, the
    PS4 SaveDataParam: 128, 128 and 1024 bytes of UTF-8), or {}."""
    try:
        data = Path(path).read_bytes()
    except OSError:
        return {}
    text = lambda a, b: data[a:b].split(b'\0')[0].decode('utf-8', 'replace').strip()
    return {'MAINTITLE': text(0, 128), 'SUBTITLE': text(128, 256), 'DETAIL': text(256, 1280)} if len(data) >= 1280 else {}


def user_dir(s):
    """The data folder (saves in savedata/1/<title id> below it: CUSA08809 EU, CUSA08034 US). A
    folder picked one level too deep (savedata itself, or a save folder) is taken back up to the
    data folder."""
    folder = Path(s.get('user_dir') or PORT_DIR / 'user')
    parts = [p.lower() for p in folder.parts]
    if 'savedata' in parts:
        folder = Path(*folder.parts[:len(parts) - 1 - parts[::-1].index('savedata')])
    return folder


# Shown in the side bar only in the expert view (the search finds them in both).
EXPERT_PAGES = ('advanced', 'debug')
NAV_ICONS = {'help': 'help', 'play': 'play', 'game': 'gameplay', 'saves': 'saves', 'graphics': 'display', 'controls': 'controls',
             'advanced': 'advanced', 'debug': 'system'}
SAVE_BACKUPS_KEPT = 10  # user/backups/saves-*.zip
LOGS_KEPT = 5      # last_run.log and last_run.1.log ... last_run.4.log
CRASH_LOGS_KEPT = 20


def open_session_log(log_dir):
    """Opens a fresh user/last_run.log; the previous sessions' logs move to last_run.1.log (the
    one before) ... last_run.4.log, so a crash is not lost when the game is started again."""
    log_dir.mkdir(parents=True, exist_ok=True)
    for i in range(LOGS_KEPT - 1, 0, -1):
        older = log_dir / ('last_run.log' if i == 1 else f'last_run.{i - 1}.log')
        if older.is_file():
            try:
                os.replace(older, log_dir / f'last_run.{i}.log')
            except OSError:
                pass
    return open(log_dir / 'last_run.log', 'w', encoding='utf-8', buffering=1)


def start_event_log(log_dir, settings, env):
    """user/events.log: one line per event of the session (src/runtime_event.h), for players and
    developers to read. The launcher writes the header (date, version, system, the settings in
    words) and the game appends its events (SOTC_EVENT_LOG). The last 5 sessions are kept."""
    log_dir.mkdir(parents=True, exist_ok=True)
    for i in range(LOGS_KEPT - 1, 0, -1):
        older = log_dir / ('events.log' if i == 1 else f'events.{i - 1}.log')
        if older.is_file():
            try:
                os.replace(older, log_dir / f'events.{i}.log')
            except OSError:
                pass
    path = log_dir / 'events.log'

    def words(table, key, default=''):
        return dict((k, _(*v)) for k, v in table).get(settings.get(key) or default, str(settings.get(key)))

    def on_off(value):
        return _('ligado', 'on') if value else _('desligado', 'off')

    lines = [
        _('Shadow of the Colossus PC {} - sessão de {}', 'Shadow of the Colossus PC {} - session of {}').format(
            VERSION, time.strftime('%Y-%m-%d %H:%M:%S')),
        _('Windows {} | jogo: {}', 'Windows {} | game: {}').format(platform.version(), settings.get('game_dir') or '?'),
        _('Saída: {} | Upscaler: {} | Nitidez: {} | DLSS: {}', 'Output: {} | Upscaler: {} | Sharpening: {} | DLSS: {}').format(
            words(OUTPUTS, 'output_res', '1920x1080'), words(UPSCALERS, 'upscaler', 'fsr1'),
            words(SHARPNESS, 'sharpness', '250'),
            words(DLSS_MODES, 'dlss', 'off') if env.get('SOTC_UPSCALER') == 'dlss' else _(*dict(DLSS_MODES)['off'])),
        _('FPS: {} | Texturas: {} | Filtro 16x: {} | Modo PS4 Pro: {} | Campo de visão: {}',
          'FPS: {} | Textures: {} | 16x filtering: {} | PS4 Pro mode: {} | Field of view: {}').format(
            words(FPS_MODES, 'fps_mode', '30'), words(TEXTURES, 'textures', 'high'), on_off(settings.get('af16')),
            on_off(env.get('SOTC_NEO') == '1'), words(FOV, 'fov', '100')),
        _('Memória da placa: {} ({})', 'Card memory: {} ({})').format(
            words(VRAM_PROFILES, 'vram_profile', 'auto'),
            _('leve', 'light') if light_vram(settings) else _('completo', 'full')),
        _('Geração de quadros: {} | Memória de vídeo: {} | Sombras distantes: {} | Volume: {}%',
          'Frame generation: {} | Video memory use: {} | Far shadows: {} | Volume: {}%').format(
            words(FRAMEGEN, 'framegen', 'off'), words(VRAM_USE, 'vram_use', 'normal'),
            on_off(str(settings.get('shadow_reuse') or '0') != '0'), settings.get('volume') or '100'),
        _('Janela: {} | Pular a introdução: {} | Variáveis extras: {}',
          'Window: {} | Skip intro: {} | Extra variables: {}').format(
            words(WINDOW_MODES, 'window_mode', 'windowed'), words(SKIP_INTRO, 'skip_intro', 'off'),
            settings.get('extra_env') or _('nenhuma', 'none')),
    ]
    if env.get('SOTC_NOTE'):
        lines.append(_('Aviso: {}', 'Note: {}').format(env['SOTC_NOTE']))
    try:
        with open(path, 'w', encoding='utf-8') as out:
            for line in lines:
                out.write(f'[00:00:00.000] [CONFIG  ] {line}\n')
    except OSError:
        return ''
    return str(path)


def end_event_log(log_dir, code, stopped=False):
    """The end of the session in user/events.log; after a crash its copy goes to user/crashes."""
    path = log_dir / 'events.log'
    if code == 0 or stopped:
        text = (_('Jogo fechado normalmente', 'Game closed normally') if code == 0
                else _('Jogo parado pelo lançador', 'Game stopped by the launcher'))
    elif code & 0xffffffff == 3:
        text = _('O jogo caiu (código 3): o motivo está na linha ERROR acima e no log da sessão',
                 'The game crashed (code 3): the reason is in the ERROR line above and in the session log')
    else:
        text = _('O jogo terminou com erro (código {:#x})', 'The game ended with an error (code {:#x})').format(code & 0xffffffff)
    try:
        with open(path, 'a', encoding='utf-8') as out:
            out.write(f'[--:--:--.---] [SESSION ] {text}\n')
        if code != 0 and not stopped:
            crashes = log_dir / 'crashes'
            crashes.mkdir(parents=True, exist_ok=True)
            shutil.copy2(path, crashes / (time.strftime('%Y-%m-%d_%H-%M-%S') + f'_{code & 0xffffffff:#x}_events.log'))
    except OSError:
        pass


def keep_crash_log(log_dir, code):
    """After a session that ended with an error code, copies its log to
    user/crashes/<date>_<time>_<code>.log (the last 20 are kept)."""
    if code == 0:
        return
    crashes = log_dir / 'crashes'
    try:
        crashes.mkdir(parents=True, exist_ok=True)
        stamp = time.strftime('%Y-%m-%d_%H-%M-%S')
        shutil.copy2(log_dir / 'last_run.log', crashes / f'{stamp}_{code & 0xffffffff:#x}.log')
        for old in sorted(crashes.glob('*.log'))[:-CRASH_LOGS_KEPT]:
            old.unlink()
        # Minidumps written by bb-probe on a fatal fault (src/probe.c): the last 5.
        for old in sorted(crashes.glob('*.dmp'))[:-5]:
            old.unlink()
    except OSError:
        pass


def near_path(path, base):
    """path relative to base when on the same drive, else as is."""
    try:
        return os.path.relpath(path, base)
    except ValueError:
        return str(path)


def short_path(path):
    """A path for display: relative to the port folder when inside it, else as is."""
    try:
        return str(Path(path).resolve().relative_to(PORT_DIR.resolve()))
    except (ValueError, OSError):
        return str(path)


def migrate_settings(s):
    """Settings of older versions: the fullscreen box (a borderless window over the monitor)
    becomes that window mode, and the Xbox button icons of the upstream launcher the always-Xbox
    choice of the icons for the device in use."""
    if 'fullscreen' in s:
        if s.pop('fullscreen') and s.get('window_mode', 'windowed') == 'windowed':
            s['window_mode'] = 'borderless'
    # 1.1.1's Skip intro box (the engine's +nointro: it also skipped the title and the profile)
    # becomes the selector, off.
    if isinstance(s.get('skip_intro'), bool):
        s['skip_intro'] = 'off'
    # The upstream launcher's two boxes; +nointro broke saving (#378) and is not offered here.
    film, logos = s.pop('skip_film', False), s.pop('skip_logos', False)
    if (film or logos) and s.get('skip_intro', 'off') == 'off':
        s['skip_intro'] = 'all' if film else 'logos'
    if s.pop('button_icons', None) == 'xbox' and not s.get('pad_glyphs'):
        s['pad_glyphs'] = 'xbox'
    # The upstream launcher (0.70) chose DLSS as an upscaler with its own mode key; here DLSS has
    # a choice of its own, FSR 1 stays the upscaler of frames without the game's scene.
    if s.get('upscaler') == 'dlss':
        s['upscaler'] = 'fsr1'
        if s.get('dlss', 'off') == 'off':
            s['dlss'] = s.get('dlss_mode') if s.get('dlss_mode') in DLSS_FACTORS else 'quality'
    # Older launchers copied shadPS4's sound modules into user/sys_modules on their own; that copy
    # is dropped for the port's own sound library (a folder chosen by hand stays).
    if not s.get('own_sound_default'):
        s['own_sound_default'] = True
        auto = user_dir(s) / 'sys_modules'
        if s.get('sys_modules') and Path(s['sys_modules']).resolve() == auto.resolve():
            s['sys_modules'] = ''
    return s


def _ps_quote(value):
    """A PowerShell single-quoted string literal (no $ expansion; every quote PowerShell accepts doubled)."""
    return "'" + ''.join(c * 2 if c in "'\u2018\u2019\u201a\u201b" else c for c in str(value)) + "'"


def _shortcut_script(play, frozen, executable, port_dir, game_icon):
    """PowerShell that creates the desktop shortcut: the launcher window, or with play the game directly
    (--play, also usable as a non-Steam game in Steam)."""
    if frozen:
        target, arguments, icon = executable, '--play' if play else '', game_icon or f'{executable},0'
    else:
        pythonw = Path(executable).with_name('pythonw.exe')
        target = pythonw if pythonw.exists() else executable
        arguments = f'"{Path(port_dir) / "launcher" / "sotc_launcher_win.py"}"' + (' --play' if play else '')
        icon = Path(port_dir) / 'launcher' / 'sotc.ico'
    name = '\\Shadow of the Colossus (play).lnk' if play else '\\Shadow of the Colossus.lnk'
    return ("$s=(New-Object -ComObject WScript.Shell).CreateShortcut([Environment]::GetFolderPath('Desktop')"
            f"+{_ps_quote(name)});"
            f'$s.TargetPath={_ps_quote(target)};$s.WorkingDirectory={_ps_quote(port_dir)};'
            f'$s.IconLocation={_ps_quote(icon)};'
            + (f'$s.Arguments={_ps_quote(arguments)};' if arguments else '') + '$s.Save()')


def load_settings():
    """Saved settings over the defaults, migrated (migrate_settings). Older settings that turned
    DLSS-G on with SOTC_DLSSG=1 in the extra variables get the frame generation setting instead."""
    saved = load_json(CONFIG_FILE, {})
    if not saved and LEGACY_CONFIG_FILE.is_file():
        # First start of this copy: the shared settings of older versions, but its own data
        # folder when the old one is another copy's user folder.
        saved = load_json(LEGACY_CONFIG_FILE, {})
        global settings_in_use
        settings_in_use = LEGACY_CONFIG_FILE
        old = Path(str(saved.get('user_dir') or ''))
        if (str(old) not in ('', '.') and old.name.lower() == 'user' and (old.parent / 'SotC.exe').is_file()
                and old.parent.resolve() != PORT_DIR.resolve()):
            saved['user_dir'] = ''
            # Its saves and the user's sound modules come along (copied; the old copy keeps them).
            for name in ('savedata', 'sys_modules'):
                source, target = old / name, PORT_DIR / 'user' / name
                if source.is_dir() and not target.exists():
                    try:
                        shutil.copytree(source, target)
                    except OSError:
                        pass
    settings = {**APP_DEFAULTS, **saved}
    if 'fps_mode' not in saved:  # before the choice: the "uncapped FPS" box
        settings['fps_mode'] = '60' if saved.get('fps60') else '30'
    if 'textures' not in saved:  # before the levels: the "high quality textures" box
        settings['textures'] = 'high' if saved.get('hq_textures', True) else 'off'
    settings.pop('hq_textures', None)
    # Rule: an update turns its improvements on in settings saved by an older version, once
    # (DEFAULT_UPGRADES). A saved value would otherwise keep the old default for good.
    if saved:
        before = sotc_update.version_key(str(saved.get('settings_version') or '0'))
        for version, values in DEFAULT_UPGRADES:
            if before < sotc_update.version_key(version):
                settings.update(values)
    settings['settings_version'] = VERSION
    if 'framegen' not in saved:
        extra = str(settings.get('extra_env', '')).split()
        if 'SOTC_DLSSG=1' in extra:
            settings['framegen'] = 'auto'
            settings['extra_env'] = ' '.join(item for item in extra if item != 'SOTC_DLSSG=1')
    if 'mouse_buttons' not in saved:
        settings['mouse_buttons'] = ini_mouse_buttons()
    return migrate_settings(settings)


def window_icon_file(s):
    """The game window's icon as a 64x64 BMP in the data folder (bb-probe: SOTC_WINDOW_ICON):
    empty setting = the port's own (launcher/window_icon.png, original art: the swordsman), 'game'
    = icon0.png of the player's dump, else a picture the player chose (cropped to a centred
    square). Nothing of the game or of anyone's art is packaged."""
    try:
        from PIL import Image
        choice = s.get('window_icon_file') or ''
        custom = Path(choice) if choice not in ('', 'game') else Path('')
        if choice == 'game':
            source, target = Path(s['game_dir'] or '.') / 'sce_sys' / 'icon0.png', user_dir(s) / 'game_icon.bmp'
        elif choice and custom.is_file():
            source, target = custom, user_dir(s) / 'window_icon_custom.bmp'
        else:
            source, target = PORT_DIR / 'launcher' / 'window_icon.png', user_dir(s) / 'window_icon_port.bmp'
        if (not target.is_file() or target.stat().st_mtime < source.stat().st_mtime
                or source == custom):
            with Image.open(source) as image:
                image = image.convert('RGBA')
                side = min(image.size)
                left, top = (image.width - side) // 2, (image.height - side) // 2
                if source == custom:
                    image = image.crop((left, top, left + side, top + side))
                target.parent.mkdir(parents=True, exist_ok=True)
                image.resize((64, 64), Image.LANCZOS).save(target)
        return target
    except (ImportError, OSError, ValueError):
        return None


def monitors():
    """This PC's monitors: [(device name, label, (x, y) of its centre, primary)], the primary first."""
    from ctypes import wintypes

    class MONITORINFOEXW(ctypes.Structure):
        _fields_ = [('size', wintypes.DWORD), ('monitor', wintypes.RECT), ('work', wintypes.RECT),
                    ('flags', wintypes.DWORD), ('device', wintypes.WCHAR * 32)]
    found = []
    PROC = ctypes.WINFUNCTYPE(ctypes.c_int, ctypes.c_void_p, ctypes.c_void_p, ctypes.POINTER(wintypes.RECT), ctypes.c_void_p)

    def one(handle, _dc, _rect, _data):
        info = MONITORINFOEXW()
        info.size = ctypes.sizeof(info)
        if ctypes.windll.user32.GetMonitorInfoW(ctypes.c_void_p(handle), ctypes.byref(info)):
            r = info.monitor
            found.append((info.device, r.right - r.left, r.bottom - r.top, ((r.left + r.right) // 2, (r.top + r.bottom) // 2),
                          bool(info.flags & 1)))
        return 1
    try:
        ctypes.windll.user32.EnumDisplayMonitors(None, None, PROC(one), None)
    except (AttributeError, OSError):
        return []
    found.sort(key=lambda m: (not m[4], m[0]))
    return [(device, '{} — {} × {}{}'.format(device.replace('\\\\.\\', '').replace('DISPLAY', _('Monitor ', 'Monitor ')),
                                          w, h, _(' (principal)', ' (primary)') if primary else ''), centre, primary)
            for device, w, h, centre, primary in found]


def game_environment(s):
    env = dict(os.environ)
    info = game_info(s['game_dir'])
    if info and info[1] != '?':
        env['SOTC_GAME_VERSION'] = info[1]  # window title (with the frame rate)
    env['SOTC_PORT_VERSION'] = VERSION  # sotc: window title
    icon = window_icon_file(s)
    if icon:
        env['SOTC_WINDOW_ICON'] = str(icon)
    env['SOTC_GAME_DIR'] = s['game_dir']
    env['SOTC_USER_DIR'] = str(user_dir(s))
    if s['sys_modules']:
        env['SOTC_SYS_MODULES'] = s['sys_modules']
    if s.get('dlc_dir'):
        env['SOTC_DLC_DIR'] = s['dlc_dir']
    # An update inside the game folder or next to its parent (sotc_discovery.find_update): sotc.py
    # itself only looks at <game>-UPDATE / <game>-patch.
    if 'SOTC_UPDATE_DIR' not in env:
        from sotc_discovery import find_update
        update = find_update(s['game_dir']) if s.get('game_dir') else ''
        if update:
            env['SOTC_UPDATE_DIR'] = update
    # The 30 FPS cap is applied or not by bb-probe (sotc_fps_cap.h), F7 switches it in game: no
    # fps60 code patch, vblanks at 120 Hz in every mode; 60 FPS limits the uncapped game. Above 30
    # the game also targets 60 FPS itself (fps60game), so its timing follows: ignoring its 30 FPS
    # target ran some physics and timers twice as fast (AjncSystem #100, #120, #49).
    fps_mode = s.get('fps_mode') or '30'
    modes = ['af16'] if s.get('af16') else []
    if fps_mode != '30':
        modes.append('fps60game')
    # DLSS renders at the output size divided by its mode's factor (SOTC_RENDER_RES below) in place of
    # the render resolution choice.
    if not dlss_mode(s) and s.get('render_res') in {key for key, _t in RENDER_RES if key}:
        modes.append(s['render_res'])
    # At this resolution the grass overflows the game's grass buffers: the farthest grass then pops
    # in and out in patches, in a band ahead with longer grass distances. grassbuf makes them 4x larger.
    modes.append('grassbuf')
    env['SOTC_MODES'] = ','.join(modes)
    env['SOTC_FPS_CAP'] = '30' if fps_mode == '30' else 'off'
    env['SOTC_FPS_LIMIT'] = '60' if fps_mode == '60' else '0'
    env['BB_VBLANK_HZ'] = '120'
    mode = s.get('window_mode') if s.get('window_mode') in dict(WINDOW_MODES) else 'windowed'
    env['SOTC_WINDOW_MODE'] = mode
    env['BB_FULLSCREEN'] = '0' if mode == 'windowed' else '1'
    # The monitor the game window opens on (window.cpp): the centre of the chosen one, found again now
    # (the monitors may have moved since it was chosen).
    chosen = next((m for m in monitors() if m[0] == s.get('game_monitor')), None)
    if chosen and not chosen[3]:
        env['SOTC_MONITOR_AT'] = f'{chosen[2][0]},{chosen[2][1]}'
    env['BB_PRESENT_MODE'] = s['present_mode']
    # HDR (#37): an HDR10 swapchain when Windows and the display offer one (vk_swapchain.cpp); the
    # game then sees an HDR display (sceVideoOutGetDeviceCapabilityInfo) and outputs HDR.
    env['BB_HDR'] = '1' if s.get('hdr') else '0'
    if s.get('fov') in {key for key, _t in FOV} - {'100'}:
        env['SOTC_FOV'] = s['fov']
    if s['language']:
        env['BB_LANGUAGE'] = s['language']
    if str(s['player_name']).strip():
        env['BB_USER_NAME'] = str(s['player_name']).strip()
    if s['show_fps']:
        env['SOTC_FPS'] = '1'
    env['SOTC_OVERLAY'] = '1' if s['show_fps'] else '0'  # over show_fps in sotc.ini
    if s['readbacks']:
        env['BB_READBACKS'] = s['readbacks']
    cvars = [off for key, _t, off in EFFECTS if not s.get(key, True)]
    if s.get('perf_culling', True):
        # From Pink-shadPS4: no GPU index-buffer culling (always both), same image, far fewer
        # GPU results the CPU has to wait for.
        cvars += ['doIndexBufferCulling=0', 'doSunShadowIndexBufferCulling=0']
    # Skip intro: the logos by the engine's own switch; the opening movie by the port holding cross
    # while it plays (runtime_pad.c). The title screen and the profile stay ("+nointro" skipped
    # them too: a new game started at once).
    if s.get('skip_intro') in ('logos', 'all'):
        cvars.append('skipSplash=1')
    if s.get('skip_intro') == 'all':
        env['SOTC_SKIP_MOVIE'] = '1'
    grass = str(s.get('grass_shadows') or '2')
    if grass in ('1', '3'):
        # Every mode's copy: the game takes the one of its graphics mode (PS4 Pro Cinematic has 3).
        cvars += [f'r_virtualGeometryNumShadowCascades{mode}={grass}'
                  for mode in ('', '_BasePS4', '_Perf', '_Cinematic')]
    fade = str(s.get('grass_fade') or '1')
    if fade in ('0.5', '0.75', '2', '3'):
        cvars.append(f'r_virtualGeometryFadeMultiplier={fade}')
    cvars += str(s.get('engine_cvars', '')).split()
    env['SOTC_CVARS'] = ' '.join(cvars)
    # Multipliers for the grass shaders' view distances and blade height (vk_rasterizer.cpp).
    env['SOTC_GRASS'] = ' '.join(str(s.get(k) or '1') for k in
                                 ('grass_distance', 'grass_height', 'weed_distance', 'weed_height', 'grass_detail'))
    env['SOTC_RECORD_THREAD'] = '1' if s.get('perf_record_thread', True) else '0'
    # RX 470-590 and Vega, opt-in for testers: 32-lane shaders stop the white flicker, the NVIDIA
    # shader options the device loss (#340, #375, #41).
    if s.get('amd_gcn_compat'):
        env['SOTC_AMD_WAVE32'], env['SOTC_GCN_NV'] = '1', '1'
    if not s.get('perf_dma', True):
        env['BB_DIRECT_MEMORY_ACCESS'] = '0'
    # Periodic submits and readbacks ahead: by default only with NVIDIA (vk_rasterizer.cpp).
    if s.get('gpu_submit') == 'on':
        env['SOTC_FLUSH_US'], env['SOTC_RB_AHEAD'] = '1000', '1'
    elif s.get('gpu_submit') == 'off':
        env['SOTC_FLUSH_EVERY'], env['SOTC_RB_AHEAD'] = '0', '0'
    light = light_vram(s)
    vram_use = s.get('vram_use')
    if light and vram_use in ('high', 'max'):
        vram_use = 'normal'  # sotc: "High"/"Maximum" (Extreme mode) hold more than such a card has
    env['SOTC_VRAM_GC_PERCENT'], env['SOTC_ARENA_EVICT_MB'] = VRAM_LIMITS.get(vram_use, VRAM_LIMITS['normal'])
    if light:
        env.setdefault('SOTC_STAGING_KEEP_VRAM_MB', '0')
    env['SOTC_VOLUME'] = str(s.get('volume') or '100')
    # The in-game menu's quick settings are written back here (bbport_overlay.cpp).
    env['SOTC_LAUNCHER_SETTINGS'] = str(settings_in_use)
    env['SOTC_SHADOW_REUSE'] = str(s.get('shadow_reuse') or '0')
    env['SOTC_OUTPUT_RES'] = s.get('output_res') or '1920x1080'
    # The PS4 Pro path keeps its 16:9 sizes: a wider screen (21:9, 32:9) wins over it (#142).
    env['SOTC_NEO'] = '1' if s.get('neo') and not is_wide(env['SOTC_OUTPUT_RES']) else '0'
    if s.get('neo') and env['SOTC_NEO'] == '0':
        env['SOTC_NOTE'] = f"{env['SOTC_OUTPUT_RES']} is wider than 16:9: PS4 Pro mode off (it renders 16:9 only)"
    framegen = s.get('framegen') or 'off'
    if framegen == 'dlss' and s.get('gpu_name') and not gpu_has_dlss_g(s['gpu_name']):
        framegen = 'auto'  # FSR 3.1 on a card without DLSS-G
    env['SOTC_FRAMEGEN'] = framegen
    env['SOTC_DLSSG_MULT'] = str(s.get('dlssg_mult') or '2')
    # sotc: texture level: the texture banks sampled since the last report read as sampled
    # (sotc_lod_stats.h; before, none ever did and the game never streamed its fine mips: blurry
    # leaves and rocks). Off: the old behaviour. Maximum: without the page pool switch; not in PS4
    # Pro mode, where it exhausted the game's page pool within minutes at the Shrine (0x1a371).
    textures = s.get('textures') or 'high'
    if textures != 'off':
        env.setdefault('SOTC_LOD_LINGER', '1')
        # sotc: what the page pool's double free (fixed in 0.71, SOTC_POOL_OWNER) had held back,
        # tested on 3 Nov (Time Attack, every colossus, Maximum + PS4 Pro, no crash nor mosaic):
        # the measured mips, finer mips (High 1, Maximum 0, both were 2) and, on Maximum, banks
        # kept sampled for 3 s (they had exhausted the pool, 0x1a371).
        env.setdefault('SOTC_LOD_FEEDBACK', '1')
        # sotc: 0.72.2: the finer mips (High 1, Maximum 0) and Maximum's 3 s linger without the
        # page pool switch brought back the hard FPS drop at area changes that 0.70 had removed:
        # texture copies of up to 1.1 GB in one frame, worst frames 134-293 ms at the forest
        # entrance (5 Nov). Mip 1 alone dropped as hard; mip 2 with the 1-tick linger and the
        # switch (as in 0.71) was "much smoother, 2-3 FPS for 2 s". Both qualities stay at mip 2
        # (sotc_lod_stats.h default); SOTC_LOD_LEVEL / SOTC_LOD_LINGER / SOTC_LOD_POOL_LOW in the
        # extra variables still choose otherwise.
    # sotc: the heap guard's two shortcuts, off since 0.70 for the 0x49800 crash, which came from
    # the same double free: on again (tested with the above).
    env.setdefault('SOTC_GUARD_STALE', '1')
    env.setdefault('SOTC_GUARD_USER_POINTER', '1')
    # sotc: 0.72.2 (T51): at the end of the game the game returns page pool pages under textures
    # it goes on drawing; a refresh then uploaded another texture's bytes into them (stretched
    # textures, flicker, the close between the ending and the intro). Two clean ending runs.
    env.setdefault('SOTC_POOL_FREEZE', '1')
    # sotc: 0.72.2 (T48): an unmap's arena release waited for all the GPU work, 28-63 times a
    # frame at area changes (10-20 ms a frame); now once the GPU passed the unmap.
    env.setdefault('SOTC_RELEASE_DEFER', '1')
    # sotc: 0.72.2: images of 2048x2048 and more get memory of their own: the shared 64 MiB
    # blocks kept up to 1.8 GB free after an area's textures went (over the budget on 8 GB cards).
    env.setdefault('SOTC_IMAGE_DEDICATED', '1')
    # sotc: "Maximum" in PS4 Pro mode exhausted the game's page pool within minutes at the Shrine
    # (0x1a371, 22 Oct) until tabnul's page allocator fix (PR #288, sotc_modes.py POOL_FIX): an
    # hour without a crash with it (26 Oct). Experimental there; SOTC_MAX_IN_PRO=0 brings back High.
    if 'SOTC_MAX_IN_PRO=0' in str(s.get('extra_env', '')) and textures == 'max' and env['SOTC_NEO'] == '1':
        textures = 'high'
        env['SOTC_NOTE'] = 'Texture quality Maximum is not used in PS4 Pro mode: High'
    env['SOTC_UI_LANG'] = s.get('ui_language') or windows_language()  # overlay and F7/F8 texts
    # The game renders at the output size. 720p renders at 1080p, as on PS4; the PS4 Pro path keeps
    # its own sizes.
    output = env['SOTC_OUTPUT_RES']
    dlss = dlss_mode(s)
    fsr4 = '' if dlss else fsr4_mode(s)
    if (dlss or fsr4) and env['SOTC_NEO'] == '0':
        env['SOTC_RENDER_RES'] = dlss_render(output, dlss or fsr4)
    elif output not in ('1280x720', '1920x1080') and env['SOTC_NEO'] == '0':
        env['SOTC_RENDER_RES'] = output
    # DLSS: FSR 1 stays the upscaler of frames without the game's scene (menus) and the fallback
    # without an RTX card or the NVIDIA DLLs.
    env['BB_FSR1'] = '1' if dlss or fsr4 or s.get('upscaler', 'fsr1') == 'fsr1' else '0'
    if dlss or fsr4:
        env['SOTC_UPSCALER'] = 'dlss' if dlss else 'fsr4'
        if s.get('dlss_compare'):
            env['SOTC_DLSS_COMPARE'] = '1'
    if s.get('sharpness') == 'off':
        env['BB_RCAS'] = '0'
    else:
        env['BB_RCAS'] = '1'
        env['BB_RCAS_ATTENUATION'] = str(s.get('sharpness') or '250')
    env['SOTC_MOUSE'] = '1' if s.get('mouse', True) else '0'
    env['SOTC_MOUSE_SENS'] = str(s.get('mouse_sens') or '1.0')
    env['SOTC_MOUSE_INVERT'] = '1' if s.get('mouse_invert') else '0'
    # Hot readbacks (buffer_cache.cpp GatherDownloads): ~25% more FPS at the Shrine. Off from
    # 22 Oct (rainbow garbage and crashes on memory with two owners, the page pool's double free
    # fixed in 0.71); on by default again since 0.72 (tested on 3 Nov).
    if s.get('rb_hot'):
        env['SOTC_RB_HOT'] = '1'
    # Bluepoint's debug menu (+RetailBuild=0 served by the port; the game files are not touched).
    if s.get('debug_menu'):
        env['SOTC_DEBUG_MENU'] = '1'
    # The texture memory watch (events.log lines "Vigia: ..."), for bug reports.
    if s.get('debug_watch'):
        env['SOTC_POOL_WATCH'] = '1'
    if str(s.get('gamepad', '')).strip():
        env['BB_GAMEPAD'] = str(s['gamepad']).strip()
    if s.get('pad_glyphs') in ('xbox', 'ps', 'keyboard'):
        env['SOTC_PAD_GLYPHS'] = s['pad_glyphs']
    env['SOTC_VIBRATION'] = str(s.get('vibration', '100'))
    env['BB_CONFIG'] = str(PORT_DIR / 'sotc.ini')
    for item in str(s['extra_env']).split():
        item = item.strip().lstrip('=')  # "=KEY=1" typed by mistake still sets KEY
        if '=' in item:
            key, value = item.split('=', 1)
            if key:
                env[key] = value
    env['PYTHONUNBUFFERED'] = '1'
    env['PYTHONIOENCODING'] = 'utf-8'
    return env


def dlss_render(output, mode):
    """The render size for DLSS mode at output WxH: multiples of 8, at least 640x360 (DLAA: the
    output itself). The exact aspect when a size within 6% of the factor has it: the presenter
    keeps the render's aspect, so 2552x1064 for 3840x1600 gave a 3838 wide output. Render widths
    of 1153 to 1280 are widened by sotc_modes.py (row pitch 1280)."""
    try:
        w, h = map(int, output.split('x'))
    except ValueError:
        return output
    factor = DLSS_FACTORS.get(mode, 1.5)
    if factor == 1.0 and 1152 < w <= 1280:
        factor = w / 1152  # sotc_modes.py would widen it past the output (row pitch 1280)
    target = h / factor

    def width(rh):
        return max(640, round(rh * w / h / 8) * 8)

    sizes = [rh for rh in range(360, h + 1, 8) if not 1152 < width(rh) <= 1280]
    exact = [rh for rh in sizes if rh * w % (h * 8) == 0]
    rh = max(360, round(target / 8) * 8)
    near = [s for s in exact if abs(s - target) <= target * 0.06]
    if 1152 < width(rh) <= 1280:
        # Widths of 1153 to 1280 are widened by sotc_modes.py with a height rounded to 8, a shape
        # off the output's (1920x1080 Quality: 1296x728, one black row): the exact aspect within
        # 15% of the target outside them, the larger one on a tie, else the nearest allowed size.
        near = near or [s for s in exact if abs(s - target) <= target * 0.15] or sizes
    if near:
        rh = min(near, key=lambda s: (abs(s - target), -s))
    return f'{width(rh)}x{rh}'


def is_wide(size):
    """True for a WxH wider than 16:9 (by more than 2%)."""
    try:
        w, h = map(int, str(size).split('x'))
    except ValueError:
        return False
    return w * 9 > h * 16 * 1.02


def shadps4_saves(serial):
    """Save folders of the game serial in shadPS4 installations next to the port or in the usual
    places."""
    roots = [PORT_DIR.parent, Path.home(), Path(os.environ.get('APPDATA', Path.home()))]
    found = []
    for root in roots:
        for pattern in ('*/user/home/*/savedata/' + serial, '*/*/user/home/*/savedata/' + serial,
                        'shadPS4/savedata/*/' + serial):
            try:
                found += [p for p in root.glob(pattern) if p.is_dir()]
            except OSError:
                pass
    return found


# ---------------------------------------------------------------------------------------------
# The window.

# Theme of the Forbidden Land: night stone, sunlight gold for titles, the sword's light (sigil blue)
# for what is active.
BG, PANEL, CARD, LINE = theme.DEEP, theme.BG, theme.CARD, theme.BORDER
TEXT, MUTED, GOLD, ACCENT, ACCENT_HI = theme.TEXT, theme.MUTED, theme.GOLD_LIGHT, theme.SIGIL, theme.SIGIL_HOVER
SIGIL = theme.SIGIL_LIGHT


class Launcher(sotc_search.SearchMixin, sotc_pad.PadMixin):
    update_checked = False

    def __init__(self, root, tk, ttk, filedialog, messagebox):
        self.tk, self.ttk, self.filedialog, self.messagebox = tk, ttk, filedialog, messagebox
        self.root = root
        self.app = load_settings()
        self.vars = {}
        self.process = self.job = None
        self.live_at_start = {}  # LIVE_KEYS as the game was started with them
        self.stopping = self.restart = False
        self.output = queue.Queue()
        self.ui_calls = queue.Queue()
        self.gpu_text = _('• Verificando a placa de vídeo…', '• Checking the graphics card…')
        # The card of the last detection until this one ends (detect_gpu): DLSS / DLSS-G choices.
        self.gpu_name = self.app.get('gpu_name', '')
        self.render_boxes, self.framegen_boxes = [], []
        self.banner_source = self.banner_image = None
        root.title('Shadow of the Colossus — ' + _('port nativo', 'native port'))
        root.configure(bg=BG)
        self.dpi = root.winfo_fpixels('1i') / 96.0
        # Tall enough for every quick setting when the screen has the room (the Play page scrolls otherwise).
        height = min(self.px(820), root.winfo_screenheight() - self.px(80))
        root.geometry(f'{self.px(1080)}x{max(self.px(620), height)}')
        root.minsize(self.px(940), self.px(620))
        self.set_icon()
        theme.load_fonts(root)
        self.style()
        self.build()
        self.show('play')
        self.refresh_library()
        # First start, or the game folder is gone: the setup screen asks for it.
        if not self.app.get('setup_done') or not game_info(self.var('game_dir').get()):
            root.after(50, self.show_setup)
        root.protocol('WM_DELETE_WINDOW', self.close)
        if self.app.get('setup_done') and self.app.get('news_seen') != VERSION:
            root.after(600, self.show_news)
        root.after(100, self.drain_output)
        threading.Thread(target=self.detect_gpu, daemon=True).start()
        # sotc: self-update (sotc_update.py): the result of an update that just ran, then a check.
        self.updating = False
        if FROZEN:
            error = sotc_update.cleanup(PORT_DIR)
            if error:
                root.after(500, lambda: messagebox.showerror('Shadow of the Colossus', _(
                    'A atualização falhou e a versão anterior foi mantida:\n{}',
                    'The update failed and the previous version was kept:\n{}').format(error)))
        # Once per process: a language switch builds the window again (main).
        if (self.app.get('check_updates', True) and (FROZEN or os.environ.get('SOTC_UPDATE_URL'))
                and not Launcher.update_checked):
            Launcher.update_checked = True
            root.after(2000, lambda: self.check_updates(False))

    def px(self, size):
        return int(size * self.dpi)

    def set_icon(self):
        """The game's own icon (sce_sys/icon0.png of the user's dump) when there is one, else the
        port's: the package carries no game art."""
        game = self.vars['game_dir'].get() if 'game_dir' in self.vars else self.app['game_dir']
        try:
            from PIL import Image, ImageTk
            with Image.open(Path(game) / 'sce_sys' / 'icon0.png') as source:
                self.icon = ImageTk.PhotoImage(source.convert('RGBA').resize((64, 64), Image.LANCZOS))
            self.root.iconphoto(True, self.icon)
            return
        except (ImportError, OSError):
            pass
        try:
            self.root.iconbitmap(default=str(PORT_DIR / 'launcher' / 'sotc.ico'))
        except self.tk.TclError:
            pass

    def game_icon_file(self):
        """icon0.png of the dump as an .ico in the data folder (for the desktop shortcut)."""
        try:
            from PIL import Image
            target = user_dir(self.app) / 'game.ico'
            with Image.open(Path(self.app['game_dir']) / 'sce_sys' / 'icon0.png') as source:
                target.parent.mkdir(parents=True, exist_ok=True)
                source.convert('RGBA').save(target, sizes=[(16, 16), (32, 32), (48, 48), (64, 64), (128, 128),
                                                           (256, 256)])
            return target
        except (ImportError, OSError):
            return None

    # ---- look --------------------------------------------------------------------------------
    def style(self):
        """The theme's widgets (sotc_theme: rounded buttons and fields, switches, the Play button),
        then the launcher's own labels."""
        ttk = self.ttk
        s = ttk.Style(self.root)
        self.theme = theme.Theme(self.root, self.dpi)
        self.theme.style(s)
        s.configure('TLabel', background=PANEL, foreground=TEXT, font=(ui_font(), 10))
        s.configure('Muted.TLabel', background=PANEL, foreground=MUTED, font=(ui_font(), 9))
        s.configure('Section.TLabel', background=PANEL, foreground=GOLD, font=(title_font(), 13))
        s.configure('Warning.TLabel', background='#1e1a12', foreground='#e0b866', padding=(10, 6))

    # ---- widget helpers ----------------------------------------------------------------------
    def var(self, key):
        tk = self.tk
        if key not in self.vars:
            value = self.app.get(key, APP_DEFAULTS[key])
            self.vars[key] = tk.BooleanVar(value=bool(value)) if isinstance(APP_DEFAULTS[key], bool) \
                else tk.StringVar(value=str(value))
        return self.vars[key]

    def choice(self, parent, key, options, width=38):
        """A read-only combobox for a setting; box.set_options(options) changes its entries (a value
        no longer offered becomes the first one)."""
        var = self.var(key)
        box = self.ttk.Combobox(parent, state='readonly', width=width)
        values = []

        def show(*_args):
            current = var.get()
            box.current(values.index(current) if current in values else 0)

        def set_options(new_options):
            values[:] = [v for v, _t in new_options]
            box.configure(values=[text if isinstance(text, str) else _(*text) for _v, text in new_options])
            if var.get() not in values:
                var.set(values[0])
            show()
        box.set_options = set_options
        set_options(options)
        box.bind('<<ComboboxSelected>>', lambda _e: var.set(values[box.current()]))
        var.trace_add('write', show)
        box.sotc_key = key  # the search's words for the setting (row)
        return box

    def next_row(self, parent):
        return parent.grid_size()[1]

    def row(self, parent, title, widget, hint=None):
        ttk = self.ttk
        r = self.next_row(parent)
        label = ttk.Label(parent, text=title)
        label.grid(row=r, column=0, sticky='nw', padx=(0, 18), pady=(8, 0))
        widget.grid(row=r, column=1, sticky='w', pady=(5, 0))
        self.index_entry(label, title, hint or '', getattr(widget, 'sotc_key', None))
        if hint:
            ttk.Label(parent, text=hint, style='Muted.TLabel', wraplength=self.px(560), justify='left').grid(
                row=r + 1, column=1, sticky='w', pady=(2, 2))
        return widget

    def check(self, parent, key, title, hint=None):
        ttk = self.ttk
        r = self.next_row(parent)
        box = ttk.Checkbutton(parent, text=title, variable=self.var(key))
        box.grid(row=r, column=0, columnspan=2, sticky='w', pady=(4, 0))
        self.index_entry(box, title, hint or '', key)
        if hint:
            ttk.Label(parent, text=hint, style='Muted.TLabel', wraplength=self.px(640), justify='left').grid(
                row=r + 1, column=0, columnspan=2, sticky='w', padx=(26, 0))

    def note(self, parent, text, top=12):
        self.ttk.Label(parent, text=text, style='Muted.TLabel', wraplength=self.px(680), justify='left').grid(
            row=self.next_row(parent), column=0, columnspan=2, sticky='w', pady=(top, 0))

    def state_note(self, parent):
        """A line under a setting that says why it is not available."""
        label = self.ttk.Label(parent, text='', style='Muted.TLabel', foreground='#d9b441',
                               wraplength=self.px(560), justify='left')
        label.grid(row=self.next_row(parent), column=1, sticky='w')
        return label

    def section(self, parent, title, top=18):
        r = self.next_row(parent)
        label = self.ttk.Label(parent, text=title, style='Section.TLabel')
        label.grid(row=r, column=0, columnspan=2, sticky='w', pady=(top, 2))
        self.index_entry(label, title)
        self.tk.Frame(parent, bg=LINE, height=1).grid(row=r + 1, column=0, columnspan=2, sticky='we', pady=(0, 4))

    def window_icon_row(self, parent):
        """The game window's icon: the game's (empty) or a picture of the player's choice."""
        ttk = self.ttk
        var = self.var('window_icon_file')
        holder = ttk.Frame(parent)
        ttk.Entry(holder, textvariable=var, width=40).pack(side='left')

        def browse():
            chosen = self.filedialog.askopenfilename(
                title=_('Imagem para o ícone da janela do jogo', "Picture for the game window's icon"),
                filetypes=[(_('Imagens', 'Pictures'), '*.png *.jpg *.jpeg *.bmp *.webp'), ('*', '*.*')])
            if chosen:
                var.set(str(Path(chosen)))
        ttk.Button(holder, text=_('Escolher imagem…', 'Choose picture…'), command=browse).pack(side='left', padx=(6, 0))
        ttk.Button(holder, text=_('Ícone do jogo', 'Game icon'), command=lambda: var.set('game')).pack(side='left', padx=(6, 0))
        ttk.Button(holder, text=_('Padrão', 'Default'), command=lambda: var.set('')).pack(side='left', padx=(6, 0))
        return self.row(parent, _('Ícone da janela', 'Window icon'), holder,
                        _('Aplica-se no próximo início do jogo.', 'Applies the next time the game starts.'))

    def folder(self, parent, key, title, prompt, hint=None, on_change=None):
        ttk = self.ttk
        var = self.var(key)
        holder = ttk.Frame(parent)
        ttk.Entry(holder, textvariable=var, width=54).pack(side='left')

        def browse():
            chosen = self.filedialog.askdirectory(title=prompt, initialdir=var.get() or str(PORT_DIR))
            if chosen:
                var.set(str(Path(chosen)))
        ttk.Button(holder, text=_('Procurar…', 'Browse…'), command=browse).pack(side='left', padx=(6, 0))
        ttk.Button(holder, text=_('Abrir', 'Open'), command=lambda: self.open_path(var.get() or (
            user_dir(self.app) if key == 'user_dir' else PORT_DIR))).pack(side='left', padx=(6, 0))
        if on_change:
            var.trace_add('write', lambda *_a: on_change())
        holder.sotc_key = key
        return self.row(parent, title, holder, hint)

    def scrolled_page(self, name, title, subtitle):
        tk, ttk = self.tk, self.ttk
        outer = ttk.Frame(self.content)
        head = ttk.Frame(outer, padding=(28, 22, 28, 6))
        head.pack(fill='x')
        ttk.Label(head, text=title, background=PANEL, foreground=GOLD, font=(title_font(), 22)).pack(anchor='w')
        ttk.Label(head, text=subtitle, style='Muted.TLabel').pack(anchor='w', pady=(2, 8))
        tk.Frame(head, bg=LINE, height=1).pack(fill='x')
        canvas = tk.Canvas(outer, bg=PANEL, highlightthickness=0, bd=0)
        bar = ttk.Scrollbar(outer, orient='vertical', command=canvas.yview)
        inner = ttk.Frame(canvas, padding=(28, 0, 28, 24))
        inner.columnconfigure(1, weight=1)
        window = canvas.create_window(0, 0, window=inner, anchor='nw')
        inner.bind('<Configure>', lambda _e: canvas.configure(scrollregion=canvas.bbox('all')))
        canvas.bind('<Configure>', lambda e: canvas.itemconfigure(window, width=e.width))
        canvas.configure(yscrollcommand=bar.set)
        bar.pack(side='right', fill='y')
        canvas.pack(side='left', fill='both', expand=True)
        outer.scroll = lambda units: canvas.yview_scroll(units, 'units') if inner.winfo_height() > canvas.winfo_height() else None
        outer.canvas, outer.inner = canvas, inner  # the search scrolls to a setting (jump)
        self.pages[name] = outer
        self.building_page = name
        self.page_titles[name] = title
        return inner

    # ---- layout ------------------------------------------------------------------------------
    def build(self):
        tk, ttk = self.tk, self.ttk
        self.custom_frame, self.tray = None, None
        self.titlebar = None if self.app.get('windows_title_bar') else self.build_titlebar()
        self.root.bind('<Unmap>', self.minimized, add='+')
        side = tk.Frame(self.root, bg=BG, width=self.px(220))
        side.pack(side='left', fill='y')
        side.pack_propagate(False)
        tk.Label(side, text='SHADOW OF\nTHE COLOSSUS', bg=BG, fg=GOLD, font=(title_font(), 16), justify='left').pack(
            anchor='w', padx=22, pady=(26, 0))
        tk.Frame(side, bg=GOLD, height=1, width=self.px(46)).pack(anchor='w', padx=22, pady=(10, 8))
        tk.Label(side, text=_('port nativo · Windows', 'native port · Windows') + f'  ·  v{VERSION}', bg=BG, fg=MUTED,
                 font=(ui_font(), 9)).pack(anchor='w', padx=22, pady=(0, 14))
        self.page_titles = {}
        self.build_search(side)
        # The pages as in the bloodborne_pc launcher: a thin line icon, a card behind the current
        # page with the sword's light at its left edge, a softer card under the mouse.
        self.nav, self.current_page = {}, None
        px, width, height = self.px, 196, 38
        self.nav_pills = {'on': self.theme.box(width, height, 8, CARD, LINE, bg=BG),
                          'hover': self.theme.box(width, height, 8, theme.SURFACE, bg=BG)}
        accent = self.theme.box(3, 18, 1.5, SIGIL, bg=CARD)
        for name, title in (('play', _('Jogar', 'Play')), ('game', _('Jogo', 'Game')), ('saves', _('Saves', 'Saves')),
                            ('graphics', _('Gráficos', 'Graphics')), ('controls', _('Controles', 'Controls')),
                            ('help', _('Ajuda', 'Help')), ('advanced', _('Avançado', 'Advanced')),
                            ('debug', _('Depuração', 'Debug'))):
            c = tk.Canvas(side, width=px(width), height=px(height), bg=BG, highlightthickness=0, bd=0, cursor='hand2')
            c.pack(padx=px(12), pady=(0, px(2)))
            mid = px(height / 2)
            items = {'pill': c.create_image(0, 0, anchor='nw', state='hidden'),
                     'bar': c.create_image(px(1), mid, anchor='w', image=accent, state='hidden'),
                     'icon': c.create_text(px(22), mid, text=theme.ICON[NAV_ICONS[name]], font=(theme.ICONS, 12)),
                     'label': c.create_text(px(44), mid, text=title, anchor='w',
                                              font=(theme.BODY_MEDIUM if LANG not in ('zh', 'ko') else ui_font(), 10))}
            c.bind('<Button-1>', lambda _e, n=name: self.show(n))
            c.bind('<Enter>', lambda _e, n=name: self.nav_paint(n, hover=True))
            c.bind('<Leave>', lambda _e, n=name: self.nav_paint(n))
            self.nav[name] = (c, items)
            self.nav_paint(name)
        self.nav_end = tk.Frame(side, bg=BG)  # the entries are packed above it (apply_view)
        self.nav_end.pack(fill='x')
        tk.Frame(side, bg=BG).pack(fill='both', expand=True)
        ttk.Checkbutton(side, text=_('Visão avançada', 'Expert view'), variable=self.var('expert_view'),
                        style='Bar.TCheckbutton', command=self.apply_view).pack(anchor='w', padx=22, pady=(0, 10))
        for text, command in ((_('Registro do jogo', 'Game log'), lambda: self.show('log')),
                              (_('Procurar atualização', 'Check for updates'), lambda: self.check_updates(True)),
                              (_('Configurar de novo', 'Set up again'), self.show_setup)):
            link = tk.Label(side, text=text, bg=BG, fg=MUTED, font=(ui_font(), 9, 'underline'), cursor='hand2')
            link.pack(anchor='w', padx=22, pady=(0, 6))
            link.bind('<Button-1>', lambda _e, c=command: c())
        tk.Frame(side, bg=BG, height=12).pack()

        right = tk.Frame(self.root, bg=PANEL)
        right.pack(side='left', fill='both', expand=True)
        bar = tk.Frame(right, bg=BG, height=self.px(72))
        bar.pack(fill='x', side='bottom')
        tk.Frame(right, bg=LINE, height=1).pack(fill='x', side='bottom')
        bar.pack_propagate(False)
        self.play_button = ttk.Button(bar, text=_('JOGAR', 'PLAY'), style='Play.TButton', command=self.play)
        self.play_button.pack(side='right', padx=(10, 24), pady=11)
        self.stop_button = ttk.Button(bar, text=_('Parar', 'Stop'), command=self.stop, state='disabled')
        self.stop_button.pack(side='right', pady=11)
        ttk.Button(bar, text=_('Relatório', 'Report'), command=self.save_report).pack(side='right', padx=(0, 8), pady=11)
        # Packed after the buttons: on a narrow window the summary wraps instead of pushing them out.
        self.status = tk.Label(bar, text='', bg=BG, fg=MUTED, font=(ui_font(), 10), anchor='w', justify='left')
        self.status.pack(side='left', fill='x', expand=True, padx=(24, 12))
        self.status.bind('<Configure>', lambda e: self.status.configure(wraplength=max(self.px(120), e.width)))
        self.content = tk.Frame(right, bg=PANEL)
        self.content.pack(fill='both', expand=True)

        self.pages = {}
        self.build_play()
        self.build_game()
        self.build_saves()
        self.build_graphics()
        self.build_controls()
        self.build_help()
        self.build_advanced()
        self.build_debug()
        self.build_log()
        self.root.bind_all('<MouseWheel>', self.wheel)
        for key in ('dlss', 'upscaler', 'fsr4_mode', 'output_res', 'neo'):
            self.var(key).trace_add('write', lambda *_a: self.refresh_upscaling())
        self.vars['dlss'].trace_add('write', lambda *_a: self.refresh_status())
        self.refresh_gpu_choices()
        self.page_titles['log'] = _('Registro do jogo', 'Game log')
        self.search_ready(self.page_titles)
        self.apply_view()
        self.refresh_status()

    def wheel(self, event):
        page = self.pages.get(self.current_page)
        if hasattr(page, 'scroll') and not isinstance(event.widget, (self.tk.Text, self.ttk.Combobox)) \
                and 'popdown' not in str(event.widget):
            page.scroll(int(-event.delta / 120))

    def show(self, name):
        for page in self.pages.values():
            page.pack_forget()
        self.pages[name].pack(fill='both', expand=True)
        self.current_page = name
        for n in self.nav:
            self.nav_paint(n)
        if name == 'controls':  # the controller test reads only while its page is shown
            self.pad_start()
        else:
            self.pad_stop()
        if hasattr(self, 'nav_end'):  # an expert page found by the search shows its entry meanwhile
            self.apply_view()
        if name == 'play':
            self.refresh_status()

    TITLE_ICONS = {'minimize': '\ue921', 'maximize': '\ue922', 'restore': '\ue923', 'close': '\ue8bb'}

    def build_titlebar(self):
        """The launcher's own title bar, as in the bloodborne_pc launcher: a drag area in the side
        bar's colour and the window's three buttons. Windows' caption goes once the window exists
        (window_chrome, sotc_frame)."""
        tk, px = self.tk, self.px
        bar = tk.Frame(self.root, bg=BG, height=px(32))
        bar.pack(side='top', fill='x')
        bar.pack_propagate(False)
        self.title_buttons = {}
        for name, command, hover in (('close', self.close, '#8a1c1c'),
                                     ('maximize', self.toggle_maximized, theme.SURFACE),
                                     ('minimize', self.root.iconify, theme.SURFACE)):
            cell = tk.Frame(bar, bg=BG, width=px(46), height=px(32))
            cell.pack(side='right')
            cell.pack_propagate(False)
            glyph = tk.Label(cell, text=self.TITLE_ICONS[name], bg=BG, fg=theme.TEXT_SOFT, font=(theme.ICONS, 9))
            glyph.pack(fill='both', expand=True)

            def paint(on, cell=cell, glyph=glyph, hover=hover, name=name):
                cell.configure(bg=hover if on else BG)
                glyph.configure(bg=hover if on else BG,
                                fg=theme.TEXT_STRONG if on and name == 'close' else TEXT if on else theme.TEXT_SOFT)
            for widget in (cell, glyph):
                widget.bind('<Enter>', lambda _e, paint=paint: paint(True))
                widget.bind('<Leave>', lambda _e, paint=paint: paint(False))
                widget.bind('<ButtonRelease-1>', lambda e, command=command, cell=cell, glyph=glyph:
                            command() if e.widget.winfo_containing(e.x_root, e.y_root) in (cell, glyph) else None)
            self.title_buttons[name] = glyph
        self.root.bind('<Configure>', lambda e: self.title_state() if e.widget is self.root else None, add='+')
        self.root.after(100, self.window_chrome)
        return bar

    def minimized(self, event):
        """Minimized, with "Minimize to the system tray" on: off the task bar, an icon in the tray."""
        if event.widget is self.root and self.var('minimize_to_tray').get():
            self.root.after(1, self.to_tray)

    def to_tray(self):
        if self.root.state() != 'iconic':
            return
        try:
            if self.tray is None:
                import sotc_tray
                self.tray = sotc_tray.Tray(PORT_DIR / 'launcher' / 'sotc.ico', 'Shadow of the Colossus', self.bring_back,
                                           self.quit_from_tray, (_('Abrir', 'Open'), _('Sair', 'Quit')), root=self.root)
            if self.tray.show():
                self.root.withdraw()
        except OSError:
            pass  # no tray: it stays on the task bar

    def bring_back(self):
        if self.tray:
            self.tray.hide()
        self.root.deiconify()
        self.root.state('normal')
        self.root.lift()
        self.root.focus_force()

    def quit_from_tray(self):
        self.bring_back()
        self.close()

    def toggle_maximized(self):
        self.root.state('normal' if self.root.state() == 'zoomed' else 'zoomed')

    def title_state(self):
        """The maximize button follows the window's state; Windows learns where the bar is."""
        if not getattr(self, 'title_buttons', None):
            return
        glyph = self.TITLE_ICONS['restore' if self.root.state() == 'zoomed' else 'maximize']
        if self.title_buttons['maximize'].cget('text') != glyph:
            self.title_buttons['maximize'].configure(text=glyph)
        if self.custom_frame:  # where Windows finds the caption and the buttons (physical pixels)
            self.custom_frame.bar = self.titlebar.winfo_height()
            self.custom_frame.buttons = sum(cell.master.winfo_width() for cell in self.title_buttons.values())
            self.custom_frame.edge, self.custom_frame.corner = self.px(5), self.px(10)

    def window_chrome(self):
        """Windows' caption off (sotc_frame), the frame dark; Windows' caption back when that fails."""
        try:
            hwnd = int(self.root.wm_frame(), 16)
            if hwnd == self.root.winfo_id() and getattr(self, 'chrome_waits', 0) < 100:
                self.chrome_waits = getattr(self, 'chrome_waits', 0) + 1  # not wrapped by Tk yet
                self.root.after(50, self.window_chrome)
                return
            dwm = ctypes.windll.dwmapi

            def attribute(number, value):
                v = ctypes.c_int(value)
                return dwm.DwmSetWindowAttribute(hwnd, number, ctypes.byref(v), ctypes.sizeof(v))
            if attribute(20, 1):  # DWMWA_USE_IMMERSIVE_DARK_MODE; 19 before Windows 10 20H1
                attribute(19, 1)
            attribute(34, 0x000e0c0a)  # DWMWA_BORDER_COLOR (Windows 11): the side bar's dark
            if self.titlebar is not None and self.custom_frame is None:
                frame = sotc_frame.Frame(hwnd, [self.root.winfo_id(), self.titlebar.winfo_id()])
                self.custom_frame = frame if frame.install() else False
                if not self.custom_frame:
                    self.titlebar.pack_forget()
                self.title_state()
        except (AttributeError, OSError, ValueError, self.tk.TclError):
            pass

    def apply_view(self):
        """The side bar's entries: the expert pages only in the expert view."""
        expert = self.var('expert_view').get()
        for name, (c, _items) in self.nav.items():
            c.pack_forget()
        for name, (c, _items) in self.nav.items():
            if expert or name not in EXPERT_PAGES or name == self.current_page:
                c.pack(padx=self.px(12), pady=(0, self.px(2)), before=self.nav_end)

    def nav_paint(self, name, hover=False):
        """A side bar entry: a card behind the current page, a softer one under the mouse."""
        c, items = self.nav[name]
        on = name == self.current_page
        pill = self.nav_pills['on'] if on else self.nav_pills['hover'] if hover else ''
        c.itemconfigure(items['pill'], image=pill, state='normal' if pill else 'hidden')
        c.itemconfigure(items['bar'], state='normal' if on else 'hidden')
        c.itemconfigure(items['icon'], fill=GOLD if on else theme.TEXT_SOFT if hover else MUTED)
        c.itemconfigure(items['label'], fill=theme.TEXT_STRONG if on else TEXT if hover else theme.TEXT_SOFT)

    def build_play(self):
        tk, ttk = self.tk, self.ttk
        page = tk.Frame(self.content, bg=PANEL)
        self.pages['play'] = page
        self.building_page = 'play'
        self.page_titles['play'] = _('Jogar', 'Play')
        # The banner and the settings scroll together when the window is shorter than them.
        canvas = tk.Canvas(page, bg=PANEL, highlightthickness=0, bd=0)
        bar = ttk.Scrollbar(page, orient='vertical', command=canvas.yview)
        inner = tk.Frame(canvas, bg=PANEL)
        window = canvas.create_window(0, 0, window=inner, anchor='nw')
        inner.bind('<Configure>', lambda _e: canvas.configure(scrollregion=canvas.bbox('all')))
        canvas.bind('<Configure>', lambda e: canvas.itemconfigure(window, width=e.width))
        canvas.configure(yscrollcommand=bar.set)
        bar.pack(side='right', fill='y')
        canvas.pack(side='left', fill='both', expand=True)
        page.scroll = lambda units: canvas.yview_scroll(units, 'units') if inner.winfo_height() > canvas.winfo_height() else None
        page.canvas, page.inner = canvas, inner  # the search scrolls to a setting (jump)
        self.banner = tk.Canvas(inner, height=self.px(290), bg=BG, highlightthickness=0, bd=0)
        self.banner.pack(fill='x')
        self.banner.bind('<Configure>', lambda _e: self.draw_banner())
        body = ttk.Frame(inner, padding=(28, 12, 28, 8))
        body.pack(fill='both', expand=True)
        body.columnconfigure(0, weight=3)
        body.columnconfigure(1, weight=2)
        info = ttk.Frame(body)
        info.grid(row=0, column=0, sticky='nw', padx=(0, 24))
        ttk.Label(info, text=_('Estado', 'Status'), style='Section.TLabel').pack(anchor='w', pady=(0, 6))
        self.checks = {}
        for key in ('game', 'dlc', 'sound', 'saves', 'gpu', 'memory'):
            self.checks[key] = ttk.Label(info, text='', justify='left', wraplength=self.px(440))
            self.checks[key].pack(anchor='w', pady=3)
        quick = ttk.Frame(body)
        quick.grid(row=0, column=1, sticky='nw')
        ttk.Label(quick, text=_('Ajustes rápidos', 'Quick settings'), style='Section.TLabel').grid(
            row=0, column=0, columnspan=2, sticky='w', pady=(0, 2))
        self.render_boxes.append(self.row(quick, _('Render', 'Render'), self.choice(quick, 'render_res', RENDER_RES, 24)))
        self.row(quick, _('Modo de jogo', 'Play mode'), self.choice(quick, 'play_mode', PLAY_MODES, 24))
        self.play_mode_hint = ttk.Label(quick, text='', style='Muted.TLabel', wraplength=self.px(300), justify='left')
        self.play_mode_hint.grid(row=self.next_row(quick), column=0, columnspan=2, sticky='w', pady=(2, 2))
        self.row(quick, _('Saída', 'Output'), self.choice(quick, 'output_res', OUTPUTS, 24))
        self.row(quick, _('Upscaler', 'Upscaler'), self.choice(quick, 'upscaler', UPSCALERS, 24))
        self.row(quick, _('Nitidez', 'Sharpening'), self.choice(quick, 'sharpness', SHARPNESS, 24))
        self.framegen_boxes.append(self.row(quick, _('Geração de quadros', 'Frame generation'),
                                            self.choice(quick, 'framegen', FRAMEGEN, 24)))
        self.row(quick, _('FPS', 'FPS'), self.choice(quick, 'fps_mode', FPS_MODES, 24))
        self.check(quick, 'af16', _('Filtragem anisotrópica 16x', '16x anisotropic filtering'))
        self.row(quick, _('Texturas', 'Textures'), self.choice(quick, 'textures', TEXTURES, 24))
        self.row(quick, _('Modo de exibição', 'Display mode'), self.choice(quick, 'window_mode', WINDOW_MODES, 24))
        self.row(quick, _('Memória da placa', 'Card memory'), self.choice(quick, 'vram_profile', VRAM_PROFILES, 24))
        self.vram_profile_hint = ttk.Label(quick, text='', style='Muted.TLabel', wraplength=self.px(300),
                                           justify='left')
        self.vram_profile_hint.grid(row=self.next_row(quick), column=0, columnspan=2, sticky='w', pady=(0, 2))
        self.var('vram_profile').trace_add('write', lambda *_a: self.show_vram_profile_hint())
        self.show_vram_profile_hint()
        self.check(quick, 'neo', _('Modo PS4 Pro', 'PS4 Pro mode'))
        self.row(quick, _('Pular as vinhetas de abertura', 'Skip intro'), self.choice(quick, 'skip_intro', SKIP_INTRO, 24))
        for key in ('af16', 'textures', 'fps_mode', 'window_mode', 'neo', 'output_res', 'render_res', 'upscaler',
                    'sharpness', 'framegen'):
            self.vars[key].trace_add('write', lambda *_a: self.refresh_status())
        self.applying_mode = False
        self.var('play_mode').trace_add('write', lambda *_a: self.apply_play_mode())
        for key in {k for values in PLAY_MODE_VALUES.values() for k in values} | {'upscaler', 'dlss'}:
            self.var(key).trace_add('write', lambda *_a, k=key: self.play_mode_edited(k))
        self.show_play_mode_hint()

    def play_mode_values(self, mode):
        values = dict(PLAY_MODE_VALUES.get(mode, {}))
        if mode == 'extreme':
            values['dlss'] = 'quality' if gpu_has_dlss(getattr(self, 'gpu_name', '') or self.app.get('gpu_name', '')) else 'off'
        return values

    def show_vram_profile_hint(self):
        """What the video memory profile does with the card found."""
        if not hasattr(self, 'vram_profile_hint'):
            return
        mb = int(self.app.get('gpu_vram_mb') or 0)
        card = (_('placa com {:.0f} GB', 'card with {:.0f} GB').format(mb / 1024) if mb
                else _('placa ainda não verificada', 'card not checked yet'))
        current = dict(self.app, vram_profile=self.var('vram_profile').get())
        if light_vram(current):
            text = _('Leve ({}): uso de memória de vídeo no máximo Padrão, sem reserva de envio nela.',
                     'Light ({}): video memory use at most Default, no upload reserve in it.').format(card)
        else:
            text = _('Completo ({}): uso de memória de vídeo como escolhido.',
                     'Full ({}): video memory use as chosen.').format(card)
        self.vram_profile_hint.configure(text=text)

    def show_play_mode_hint(self):
        self.play_mode_hint.configure(text=_(*PLAY_MODE_HINTS.get(self.var('play_mode').get(), ('', ''))))

    def apply_play_mode(self):
        """The chosen mode's settings (Custom changes nothing)."""
        self.show_play_mode_hint()
        values = self.play_mode_values(self.var('play_mode').get())
        self.applying_mode = True
        try:
            for key, value in values.items():
                self.var(key).set(value)
        finally:
            self.applying_mode = False

    def play_mode_edited(self, key):
        """A setting of the mode changed by hand: the mode becomes Custom."""
        if self.applying_mode:
            return
        mode = self.var('play_mode').get()
        values = self.play_mode_values(mode)
        if key in values and str(self.var(key).get()) != str(values[key]):
            self.applying_mode = True
            try:
                self.var('play_mode').set('custom')
                self.show_play_mode_hint()
            finally:
                self.applying_mode = False

    def draw_banner(self):
        """The dump's own art (sce_sys/pic1.png), darker on the left and fading into the page
        (sotc_theme.hero), the game and its version under the title."""
        c = self.banner
        c.delete('all')
        w, h = max(c.winfo_width(), 400), int(c['height'])
        art = Path(self.var('game_dir').get() or '.') / 'sce_sys' / 'pic1.png'
        if self.banner_source != art:
            self.banner_source, self.banner_image = art, None
            try:
                from PIL import Image
                with Image.open(art) as image:
                    self.banner_image = image.convert('RGBA')
            except (ImportError, OSError):
                pass
        if self.banner_image is not None:
            self.banner_photo = self.theme.cached(('hero', str(art), w, h),
                                                  lambda: theme.hero(self.banner_image, (w, h), PANEL))
            c.create_image(0, 0, image=self.banner_photo, anchor='nw')
            c.create_text(33, h - 74, text='Shadow of the Colossus', anchor='w', fill=theme.TEXT_STRONG,
                          font=(title_font(), 26))
        else:
            c.create_text(30, h - 80, text='Shadow of the Colossus', anchor='w', fill=theme.TEXT_STRONG,
                          font=(title_font(), 30))
            c.create_text(w // 2, h // 2 - 40, text=_('Escolha a pasta do jogo na página Jogo.',
                                                       'Select the game folder on the Game page.'),
                          fill=MUTED, font=(ui_font(), 11))
        info = game_info(self.var('game_dir').get())
        sub = (_('{} · versão do jogo {}', '{} · game version {}').format(*info) if info
               else _('Pasta do jogo não escolhida', 'Game folder not set'))
        c.create_text(35, h - 38, text=sub, anchor='w', fill=GOLD, font=(ui_font(), 11))

    def build_game(self):
        ttk = self.ttk
        f = self.scrolled_page('game', _('Jogo', 'Game'),
                               _('Pasta do jogo, DLC e som.', 'Game folder, DLC and sound.'))
        self.section(f, _('Biblioteca de jogos', 'Game library'), top=4)
        self.note(f, _('Adicione as pastas com os seus jogos e selecione Shadow of the Colossus na lista.',
                       'Add the folders that contain your games and select Shadow of the Colossus in the list.'), top=0)
        tree_holder = ttk.Frame(f)
        tree_holder.grid(row=self.next_row(f), column=0, columnspan=2, sticky='we', pady=(6, 0))
        style = self.ttk.Style()
        style.configure('Library.Treeview', rowheight=self.px(40), background=CARD, fieldbackground=CARD,
                        foreground=TEXT, borderwidth=0)
        style.configure('Library.Treeview.Heading', background=PANEL, foreground=MUTED)
        self.library = ttk.Treeview(tree_holder, columns=('id', 'version', 'path'), height=4,
                                    style='Library.Treeview')
        for column, title, width in (('#0', _('Jogo', 'Game'), 300), ('id', 'ID', 100),
                                     ('version', _('Versão', 'Version'), 70), ('path', _('Pasta', 'Folder'), 380)):
            self.library.heading(column, text=title, anchor='w')
            self.library.column(column, width=self.px(width), anchor='w', stretch=column == 'path')
        self.library.pack(side='left', fill='x', expand=True)
        self.library.bind('<Double-1>', lambda _e: self.use_selected_game())
        self.library_icons = {}
        buttons = ttk.Frame(f)
        buttons.grid(row=self.next_row(f), column=0, columnspan=2, sticky='w', pady=(6, 0))
        ttk.Button(buttons, text=_('Usar este jogo', 'Use this game'), command=self.use_selected_game).pack(side='left')
        ttk.Button(buttons, text=_('Adicionar pasta de jogos…', 'Add a games folder…'),
                   command=self.add_library_dir).pack(side='left', padx=(6, 0))
        ttk.Button(buttons, text=_('Remover pasta', 'Remove folder'),
                   command=self.remove_library_dir).pack(side='left', padx=(6, 0))
        ttk.Button(buttons, text=_('Procurar no PC', 'Search this PC'),
                   command=lambda: self.start_discovery(manual=True)).pack(side='left', padx=(6, 0))
        self.library_label = ttk.Label(f, text='', style='Muted.TLabel')
        self.library_label.grid(row=self.next_row(f), column=0, columnspan=2, sticky='w', pady=(4, 0))
        self.section(f, _('Jogo', 'Game'))
        self.folder(f, 'game_dir', _('Pasta do jogo', 'Game folder'), _('Pasta com eboot.bin', 'Folder with eboot.bin'),
                    _('Requer a versão europeia 1.01 (CUSA08809) ou a americana 1.00 (CUSA08034).',
                      'Requires the European version 1.01 (CUSA08809) or the US version 1.00 (CUSA08034).'),
                    on_change=self.game_changed)
        self.section(f, _('Som', 'Sound'))
        self.folder(f, 'sys_modules', _('Módulos de som', 'Sound modules'),
                    _('Pasta com libSceNgs2.sprx e libSceUlt.sprx', 'Folder with libSceNgs2.sprx and libSceUlt.sprx'),
                    _('Opcional: libSceNgs2.sprx e libSceUlt.sprx do seu próprio console.',
                      'Optional: libSceNgs2.sprx and libSceUlt.sprx from your own console.'),
                    on_change=self.refresh_status)
        self.row(f, _('Volume', 'Volume'), self.choice(f, 'volume', VOLUME))
        self.section(f, _('DLC', 'DLC'))
        self.folder(f, 'dlc_dir', _('Pasta de DLC', 'DLC folder'), _('Pasta com as DLCs do jogo', "Folder with the game's DLC"),
                    _('Opcional.', 'Optional.'), on_change=self.refresh_status)
        self.section(f, _('Sistema', 'System'))
        self.row(f, _('Idioma do jogo', 'Game language'), self.choice(f, 'language', LANGUAGES))
        self.row(f, _('Nome do jogador', 'Player name'), ttk.Entry(f, textvariable=self.var('player_name'), width=30))

    def build_graphics(self):
        f = self.scrolled_page('graphics', _('Gráficos', 'Graphics'),
                               _('Configurações de imagem e desempenho.', 'Display and performance settings.'))
        self.section(f, _('Geral', 'General'), top=4)
        self.check(f, 'af16', _('Filtragem anisotrópica 16x', '16x anisotropic filtering'))
        self.row(f, _('Limite de FPS', 'Frame rate limit'), self.choice(f, 'fps_mode', FPS_MODES),
                 _('Acima de 30 FPS, algumas cenas podem se comportar de forma diferente (por exemplo, a luta '
                   'contra Avion); acima de 60 FPS, o vídeo de abertura pode fechar o jogo.',
                   'Above 30 FPS, some scenes may behave differently (for example, the fight with Avion); '
                   'above 60 FPS, the opening movie may close the game.'))
        self.framegen_boxes.append(self.choice(f, 'framegen', FRAMEGEN))
        self.row(f, _('Geração de quadros', 'Frame generation'), self.framegen_boxes[-1],
                 _('Recurso experimental; só tem efeito com o limite de 30 FPS.',
                   'Experimental feature; effective only with the 30 FPS limit.'))
        self.mult_box = self.choice(f, 'dlssg_mult', DLSSG_MULT)
        self.row(f, _('Multiplicador de quadros', 'Frame multiplier'), self.mult_box,
                 _('NVIDIA DLSS-G; x3 a x6 e Dinâmico em placas GeForce RTX 50.',
                   'NVIDIA DLSS-G; x3 to x6 and Dynamic on GeForce RTX 50 series cards.'))
        self.row(f, _('Campo de visão', 'Field of view'), self.choice(f, 'fov', FOV),
                 _('Também se aplica às cenas.', 'Also applies to cutscenes.'))
        self.window_icon_row(f)
        self.section(f, _('Resolução', 'Resolution'))
        self.render_boxes.append(self.choice(f, 'render_res', RENDER_RES))
        self.row(f, _('Resolução de render', 'Render resolution'), self.render_boxes[-1],
                 _('Aplica-se apenas a resoluções de saída 16:9.', 'Applies only to 16:9 output resolutions.'))
        self.render_note = self.state_note(f)
        self.dlss_box = self.choice(f, 'dlss', DLSS_MODES)
        self.row(f, 'NVIDIA DLSS', self.dlss_box,
                 _('Substitui a resolução de render; no modo PS4 Pro, amplia a imagem do próprio Pro.',
                   'Replaces the render resolution; in PS4 Pro mode it upscales the Pro\'s own image.') + '\n' +
                 _('No painel da NVIDIA, mantenha o "Modo de baixa latência" em Desligado ou Ligado, não em Ultra: '
                   'com Ultra o DLSS perde FPS.',
                   'In the NVIDIA control panel, keep "Low Latency Mode" at Off or On, not Ultra: with Ultra, DLSS '
                   'loses FPS.'))
        self.dlss_note = self.state_note(f)
        self.row(f, _('Resolução de saída', 'Output resolution'), self.choice(f, 'output_res', OUTPUTS),
                 _('Em telas ultralargas, menus e vídeos podem aparecer esticados.',
                   'On ultrawide screens, menus and videos may appear stretched.'))
        self.row(f, _('Upscaler', 'Upscaler'), self.choice(f, 'upscaler', UPSCALERS),
                 _('Usado em resoluções de saída acima de 1080p.', 'Used for output resolutions above 1080p.'))
        if any(key == 'fsr4' for key, _t in UPSCALERS):
            self.row(f, _('Modo do FSR 4', 'FSR 4 mode'), self.choice(f, 'fsr4_mode', FSR4_MODES),
                     _('Usado quando o upscaler é o AMD FSR 4.1. Substitui a resolução de render.',
                       'Used when the upscaler is AMD FSR 4.1. Replaces the render resolution.'))
        self.row(f, _('Nitidez', 'Sharpening'), self.choice(f, 'sharpness', SHARPNESS))
        self.section(f, _('Modo PS4 Pro', 'PS4 Pro mode'))
        self.check(f, 'neo', _('Ativar o modo PS4 Pro', 'Enable PS4 Pro mode'),
                   _('Adiciona o modo gráfico do PS4 Pro em Opções > Apresentação; reduz o desempenho.',
                     'Adds the PS4 Pro graphics mode under Options > Presentation; lowers performance.'))
        self.section(f, _('Desempenho', 'Performance'))
        self.check(f, 'perf_culling', _('Otimização de culling (recomendado)', 'Culling optimization (recommended)'))
        self.check(f, 'perf_record_thread', _('Renderização multithread (recomendado)',
                                               'Multithreaded rendering (recommended)'),
                   _('Desative se o jogo travar.', 'Turn off if the game freezes.'))
        self.check(f, 'perf_dma', _('Reflexos completos da água', 'Full water reflections'),
                   _('Desativar melhora o desempenho; não é usado em placas AMD e Intel.',
                     'Turning it off improves performance; not used on AMD and Intel graphics cards.'))
        if not re.search('nvidia|intel', str(self.gpu_name), re.I):
            self.check(f, 'amd_gcn_compat', _('Compatibilidade AMD GCN (RX 470–590, Vega; em teste)',
                                              'AMD GCN compatibility (RX 470–590, Vega; testing)'),
                       _('Para placas AMD antigas com objetos brancos piscando ou perda do dispositivo. Os shaders são '
                         'compilados novamente na primeira abertura. Se o resultado piorar, desative a opção.',
                         'For older AMD cards with flickering white objects or a lost device. Shaders are rebuilt on '
                         'the first start. If the result is worse, turn this option off.'))
        self.row(f, _('Otimização de envio à GPU', 'GPU submission optimization'), self.choice(f, 'gpu_submit', GPU_SUBMIT),
                 _('Experimental em placas AMD e Intel; se a tela ficar preta, selecione "Automático".',
                   'Experimental on AMD and Intel graphics cards; if the screen turns black, select "Automatic".'))
        self.row(f, _('Sombras distantes', 'Distant shadows'), self.choice(f, 'shadow_reuse', SHADOW_REUSE),
                 _('Ligado: um pouco mais de FPS, mas as sombras distantes podem piscar.',
                   'On: slightly higher FPS, but distant shadows may flicker.'))
        self.row(f, _('Uso de memória de vídeo', 'Video memory usage'), self.choice(f, 'vram_use', VRAM_USE),
                 _('Se ocorrer erro de falta de memória, escolha um nível abaixo.',
                   'If an out-of-memory error occurs, select a lower level.'))
        self.section(f, _('Grama', 'Grass'))
        self.row(f, _('Sombras da grama', 'Grass shadows'), self.choice(f, 'grass_shadows', GRASS_SHADOWS),
                 _('"Longe" reduz o FPS. Aplica-se ao iniciar o jogo.',
                   '"Far" lowers the FPS. Applies when the game starts.'))
        self.row(f, _('Distância da grama', 'Grass distance'), self.choice(f, 'grass_distance', VEG_DISTANCE),
                 _('Também no menu do jogo (F1).', 'Also in the in-game menu (F1).'))
        self.row(f, _('Altura da grama', 'Grass height'), self.choice(f, 'grass_height', VEG_HEIGHT))
        self.row(f, _('Distância do mato', 'Weed distance'), self.choice(f, 'weed_distance', WEED_DISTANCE))
        self.row(f, _('Altura do mato', 'Weed height'), self.choice(f, 'weed_height', VEG_HEIGHT))
        self.row(f, _('Detalhe da grama', 'Grass detail'), self.choice(f, 'grass_detail', GRASS_DETAIL),
                 _('A que distância a grama é desenhada por completo.', 'How far out the grass is drawn complete.'))
        self.row(f, _('Esmaecimento da grama', 'Grass fade'), self.choice(f, 'grass_fade', GRASS_FADE),
                 _('Valores diferentes de 1 deixam a grama mais rala. Aplica-se ao iniciar o jogo.',
                   'Values other than 1 make the grass sparser. Applies when the game starts.'))
        self.section(f, _('Efeitos', 'Effects'))
        for key, title, _off in EFFECTS:
            self.check(f, key, _(*title), _(*EFFECT_HINTS[key]) if key in EFFECT_HINTS else None)
        self.note(f, _('Desfoque de movimento, brilho e filtros ficam no menu do jogo (Opções > Apresentação).',
                       'Motion blur, brightness and filters are in the in-game menu (Options > Presentation).'), top=4)
        self.section(f, _('Tela', 'Display'))
        self.row(f, _('Modo de exibição', 'Display mode'), self.choice(f, 'window_mode', WINDOW_MODES))
        screens = [('', (_('Principal', 'Primary'),) * 2)] + [(m[0], (m[1],) * 2) for m in monitors() if not m[3]]
        if len(screens) > 1 or self.var('game_monitor').get():
            self.row(f, _('Monitor do jogo', 'Game monitor'), self.choice(f, 'game_monitor', screens),
                     _('Em qual monitor a janela do jogo abre (e a tela cheia). Vale na próxima vez que o jogo abrir.',
                       'Which monitor the game window opens on (fullscreen too). Applies the next time the game starts.'))
        self.row(f, _('Sincronização vertical', 'Vertical sync'), self.choice(f, 'present_mode', PRESENT_MODES))
        self.check(f, 'hdr', _('HDR (experimental)', 'HDR (experimental)'),
                   _('Só com o HDR ligado no Windows e uma tela HDR.', 'Only with HDR on in Windows and an HDR display.'))

    def build_controls(self):
        ttk = self.ttk
        f = self.scrolled_page('controls', _('Controles', 'Controls'),
                               _('Controles conectados são detectados automaticamente.',
                                 'Connected gamepads are detected automatically.'))
        self.section(f, _('Teste do controle', 'Controller test'), top=4)
        self.controller_card(f, PORT_DIR).grid(row=self.next_row(f), column=0, columnspan=2, sticky='we',
                                              pady=(self.px(4), self.px(6)))
        self.section(f, _('Controle', 'Gamepad'))
        self.row(f, _('Controle preferido', 'Preferred gamepad'), ttk.Entry(f, textvariable=self.var('gamepad'), width=30),
                 _('Nome do controle a usar quando houver vários conectados.',
                   'Name of the gamepad to use when several are connected.'))
        self.row(f, _('Ícones dos botões', 'Button prompts'), self.choice(f, 'pad_glyphs', PAD_GLYPHS, 44))
        self.row(f, _('Intensidade da vibração', 'Vibration strength'), self.choice(f, 'vibration', VIBRATION, 20))
        self.section(f, _('Mouse', 'Mouse'))
        self.check(f, 'mouse', _('Câmera com o mouse', 'Mouse camera control'),
                   _('Clique na janela do jogo para capturar o mouse; pressione Esc para liberá-lo.',
                     'Click the game window to capture the mouse; press Esc to release it.'))
        self.row(f, _('Sensibilidade', 'Sensitivity'), self.choice(f, 'mouse_sens', MOUSE_SENS, 20))
        self.check(f, 'mouse_invert', _('Inverter eixo vertical', 'Invert vertical axis'))
        self.section(f, _('Teclado', 'Keyboard'))
        self.note(f, _('Clique em uma ação e pressione a nova tecla ou botão do mouse; aplica-se no próximo início do jogo.',
                       'Click an action and press the new key or mouse button; changes apply the next time the game '
                       'starts.'), top=0)
        self.key_buttons = {}
        for name, default, label in KEY_INPUTS:
            r = self.next_row(f)
            ttk.Label(f, text=_(*label)).grid(row=r, column=0, sticky='w', padx=(0, 24), pady=1)
            button = ttk.Button(f, width=30, command=lambda n=name: self.capture_key(n))
            button.grid(row=r, column=1, sticky='w', pady=1)
            self.key_buttons[name] = button
        holder = ttk.Frame(f)
        holder.grid(row=self.next_row(f), column=0, columnspan=2, sticky='w', pady=(8, 0))
        ttk.Button(holder, text=_('Restaurar padrões', 'Restore defaults'),
                   command=self.reset_keys).pack(side='left')
        self.show_keys()

    def show_keys(self):
        """Each action's key and mouse buttons; * marks a changed one."""
        keys = self.app.setdefault('keys', {})
        mouse = self.app.setdefault('mouse_buttons', {})
        labels = dict(MOUSE_BUTTONS)
        for name, default, _label in KEY_INPUTS:
            value = (keys[name] or '—') if name in keys else default
            buttons = [_(*labels[b]) for b in mouse_binding(mouse, name).split(',') if b in labels]
            if buttons:
                value += '  ·  ' + ', '.join(buttons)
            self.key_buttons[name].configure(text=value + (' *' if name in keys or name in mouse else ''))

    def reset_keys(self):
        self.app['keys'] = {}
        self.app['mouse_buttons'] = {}
        self.show_keys()

    def capture_key(self, name):
        """A small window that waits for the new key of one action."""
        tk, ttk = self.tk, self.ttk
        window = tk.Toplevel(self.root)
        window.title(_('Nova tecla', 'New key'))
        window.configure(bg=PANEL)
        window.transient(self.root)
        window.resizable(False, False)
        label = next(_(*l) for n, _d, l in KEY_INPUTS if n == name)
        ttk.Label(window, text=_('Pressione a tecla ou o botão do mouse para: {}',
                                 'Press the key or mouse button for: {}').format(label),
                  font=(ui_font(), 12)).pack(padx=30, pady=(24, 6))
        hint = ttk.Label(window, text=_('Esc cancela', 'Esc cancels'), style='Muted.TLabel')
        hint.pack(padx=30, pady=(0, 22))

        def pressed(event):
            if event.keysym == 'Escape':
                window.destroy()
                return
            key = sdl_key_name(event.keysym)
            if not key:
                hint.configure(text=_('Tecla não suportada: {}', 'Unsupported key: {}').format(event.keysym))
                return
            keys = self.app.setdefault('keys', {})
            default = next(d for n, d, _l in KEY_INPUTS if n == name)
            if key == default:
                keys.pop(name, None)
            else:
                keys[name] = key
            # The key leaves any other action that had it (one action per key keeps it clear).
            for other, other_default, _l in KEY_INPUTS:
                if other != name and (keys[other] if other in keys else other_default) == key:
                    keys[other] = ''
            self.show_keys()
            window.destroy()

        def clicked(event):
            button = TK_MOUSE_BUTTONS.get(event.num)
            if not button:
                return
            if name not in MOUSE_INPUTS:
                hint.configure(text=_('Esta ação não aceita botões do mouse.', 'This action does not take mouse buttons.'))
                return
            self.app['mouse_buttons'] = bind_mouse_button(self.app.get('mouse_buttons'), name, button)
            self.show_keys()
            window.destroy()
        window.bind('<KeyPress>', pressed)
        window.bind('<ButtonPress>', clicked)
        window.grab_set()
        window.focus_force()

    def build_debug(self):
        f = self.scrolled_page('debug', _('Depuração', 'Debug'),
                               _('Ferramentas de teste. Aplicam-se na próxima abertura do jogo.',
                                 'Test tools. They apply the next time the game starts.'))
        self.section(f, _('Menu de depuração do jogo', "The game's debug menu"), top=4)
        self.check(f, 'debug_menu', _('Menu de depuração (F6 ou Start+Select no jogo)',
                                      'Debug menu (F6 or Start+Select in game)'),
                   _('O menu de testes da Bluepoint. O jogo pode ficar menos estável com ele ativado.',
                     "Bluepoint's test menu. The game may be less stable with it enabled."))
        self.section(f, _('Desempenho', 'Performance'))
        self.check(f, 'rb_hot', _('Leituras rápidas da GPU (experimental)', 'Fast GPU readbacks (experimental)'),
                   _('Mais FPS, mas podem surgir texturas com cores erradas.',
                     'Higher FPS, but textures with wrong colours may appear.'))
        self.section(f, _('Diagnóstico', 'Diagnostics'))
        self.check(f, 'debug_watch', _('Vigia da memória de texturas', 'Texture memory watch'),
                   _('Registra em user/events.log a memória de texturas reaproveitada pelo jogo ainda em uso.',
                     'Writes to user/events.log when the game reuses memory of textures still in use.'))
        self.check(f, 'dlss_compare', _('Comparar DLSS com FSR 1 (F9)', 'Compare DLSS with FSR 1 (F9)'),
                   _('Com o NVIDIA DLSS ativado: F9 alterna entre DLSS e FSR 1.',
                     'With NVIDIA DLSS enabled: F9 switches between DLSS and FSR 1.'))
        self.section(f, _('Teclas no jogo', 'In-game keys'))
        for key, text in (('F5', _('Salva as texturas do quadro atual em user/log/texcap_N.',
                                    'Saves the textures of the current frame to user/log/texcap_N.')),
                          ('F9', _('Recarrega as texturas da memória do jogo (com a comparação do DLSS: '
                                   'alterna entre DLSS e FSR 1).',
                                   'Reloads the textures from game memory (with the DLSS comparison: switches '
                                   'between DLSS and FSR 1).'))):
            self.row(f, key, self.ttk.Label(f, text=text))

    def build_advanced(self):
        ttk = self.ttk
        f = self.scrolled_page('advanced', _('Avançado', 'Advanced'),
                               _('Opções do lançador e diagnóstico.', 'Launcher options and diagnostics.'))
        self.section(f, _('Lançador', 'Launcher'), top=4)
        self.row(f, _('Idioma do lançador', 'Launcher language'), self.choice(f, 'ui_language', UI_LANGUAGES))
        self.vars['ui_language'].trace_add('write', lambda *_a: self.switch_language())
        self.check(f, 'close_on_play', _('Fechar o lançador quando o jogo abrir', 'Close the launcher when the game starts'))
        self.check(f, 'minimize_to_tray', _('Minimizar para a bandeja do Windows', 'Minimize to the system tray'),
                   _('Minimizado, o lançador sai da barra de tarefas e fica como um ícone perto do relógio: um clique '
                     'o traz de volta, o botão direito tem Abrir e Sair.',
                     'Minimized, the launcher leaves the task bar for an icon by the clock: a click brings it back, the '
                     'right button has Open and Quit.'))
        self.check(f, 'check_updates', _('Procurar atualizações ao abrir', 'Check for updates at startup'))
        holder = ttk.Frame(f)
        holder.grid(row=self.next_row(f), column=0, columnspan=2, sticky='w', pady=(10, 0))
        ttk.Button(holder, text=_('Atalho na área de trabalho', 'Desktop shortcut'), command=self.shortcut).pack(side='left')
        ttk.Button(holder, text=_('Atalho para jogar direto', 'Shortcut that starts the game'),
                   command=lambda: self.shortcut(play=True)).pack(side='left', padx=(6, 0))
        # A second row: on a narrow window (a laptop) the fourth button fell off the window's edge.
        holder = ttk.Frame(f)
        holder.grid(row=self.next_row(f), column=0, columnspan=2, sticky='w', pady=(6, 0))
        ttk.Button(holder, text=_('Pasta do port', 'Port folder'), command=lambda: self.open_path(PORT_DIR)).pack(side='left')
        ttk.Button(holder, text=_('Limpar cache de shaders', 'Clear shader cache'), command=self.clear_cache).pack(side='left', padx=(6, 0))
        self.section(f, _('Diagnóstico', 'Diagnostics'))
        self.check(f, 'show_fps', _('FPS no registro', 'FPS in the log'))
        self.row(f, _('Leituras da GPU', 'GPU readbacks'), self.choice(f, 'readbacks', READBACKS))
        self.row(f, _('Cvars do motor', 'Engine cvars'), ttk.Entry(f, textvariable=self.var('engine_cvars'), width=58),
                 _('NOME=valor, separados por espaço.', 'NAME=value, separated by spaces.'))
        self.row(f, _('Variáveis de ambiente', 'Environment variables'), ttk.Entry(f, textvariable=self.var('extra_env'), width=58),
                 _('NOME=valor, separados por espaço.', 'NAME=value, separated by spaces.'))

    def build_log(self):
        tk, ttk = self.tk, self.ttk
        page = ttk.Frame(self.content, padding=(20, 14, 20, 8))
        self.pages['log'] = page
        top = ttk.Frame(page)
        top.pack(fill='x', pady=(0, 8))
        ttk.Label(top, text=_('Registro', 'Log'), font=(title_font(), 20)).pack(side='left')
        ttk.Button(top, text=_('Copiar', 'Copy'), command=self.copy_log).pack(side='right')
        ttk.Button(top, text=_('Limpar', 'Clear'), command=lambda: self.set_log('')).pack(side='right', padx=6)
        self.log = tk.Text(page, wrap='none', bg='#090a0b', fg='#c9ccd0', insertbackground=TEXT, relief='flat',
                           font=('Consolas', 9), padx=8, pady=6, state='disabled', highlightthickness=0)
        bar = ttk.Scrollbar(page, command=self.log.yview)
        self.log.configure(yscrollcommand=bar.set)
        bar.pack(side='right', fill='y')
        self.log.pack(fill='both', expand=True)

    # ---- state -------------------------------------------------------------------------------
    def game_changed(self):
        game = self.var('game_dir').get()
        if game_info(game) and not self.var('dlc_dir').get():
            from sotc_discovery import find_dlc_root
            dlc = find_dlc_root(game)
            if dlc:
                self.var('dlc_dir').set(dlc)
        self.banner_source = None
        self.draw_banner()
        self.set_icon()
        self.refresh_status()

    def refresh_status(self):
        info = game_info(self.var('game_dir').get())
        if not info:
            game = _('✗ Jogo não encontrado na pasta escolhida', '✗ Game not found in the selected folder')
        elif info[0] in GAMES and (bad := damaged_archives(self.var('game_dir').get())):
            game = _('⚠ Arquivos do jogo danificados ou incompletos: {}. Copie o jogo de novo.',
                     '⚠ Game files damaged or incomplete: {}. Copy the game again.').format(', '.join(bad))
        elif game_supported(self.var('game_dir').get()):
            game = _('✓ {} versão {}', '✓ {} version {}').format(*info)
        else:
            game = _('⚠ {} versão {}: alguns recursos exigem {}',
                     '⚠ {} version {}: some features require {}').format(*info, supported_games())
        modules = Path(self.var('sys_modules').get() or '.')
        if self.var('sys_modules').get() and all((modules / m).is_file() for m in SYS_MODULES):
            sound = _('✓ Som: módulos do seu console', '✓ Sound: your console\'s modules')
        else:
            sound = _('✓ Som: integrado', '✓ Sound: built-in')
        user = user_dir({'user_dir': self.var('user_dir').get()})
        serial = game_serial(self.var('game_dir').get())
        saves = user / 'savedata' / '1' / serial
        save = (_('✓ Saves em {}', '✓ Saves in {}').format(saves) if saves.is_dir() and any(saves.iterdir())
                else _('• Ainda não há saves', '• No saves yet'))
        from sotc_discovery import dlc_count
        count = dlc_count(self.var('dlc_dir').get(), serial)
        dlc = (_('✓ DLC: {} instalada(s)', '✓ DLC: {} installed').format(count) if count
               else _('• Sem DLC', '• No DLC'))
        # A running game holds its own commit: the row keeps what it showed before the start.
        free_mb = None if self.process else commit_free_mb()
        memory = (self.checks['memory'].cget('text') if self.process else
                  '' if free_mb is None or free_mb >= GAME_COMMIT_MB else
                  _('⚠ Memória virtual: {:.1f} GB livres, o jogo precisa de {} GB. Aumente o arquivo de paginação para 16 GB.',
                    '⚠ Virtual memory: {:.1f} GB free, the game needs {} GB. Increase the page file to 16 GB.').format(
                      free_mb / 1024, math.ceil(GAME_COMMIT_MB / 1024)))
        for key, text in (('game', game), ('dlc', dlc), ('sound', sound), ('saves', save), ('gpu', self.gpu_text),
                          ('memory', memory)):
            self.checks[key].configure(text=text, foreground=MUTED if text.startswith('•') else
                                       '#d9b441' if text.startswith('⚠') else '#d36b5c' if text.startswith('✗') else TEXT)
        # The memory row is the last one: shown only with a warning, without an empty line otherwise.
        if memory:
            self.checks['memory'].pack(anchor='w', pady=3)
        else:
            self.checks['memory'].pack_forget()
        if not self.process:
            self.play_button.configure(state='normal' if info else 'disabled')
            self.status.configure(text=self.summary() if info else _('Escolha primeiro a pasta do jogo.',
                                                                     'Choose the game folder first.'), fg=MUTED)

    def summary(self):
        dlss = dlss_mode(self.upscaling_state())
        parts = [dict((k, _(*v)) for k, v in FPS_MODES).get(self.var('fps_mode').get(), '30 FPS'),
                 self.var('output_res').get().replace('x', ' × '),
                 _(*dict(DLSS_MODES)[dlss]) if dlss else
                 _('render {}', '{} render').format(render_height(self.var('render_res').get())),
                 {'fsr1': 'FSR 1', 'fsr4': 'FSR 4'}.get(self.var('upscaler').get(), 'bilinear')]
        framegen = self.var('framegen').get()
        if framegen != 'off':
            parts.append({'auto': _('geração de quadros', 'frame generation'), 'dlss': 'DLSS-G',
                          'fsr': 'FSR 3.1'}.get(framegen, framegen))
        if self.var('af16').get():
            parts.append(_('AF 16x', '16x AF'))
        if self.var('neo').get():
            parts.append('PS4 Pro' if not is_wide(self.var('output_res').get())
                         else _('PS4 Pro desligado (tela larga)', 'PS4 Pro off (wide screen)'))
        parts.append(_(*dict(WINDOW_MODES).get(self.var('window_mode').get(), WINDOW_MODES[0][1])))
        return '   ·   '.join(parts)

    def detect_gpu(self):
        exe = PORT_DIR / 'bin' / 'bb-gpu-capabilities.exe'
        if not exe.is_file():
            exe = PORT_DIR / 'out' / 'bb-gpu-capabilities.exe'
        env = dict(os.environ)
        if not (exe.parent / 'SDL3.dll').is_file():
            clang64 = Path(os.environ.get('MSYS2_ROOT', r'C:\msys64')) / 'clang64' / 'bin'
            env['PATH'] = f'{clang64}{os.pathsep}{env.get("PATH", "")}'
        text = _('• Placa de vídeo: não verificada', '• Graphics card: not checked')
        gpu_line = ''
        try:
            result = subprocess.run([str(exe), '--live-resolution'], capture_output=True, text=True, timeout=30,
                                    env=env, creationflags=NO_WINDOW)
            names = [line[5:].split(':')[0] for line in result.stderr.splitlines() if line.startswith('GPU: ')]
            if names:
                self.gpu_name = names[0]
                found = re.search(r'(\d+)\s*MiB', names[0])
                if found:
                    self.app['gpu_vram_mb'] = int(found.group(1))
                    self.ui_calls.put(self.show_vram_profile_hint)
                text = _('✓ Placa de vídeo: {}', '✓ Graphics card: {}').format(names[0])
                gpu_line = names[0]
            elif result.returncode:
                text = _('✗ Nenhuma placa com Vulkan 1.3 (atualize o driver)',
                         '✗ No Vulkan 1.3 graphics card found (update the driver)')
        except (OSError, subprocess.TimeoutExpired):
            pass
        self.gpu_text = text
        self.ui_calls.put(lambda: self.tune_for_gpu(gpu_line) if gpu_line else self.refresh_status())

    def tune_for_gpu(self, gpu_line):
        """Once per graphics card: a weak one starts at a lower render resolution (the settings
        keep any choice made later, and a resolution chosen before is not replaced)."""
        name = gpu_line.split(',')[0].strip()
        self.gpu_name = self.app['gpu_name'] = name
        self.refresh_gpu_choices()
        if self.app.get('gpu_tuned') == name:
            self.collect()
            self.refresh_status()
            return
        self.app['gpu_tuned'] = name
        render = weak_gpu_render_res(gpu_line)
        if render and not self.var('render_res').get():
            self.var('render_res').set(render)
            self.gpu_text += _(' — resolução de render ajustada para {}',
                               ' — render resolution set to {}').format(render[3:] + 'p')
        self.collect()
        self.refresh_status()

    def upscaling_state(self):
        return {'dlss': self.var('dlss').get(), 'upscaler': self.var('upscaler').get(),
                'fsr4_mode': self.var('fsr4_mode').get(), 'output_res': self.var('output_res').get(),
                'neo': self.var('neo').get(), 'gpu_name': self.gpu_name}

    def refresh_gpu_choices(self):
        """The choices that depend on the graphics card: DLSS-G only on an RTX 40/50 (a saved DLSS-G
        becomes automatic, i.e. FSR 3.1), DLSS Super Resolution only on an RTX card. An unknown card
        keeps every choice; the game checks the support again at start."""
        dlss_g = not self.gpu_name or gpu_has_dlss_g(self.gpu_name)
        if not dlss_g and self.var('framegen').get() == 'dlss':
            self.var('framegen').set('auto')
        for box in self.framegen_boxes:
            box.set_options(FRAMEGEN if dlss_g else FRAMEGEN_FSR)
        if getattr(self, 'mult_box', None):
            mfg = not self.gpu_name or gpu_has_mfg(self.gpu_name)
            self.mult_box.configure(state='readonly' if mfg else 'disabled')
        self.refresh_upscaling()

    def refresh_upscaling(self):
        """DLSS Super Resolution and the render resolution it overrides: enabled or not, and why."""
        if not getattr(self, 'dlss_box', None):
            return
        blocked = dlss_blocked(self.upscaling_state())
        self.dlss_box.configure(state='disabled' if blocked else 'readonly')
        self.dlss_note.configure(text={
            'gpu': _('Requer uma placa de vídeo NVIDIA GeForce RTX.', 'Requires an NVIDIA GeForce RTX graphics card.'),
        }.get(blocked, ''))
        state = self.upscaling_state()
        active = bool(dlss_mode(state) or fsr4_mode(state))
        for box in self.render_boxes:
            box.configure(state='disabled' if active else 'readonly')
        self.render_note.configure(text=_('Definida pelo modo do upscaler.', 'Set by the upscaler mode.')
                                   if active else '')

    def collect(self):
        for key, var in self.vars.items():
            self.app[key] = var.get()
        text = json.dumps(self.app, indent=2, ensure_ascii=False)
        global settings_in_use
        try:
            CONFIG_DIR.mkdir(parents=True, exist_ok=True)
            CONFIG_FILE.write_text(text, encoding='utf-8')
            settings_in_use = CONFIG_FILE
        except OSError:  # a read-only port folder (Program Files): the shared settings
            LEGACY_CONFIG_DIR.mkdir(parents=True, exist_ok=True)
            LEGACY_CONFIG_FILE.write_text(text, encoding='utf-8')
            settings_in_use = LEGACY_CONFIG_FILE

    def import_save(self):
        serial = game_serial(self.var('game_dir').get())
        found = shadps4_saves(serial)
        initial = str(found[0]) if found else str(PORT_DIR.parent)
        chosen = self.filedialog.askdirectory(title=_('Pasta savedata/{} do shadPS4', "shadPS4's savedata/{} folder").format(serial),
                                              initialdir=initial)
        if not chosen:
            return
        source = Path(chosen)
        # A savedata folder: its subfolder of this game, else of the other release (EU and US
        # saves are of the same game build). The save goes to this game's folder.
        if source.name not in GAMES:
            source = next((source / s for s in (serial, *GAMES) if (source / s).is_dir()), source)
        target = user_dir({'user_dir': self.var('user_dir').get()}) / 'savedata' / '1' / serial
        # sotc: one save folder picked (it holds sce_sys/param.sfo): import it as one save. The
        # runtime creates the port's metadata for saves that come without it.
        single = (source / 'sce_sys' / 'param.sfo').is_file()
        try:
            if not single and target.exists() and any(target.iterdir()):
                if not self.messagebox.askyesno('Shadow of the Colossus', _(
                        'Já há saves em {}. Substituir? (uma cópia fica em {}.bak)',
                        'There are saves in {} already. Replace them? (a copy stays in {}.bak)').format(target, target.name)):
                    return
                backup = target.with_name(target.name + '.bak')
                shutil.rmtree(backup, ignore_errors=True)
                target.rename(backup)
            target.mkdir(parents=True, exist_ok=True)
            shutil.copytree(source, target / source.name if single else target, dirs_exist_ok=True)
        except OSError as error:
            self.messagebox.showerror('Shadow of the Colossus', _('Não foi possível importar o save: {}',
                                                                 'Could not import the save: {}').format(error))
            return
        self.refresh_status()
        self.messagebox.showinfo('Shadow of the Colossus', _('Save importado para {}.', 'Save imported to {}.').format(target))

    def build_help(self):
        """The Help page: asking for help (the report, the issues page), then the problems any
        installation can run into, each with what to do and buttons that lead there. After the
        bloodborne_pc launcher's Help page."""
        ttk = self.ttk
        f = self.scrolled_page('help', _('Ajuda', 'Help'),
                               _('Os problemas mais comuns e o que fazer. A busca (Ctrl+F) também acha estes tópicos.',
                                 'The most common problems and what to do. The search (Ctrl+F) finds these topics too.'))
        issues = 'https://github.com/MMadmer/ShadowOfTheColossus-PC/issues'
        topics = [
            (_('Pedir ajuda', 'Asking for help'),
             _('Gere um relatório (registros das últimas sessões, quedas e um resumo do PC) e anexe numa issue no '
               'GitHub, dizendo o que aconteceu e onde no jogo.',
               'Make a report (the last sessions\' logs, crashes and a summary of the PC) and attach it to a GitHub '
               'issue, saying what happened and where in the game.'),
             [(_('Gerar relatório', 'Make a report'), self.save_report, True),
              (_('Abrir as issues', 'Open the issues'), lambda: self.open_url(issues), False)]),
            (_('O jogo não abre ou fecha logo ao abrir', 'The game does not start or closes at once'),
             _('Veja a Verificação na aba Jogar: o dump precisa estar decriptado (eboot.bin e sce_module), na versão '
               'EU 1.01 ou US 1.00. Atualize o driver de vídeo (Vulkan 1.3). Um antivírus pode barrar bin\\bb-probe.exe: '
               'ponha a pasta do port nas exceções. O registro do jogo mostra o motivo da queda.',
               'See the ready check on the Play page: the dump must be decrypted (eboot.bin and sce_module), version '
               'EU 1.01 or US 1.00. Update the graphics driver (Vulkan 1.3). An antivirus can block bin\\bb-probe.exe: '
               'add the port folder to its exceptions. The game log tells why it closed.'),
             [(_('Registro do jogo', 'Game log'), lambda: self.show('log'), False),
              (_('Verificação', 'Ready check'), lambda: self.show('play'), False)]),
            (_('Tela preta ou imagem congelada', 'Black screen or frozen picture'),
             _('Teste com a geração de quadros desligada e o upscaler FSR 1. Atualize o driver de vídeo. Se começou '
               'depois de atualizar o driver ou o port, limpe o cache de shaders (o jogo o refaz na próxima abertura, '
               'com algumas travadas no começo).',
               'Try with frame generation off and the FSR 1 upscaler. Update the graphics driver. If it began after a '
               'driver or port update, clear the shader cache (the game builds it again next time, with a few stutters '
               'at first).'),
             [(_('Geração de quadros', 'Frame generation'), lambda: self.jump_key('framegen'), False),
              (_('Limpar cache de shaders', 'Clear shader cache'), self.clear_cache, False)]),
            (_('Sem som', 'No sound'),
             _('O som do jogo precisa dos módulos libSceNgs2 e libSceUlt do seu próprio console (o port não os traz). '
               'Escolha a pasta onde eles estão; a do shadPS4 serve.',
               'The game\'s sound needs the libSceNgs2 and libSceUlt modules of your own console (the port does not '
               'bring them). Choose the folder that holds them; shadPS4\'s works.'),
             [(_('Módulos de som', 'Sound modules'), lambda: self.jump_key('sys_modules'), False)]),
            (_('Travadas nas trocas de área ou na primeira vez', 'Stutters at area changes or the first time'),
             _('Na primeira vez (e depois de atualizar) o cache de shaders é montado: as travadas somem com o tempo. '
               'Instale o jogo num SSD ou NVMe: num HD as trocas de área travam muito mais.',
               'The first time (and after an update) the shader cache is built: the stutters fade with play. Install '
               'the game on an SSD or NVMe drive: on a hard disk area changes stutter far more.'), []),
            (_('FPS baixo', 'Low frame rate'),
             _('Escolha o Modo de jogo Normal ou Pro, ou 30 FPS. Feche programas por cima do jogo (OBS, RTSS, '
               'Afterburner, gravação do Windows). Em placas com pouca memória, use "Uso de memória de vídeo" Baixo.',
               'Choose the Normal or Pro play mode, or 30 FPS. Close programs over the game (OBS, RTSS, Afterburner, '
               'Windows recording). On cards with little memory, set "Video memory use" to Low.'),
             [(_('Modo de jogo', 'Play mode'), lambda: self.jump_key('play_mode'), False),
              (_('Memória de vídeo', 'Video memory'), lambda: self.jump_key('vram_use'), False)]),
            (_('Texturas coloridas (mosaico) ou borradas', 'Coloured (mosaic) or blurry textures'),
             _('O mosaico depois das cargas foi corrigido na 1.3.0. Texturas borradas: use Texturas Alta ou Máxima e o '
               'filtro 16x. Se o mosaico voltar, gere um relatório logo depois.',
               'The mosaic after loads was fixed in 1.3.0. Blurry textures: use High or Maximum textures and 16x '
               'filtering. If the mosaic comes back, make a report right after.'),
             [(_('Texturas', 'Textures'), lambda: self.jump_key('textures'), False)]),
            (_('O controle não responde', 'The controller does not respond'),
             _('Conecte o controle antes de abrir o jogo. Com vários controles, escolha o preferido na aba Controles. '
               'Pela Steam, desligue o Steam Input para o atalho do jogo.',
               'Connect the controller before starting the game. With several, choose the preferred one on the '
               'Controls page. Through Steam, turn Steam Input off for the game\'s shortcut.'),
             [(_('Controles', 'Controls'), lambda: self.show('controls'), False)]),
            (_('Um save sumiu ou estragou, ou quero levá-lo a outro PC', 'A save is gone or broken, or I want it on another PC'),
             _('A aba Saves guarda um backup a cada vez que o jogo fecha: importe o .zip de antes do problema. Para '
               'outro PC, exporte os saves e importe lá.',
               'The Saves page keeps a backup every time the game closes: import the .zip from before the problem. '
               'For another PC, export the saves and import them there.'),
             [(_('Saves', 'Saves'), lambda: self.show('saves'), False)]),
            (_('Atualizar o port', 'Updating the port'),
             _('O launcher procura versões novas ao abrir. Saves, cache de shaders e sotc.ini são mantidos.',
               'The launcher looks for new versions when it starts. Saves, shader cache and sotc.ini are kept.'),
             [(_('Procurar atualização', 'Check for updates'), lambda: self.check_updates(True), False)]),
        ]
        f.columnconfigure(0, weight=1)
        for k, (title, text, actions) in enumerate(topics):
            card = ttk.Frame(f, style='Card.TFrame', padding=(self.px(18), self.px(14)))
            card.grid(row=self.next_row(f), column=0, columnspan=2, sticky='we', pady=(self.px(4) if k else 0, self.px(6)))
            card.columnconfigure(0, weight=1)
            head = ttk.Label(card, text=title, style='Card.Section.TLabel')
            head.grid(row=0, column=0, sticky='w')
            ttk.Label(card, text=text, style='Card.TLabel', wraplength=self.px(720), justify='left').grid(
                row=1, column=0, sticky='w', pady=(self.px(4), 0))
            if actions:
                buttons = ttk.Frame(card, style='Inner.TFrame')
                buttons.grid(row=2, column=0, sticky='w', pady=(self.px(10), 0))
                for label, command, accent in actions:
                    ttk.Button(buttons, text=label, command=command,
                               style='Card.Accent.TButton' if accent else 'Card.TButton').pack(side='left', padx=(0, self.px(6)))
            self.index_entry(head, title, text, page='help')

    def jump_key(self, key):
        """The page of a setting (not the Play page's copy), scrolled to it and marked."""
        entries = [e for e in getattr(self, 'search_index', []) if e.get('key') == key]
        entry = next((e for e in entries if e['page'] != 'play'), entries[0] if entries else None)
        if entry:
            self.jump(entry)

    def open_url(self, url):
        import webbrowser
        webbrowser.open(url)

    def build_saves(self):
        """The Saves page: the game's saves with their progress, export and import (.zip, a folder,
        shadPS4), backups, the data folder."""
        ttk = self.ttk
        f = self.scrolled_page('saves', _('Saves', 'Saves'),
                               _('Os seus jogos salvos: exportar para guardar ou levar a outro PC, importar, fazer backup.',
                                 'Your saved games: export to keep them or take them to another PC, import, back up.'))
        self.section(f, _('Seus saves', 'Your saves'), top=4)
        holder = ttk.Frame(f)
        holder.grid(row=self.next_row(f), column=0, columnspan=2, sticky='we', pady=(6, 0))
        self.save_list = ttk.Treeview(holder, columns=('detail', 'date'), height=7, style='Library.Treeview',
                                      selectmode='extended')
        for column, title, width in (('#0', _('Save', 'Save'), 430), ('detail', _('Tempo de jogo', 'Play time'), 260),
                                     ('date', _('Gravado em', 'Saved'), 130)):
            self.save_list.heading(column, text=title, anchor='w')
            self.save_list.column(column, width=self.px(width), anchor='w', stretch=column == '#0')
        self.save_list.pack(side='left', fill='x', expand=True)
        self.save_label = ttk.Label(f, text='', style='Muted.TLabel')
        self.save_label.grid(row=self.next_row(f), column=0, columnspan=2, sticky='w', pady=(4, 0))
        buttons = ttk.Frame(f)
        buttons.grid(row=self.next_row(f), column=0, columnspan=2, sticky='w', pady=(8, 0))
        ttk.Button(buttons, text=_('Exportar selecionados…', 'Export selected…'),
                   command=lambda: self.export_saves(selected=True)).pack(side='left')
        ttk.Button(buttons, text=_('Exportar todos…', 'Export all…'),
                   command=lambda: self.export_saves(selected=False)).pack(side='left', padx=(6, 0))
        ttk.Button(buttons, text=_('Atualizar lista', 'Refresh'), command=self.refresh_saves).pack(side='left', padx=(6, 0))
        ttk.Button(buttons, text=_('Abrir pasta', 'Open folder'),
                   command=lambda: self.open_path(self.saves_folder() if self.saves_folder().is_dir()
                                                  else user_dir({'user_dir': self.var('user_dir').get()}))).pack(
            side='left', padx=(6, 0))

        self.section(f, _('Importar', 'Import'))
        self.note(f, _('Os saves com o mesmo nome são substituídos; antes disso o launcher faz um backup de todos.',
                       'Saves with the same name are replaced; the launcher backs them all up first.'), top=0)
        buttons = ttk.Frame(f)
        buttons.grid(row=self.next_row(f), column=0, columnspan=2, sticky='w', pady=(8, 0))
        ttk.Button(buttons, text=_('Importar .zip…', 'Import a .zip…'), command=self.import_saves_zip).pack(side='left')
        ttk.Button(buttons, text=_('Importar pasta…', 'Import a folder…'),
                   command=self.import_saves_folder).pack(side='left', padx=(6, 0))
        ttk.Button(buttons, text=_('Importar do shadPS4…', 'Import from shadPS4…'),
                   command=self.import_save).pack(side='left', padx=(6, 0))
        self.note(f, _('.zip: um arquivo exportado por este launcher. Pasta: um save (com sce_sys/param.sfo), a pasta '
                       'savedata/CUSA08809 de outra instalação ou um backup. shadPS4: a pasta savedata do shadPS4.',
                       '.zip: a file this launcher exported. Folder: one save (with sce_sys/param.sfo), the '
                       'savedata/CUSA08809 folder of another install or a backup. shadPS4: shadPS4\'s savedata folder.'))

        self.section(f, _('Backup', 'Backup'))
        buttons = ttk.Frame(f)
        buttons.grid(row=self.next_row(f), column=0, columnspan=2, sticky='w', pady=(4, 0))
        ttk.Button(buttons, text=_('Fazer backup agora', 'Back up now'), style='Accent.TButton',
                   command=lambda: self.backup_saves(notify=True)).pack(side='left')
        ttk.Button(buttons, text=_('Abrir pasta de backups', 'Open the backups folder'),
                   command=lambda: self.open_path(self.backups_folder())).pack(side='left', padx=(6, 0))
        self.note(f, _('Um .zip com todos os saves em user/backups (os {} mais novos ficam). Para restaurar, use '
                       '"Importar .zip…".', 'A .zip of every save in user/backups (the {} newest are kept). To '
                       'restore one, use "Import a .zip…".').format(SAVE_BACKUPS_KEPT))
        self.check(f, 'exit_backup', _('Backup automático quando o jogo fecha', 'Automatic backup when the game closes'),
                   _('Depois de cada sessão, também quando o jogo cai: um save estragado por uma queda volta pelo '
                     'backup anterior.', 'After every session, also when the game crashes: a save a crash spoiled '
                     'comes back from the backup before.'))

        self.section(f, _('Pasta de dados', 'Data folder'))
        self.folder(f, 'user_dir', _('Pasta de dados', 'Data folder'), _('Pasta dos saves', 'Saves folder'),
                    _('Saves, cache de shaders e capturas. Vazio = a pasta user ao lado do port.',
                      'Saves, shader cache and screenshots. Empty = the user folder next to the port.'),
                    on_change=lambda: (self.refresh_status(), self.refresh_saves()))
        self.refresh_saves()

    def save_entries(self):
        """The game's saves: (folder name, title, play time, saved at) newest first."""
        folder = self.saves_folder()
        entries = []
        try:
            from prepare import sfo
        except ImportError:
            sfo = None
        for path in (folder.iterdir() if folder.is_dir() else ()):
            name = path.name
            if not path.is_dir() or name.startswith('.') or name.endswith(('.sce_sys', '.sce_backup', '.bak')):
                continue
            values = save_param(folder / (name + '.sce_sys') / 'param.bin')
            if not values.get('SUBTITLE'):
                try:
                    values = sfo((path / 'sce_sys' / 'param.sfo').read_bytes()) if sfo else {}
                except (OSError, ValueError):
                    pass
            title = str(values.get('SUBTITLE') or values.get('MAINTITLE') or name)
            stamp = max((p.stat().st_mtime for p in path.rglob('*') if p.is_file()), default=path.stat().st_mtime)
            detail = ' · '.join(line.strip() for line in str(values.get('DETAIL') or '').splitlines() if line.strip())
            entries.append((name, title, detail, stamp))
        return sorted(entries, key=lambda e: -e[3])

    def refresh_saves(self):
        if not getattr(self, 'save_list', None):
            return
        self.save_list.delete(*self.save_list.get_children())
        entries = self.save_entries()
        for name, title, detail, stamp in entries:
            self.save_list.insert('', 'end', iid=name, text='  ' + title,
                                  values=(detail, time.strftime('%d/%m/%Y %H:%M', time.localtime(stamp))))
        self.save_label.configure(text=_('{} save(s) em {}', '{} save(s) in {}').format(len(entries), self.saves_folder())
                                  if entries else _('Nenhum save ainda em {}', 'No saves yet in {}').format(self.saves_folder()))

    def save_parts(self, name):
        """A save's folder and the port's folders beside it (<name>.sce_sys, <name>.sce_backup)."""
        folder = self.saves_folder()
        return [folder / n for n in (name, name + '.sce_sys', name + '.sce_backup') if (folder / n).exists()]

    def write_saves_zip(self, target, names):
        """`names` with their port folders into the .zip `target`, below <title id>/."""
        serial = self.saves_folder().name
        with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED) as archive:
            for name in names:
                for part in self.save_parts(name):
                    for file in part.rglob('*'):
                        if file.is_file():
                            archive.write(file, f'{serial}/{file.relative_to(part.parent).as_posix()}')

    def export_saves(self, selected):
        names = list(self.save_list.selection()) if selected else [e[0] for e in self.save_entries()]
        if not names:
            self.messagebox.showinfo('Shadow of the Colossus', _('Escolha um ou mais saves na lista.',
                                                                 'Choose one or more saves in the list.'))
            return
        default = f'SotC-saves-{time.strftime("%Y-%m-%d")}.zip' if len(names) > 1 else f'SotC-{names[0]}.zip'
        target = self.filedialog.asksaveasfilename(title=_('Exportar saves', 'Export saves'), defaultextension='.zip',
                                                   initialfile=default, filetypes=[('ZIP', '*.zip')])
        if not target:
            return
        try:
            self.write_saves_zip(target, names)
        except OSError as error:
            self.messagebox.showerror('Shadow of the Colossus', _('Não foi possível exportar: {}',
                                                                 'Could not export: {}').format(error))
            return
        self.messagebox.showinfo('Shadow of the Colossus', _('{} save(s) exportado(s) para\n{}',
                                                             '{} save(s) exported to\n{}').format(len(names), target))

    def backups_folder(self):
        folder = user_dir({'user_dir': self.var('user_dir').get()}) / 'backups'
        folder.mkdir(parents=True, exist_ok=True)
        return folder

    def news_items(self):
        """This version's entry of CHANGELOG.md in the launcher's language (Russian, otherwise
        English): one text per item (its continuation lines joined), **bold** kept as markers."""
        try:
            text = (PORT_DIR / 'CHANGELOG.md').read_text(encoding='utf-8')
        except OSError:
            return []
        half = 'Русский' if LANG == 'ru' else 'English'
        items, in_half, inside = [], False, False
        for line in text.splitlines():
            if line.startswith('## '):
                in_half, inside = line[3:].strip() == half, False
            elif line.startswith('### '):
                inside = in_half and line[4:].split(' ')[0] == VERSION
            elif not inside or not line.strip() or re.fullmatch(r'\*\*[^*]+\*\*', line.strip()):
                continue
            elif line.startswith('- '):
                items.append(line[2:].strip())
            elif items:
                items[-1] += ' ' + line.strip()
        return [item.replace('`', '') for item in items]

    def show_news(self):
        """What's new, once per version (news_seen)."""
        tk, ttk = self.tk, self.ttk
        items = self.news_items()
        self.var('news_seen').set(VERSION)
        self.collect()
        if not items:
            return
        win = tk.Toplevel(self.root, bg=PANEL)
        win.title(_('Novidades', "What's new"))
        win.transient(self.root)
        win.geometry(f'{self.px(720)}x{self.px(560)}')
        ttk.Label(win, text=_('Novidades da v{}', "What's new in v{}").format(VERSION), background=PANEL, foreground=GOLD,
                  font=(title_font(), 20)).pack(anchor='w', padx=self.px(24), pady=(self.px(20), self.px(4)))
        tk.Frame(win, bg=LINE, height=1).pack(fill='x', padx=self.px(24), pady=(0, self.px(8)))
        bar = ttk.Frame(win)
        bar.pack(side='bottom', fill='x', padx=self.px(24), pady=self.px(14))
        ttk.Button(bar, text=_('Continuar', 'Continue'), style='Accent.TButton', command=win.destroy).pack(side='right')
        holder = ttk.Frame(win)
        holder.pack(fill='both', expand=True, padx=(self.px(24), self.px(8)))
        body = tk.Text(holder, wrap='word', bg=PANEL, fg=TEXT, relief='flat', bd=0, highlightthickness=0, height=8,
                       font=(ui_font(), 10), spacing1=self.px(6), spacing3=self.px(2), cursor='arrow')
        scroll = ttk.Scrollbar(holder, orient='vertical', command=body.yview)
        body.configure(yscrollcommand=scroll.set)
        scroll.pack(side='right', fill='y')
        body.pack(side='left', fill='both', expand=True)
        body.tag_configure('item', lmargin1=0, lmargin2=self.px(18))
        body.tag_configure('bold', font=(theme.BODY_SEMI if LANG not in ('zh', 'ko') else ui_font(), 10),
                           foreground=theme.TEXT_STRONG)
        body.tag_configure('mark', foreground=SIGIL)
        for item in items:
            body.insert('end', '•  ', ('item', 'mark'))
            for k, part in enumerate(item.split('**')):
                body.insert('end', part, ('item', 'bold') if k % 2 else ('item',))
            body.insert('end', '\n', 'item')
        body.configure(state='disabled')
        win.after(80, lambda: body.yview_moveto(0))  # once the text is laid out at the window's width
        body.bind('<MouseWheel>', lambda e: (body.yview_scroll(int(-e.delta / 120), 'units'), 'break')[1])
        win.bind('<Escape>', lambda _e: win.destroy())
        win.grab_set()

    def save_report(self):
        """A .zip to attach to an issue: the last sessions' logs and events, the newest crashes with
        their dump, the launcher settings (the player name left out) and a summary of this PC."""
        default = Path.home() / 'Desktop' / f'SotC-relatorio-{time.strftime("%Y-%m-%d_%H-%M")}.zip'
        target = self.filedialog.asksaveasfilename(
            title=_('Salvar relatório', 'Save report'), defaultextension='.zip', filetypes=[('ZIP', '*.zip')],
            initialdir=str(default.parent if default.parent.is_dir() else Path.home()), initialfile=default.name)
        if not target:
            return
        self.collect()
        data = user_dir(self.app)
        files = [data / n for n in ('last_run.log', 'last_run.1.log', 'events.log', 'events.1.log')]
        crashes = data / 'crashes'
        if crashes.is_dir():
            logs = sorted(crashes.glob('*.log'), key=lambda p: p.stat().st_mtime)[-6:]
            dumps = sorted(crashes.glob('*.dmp'), key=lambda p: p.stat().st_mtime)[-1:]
            files += logs + dumps
        settings = dict(self.app, player_name='')
        info = game_info(self.app.get('game_dir'))
        summary = '\n'.join([
            f'SotC-PC launcher {VERSION}',
            f'Windows {platform.version()} ({platform.machine()})',
            f'CPU: {platform.processor()}, {os.cpu_count()} threads',
            self.gpu_text.lstrip('✓✗• '),
            'Game: ' + ('{} {}'.format(*info) if info else 'not set'),
            'Summary: ' + self.summary(),
        ])
        try:
            with zipfile.ZipFile(target, 'w', zipfile.ZIP_DEFLATED) as archive:
                archive.writestr('report.txt', summary + '\n')
                archive.writestr('launcher_settings.json', json.dumps(settings, indent=2, ensure_ascii=False))
                for file in files:
                    if file.is_file():
                        archive.write(file, file.relative_to(data).as_posix())
        except OSError as error:
            self.messagebox.showerror('Shadow of the Colossus', _('Não foi possível salvar o relatório: {}',
                                                                 'Could not save the report: {}').format(error))
            return
        self.messagebox.showinfo('Shadow of the Colossus', _(
            'Relatório salvo em\n{}\n\nAnexe este arquivo na issue do GitHub. Ele tem os registros das últimas '
            'sessões e as pastas do jogo e do port (sem o nome do jogador).',
            'Report saved to\n{}\n\nAttach this file to the GitHub issue. It holds the last sessions\' logs and '
            'the game and port folders (without the player name).').format(target))
        self.open_path(Path(target).parent)

    def exit_backup(self):
        """After the game closed (a crash too): the saves into user/backups (Saves > Backup when the
        game closes), the newest SAVE_BACKUPS_KEPT kept as for the manual ones."""
        if not self.var('exit_backup').get():
            return
        try:
            target = self.backup_saves(suffix='-exit')
        except OSError as error:
            self.append(_('Backup dos saves ao fechar falhou: {}', 'Saves backup on exit failed: {}').format(error) + '\n')
            return
        if target:
            self.append(_('Backup dos saves: {}', 'Saves backup: {}').format(target) + '\n')
        self.refresh_saves()

    def backup_saves(self, notify=False, suffix=''):
        """Every save into user/backups/saves-<title id>-<date>.zip; the newest SAVE_BACKUPS_KEPT stay."""
        names = [e[0] for e in self.save_entries()]
        if not names:
            if notify:
                self.messagebox.showinfo('Shadow of the Colossus', _('Ainda não há saves.', 'There are no saves yet.'))
            return None
        folder = self.backups_folder()
        target = folder / f'saves-{self.saves_folder().name}-{time.strftime("%Y-%m-%d_%H-%M-%S")}{suffix}.zip'
        self.write_saves_zip(target, names)
        for old in sorted(folder.glob('saves-*.zip'), key=lambda p: p.stat().st_mtime)[:-SAVE_BACKUPS_KEPT]:
            try:
                old.unlink()
            except OSError:
                pass
        if notify:
            self.messagebox.showinfo('Shadow of the Colossus', _('Backup de {} save(s) em\n{}',
                                                                 'Backup of {} save(s) in\n{}').format(len(names), target))
        return target

    def import_save_folders(self, saves):
        """Copies save folders (each with sce_sys/param.sfo) into the game's saves, with their port
        folders when they come along; backs up everything first."""
        if not saves:
            self.messagebox.showerror('Shadow of the Colossus', _(
                'Nenhum save encontrado aí (um save é uma pasta com sce_sys/param.sfo).',
                'No save found there (a save is a folder with sce_sys/param.sfo).'))
            return
        target = self.saves_folder()
        existing = [p.name for p in saves if (target / p.name).exists()]
        if existing and not self.messagebox.askyesno('Shadow of the Colossus', _(
                'Substituir {} save(s) com o mesmo nome? Um backup de todos é feito antes.',
                'Replace {} save(s) with the same name? Everything is backed up first.').format(len(existing))):
            return
        try:
            if existing:
                self.backup_saves()
            target.mkdir(parents=True, exist_ok=True)
            for save in saves:
                for part in (save, save.with_name(save.name + '.sce_sys'), save.with_name(save.name + '.sce_backup')):
                    if part.is_dir():
                        shutil.rmtree(target / part.name, ignore_errors=True)
                        shutil.copytree(part, target / part.name)
        except OSError as error:
            self.messagebox.showerror('Shadow of the Colossus', _('Não foi possível importar o save: {}',
                                                                 'Could not import the save: {}').format(error))
            return
        self.refresh_saves()
        self.refresh_status()
        self.messagebox.showinfo('Shadow of the Colossus', _('{} save(s) importado(s).', '{} save(s) imported.').format(len(saves)))

    @staticmethod
    def find_saves(folder):
        """The save folders in `folder` or below it: with sce_sys/param.sfo inside (PS4, shadPS4) or
        the port's <name>.sce_sys beside them; not the port's own folders and backups."""
        def is_save(d):
            return (d / 'sce_sys' / 'param.sfo').is_file() or d.with_name(d.name + '.sce_sys').is_dir()
        folder = Path(folder)
        if is_save(folder):
            return [folder]
        skip = ('.sce_sys', '.sce_backup', '.bak')
        found = set()
        for d in folder.rglob('*'):
            if d.is_dir() and not any(part.endswith(skip) for part in d.relative_to(folder).parts) and is_save(d):
                found.add(d)
        # a save's own subfolders (its sce_sys) are not saves
        return sorted(d for d in found if not any(o != d and o in d.parents for o in found))

    def import_saves_folder(self):
        chosen = self.filedialog.askdirectory(title=_('Pasta com os saves', 'Folder with the saves'),
                                              initialdir=str(PORT_DIR.parent))
        if chosen:
            self.import_save_folders(self.find_saves(chosen))

    def import_saves_zip(self):
        chosen = self.filedialog.askopenfilename(title=_('Saves exportados (.zip)', 'Exported saves (.zip)'),
                                                 initialdir=str(self.backups_folder()), filetypes=[('ZIP', '*.zip')])
        if not chosen:
            return
        import tempfile
        with tempfile.TemporaryDirectory(prefix='sotc-saves-') as temp:
            try:
                with zipfile.ZipFile(chosen) as archive:
                    root = Path(temp).resolve()
                    for member in archive.namelist():  # nothing written outside the temporary folder
                        if not (root / member).resolve().is_relative_to(root):
                            raise zipfile.BadZipFile(member)
                    archive.extractall(temp)
            except (OSError, zipfile.BadZipFile) as error:
                self.messagebox.showerror('Shadow of the Colossus', _('Arquivo .zip inválido: {}',
                                                                     'Invalid .zip file: {}').format(error))
                return
            self.import_save_folders(self.find_saves(temp))

    # ---- first start: the setup screen -------------------------------------------------------
    # The guide's steps: (title, under the title), named after the game's journey.
    SETUP_STEPS = [(('O Despertar', 'The Awakening'), ('Boas-vindas', 'Welcome')),
                   (('O Santuário', 'The Shrine'), ('O seu jogo', 'Your game')),
                   (('A Espada Antiga', 'The Ancient Sword'), ('O seu PC', 'Your PC')),
                   (('Agro', 'Agro'), ('Controles', 'Controls')),
                   (('A Terra Proibida', 'The Forbidden Land'), ('Pronto para jogar', 'Ready to play'))]

    def show_setup(self):
        """The first setup as a guide in five steps, as in the bloodborne_pc launcher: the steps on
        the left, the step on the right, Back and Next at the bottom."""
        tk, ttk, px = self.tk, self.ttk, self.px
        if getattr(self, 'setup_frame', None):
            self.setup_frame.destroy()
        top = self.titlebar.winfo_reqheight() if self.titlebar is not None and self.custom_frame is not False else 0
        frame = self.setup_frame = tk.Frame(self.root, bg=PANEL)
        frame.place(x=0, y=top, relwidth=1, relheight=1, height=-top)
        frame.lift()
        rail = tk.Frame(frame, bg=BG, width=px(280))
        rail.pack(side='left', fill='y')
        rail.pack_propagate(False)
        tk.Frame(frame, bg=LINE, width=1).pack(side='left', fill='y')
        tk.Label(rail, text=_('PRIMEIRA JORNADA', 'FIRST JOURNEY'), bg=BG, fg=GOLD, font=(title_font(), 9)).pack(
            anchor='w', padx=px(26), pady=(px(34), 0))
        tk.Label(rail, text=_('Configuração inicial', 'First setup'), bg=BG, fg=theme.TEXT_STRONG,
                 font=(title_font(), 14), wraplength=px(230), justify='left').pack(anchor='w', padx=px(26),
                                                                                  pady=(px(2), px(26)))
        self.setup_rail = []
        for k, (title, sub) in enumerate(self.SETUP_STEPS):
            c = tk.Canvas(rail, width=px(250), height=px(58), bg=BG, highlightthickness=0, bd=0)
            c.pack(padx=px(14))
            self.setup_rail.append((c, _(*title), _(*sub)))
        tk.Frame(rail, bg=BG).pack(fill='both', expand=True)
        skip = tk.Label(rail, text=_('Pular: eu configuro sozinho', 'Skip: I set it up myself'), bg=BG, fg=MUTED,
                        font=(ui_font(), 9, 'underline'), cursor='hand2')
        skip.pack(pady=(0, px(20)))
        skip.bind('<Button-1>', lambda _e: self.finish_setup())
        right = tk.Frame(frame, bg=PANEL)
        right.pack(side='left', fill='both', expand=True)
        bar = tk.Frame(right, bg=BG, height=px(72))
        bar.pack(side='bottom', fill='x')
        bar.pack_propagate(False)
        tk.Frame(right, bg=LINE, height=1).pack(side='bottom', fill='x')
        self.setup_counter = tk.Label(bar, text='', bg=BG, fg=MUTED, font=(title_font(), 10))
        self.setup_counter.pack(side='left', padx=px(30))
        self.setup_next = ttk.Button(bar, text='', style='Bar.Accent.TButton', command=lambda: self.setup_go(1))
        self.setup_next.pack(side='right', padx=(px(8), px(26)))
        self.setup_back = ttk.Button(bar, text=_('Voltar', 'Back'), style='Bar.TButton', command=lambda: self.setup_go(-1))
        self.setup_back.pack(side='right')
        head = tk.Frame(right, bg=PANEL)
        head.pack(fill='x', padx=px(40), pady=(px(26), px(8)))
        self.setup_kicker = tk.Label(head, text='', bg=PANEL, fg=GOLD, font=(title_font(), 9))
        self.setup_kicker.pack(anchor='w')
        self.setup_title = tk.Label(head, text='', bg=PANEL, fg=theme.TEXT_STRONG, font=(title_font(), 24))
        self.setup_title.pack(anchor='w')
        self.setup_lead = tk.Label(head, text='', bg=PANEL, fg=MUTED, font=(ui_font(), 10), justify='left',
                                   wraplength=px(640))
        self.setup_lead.pack(anchor='w', pady=(px(4), 0))
        self.setup_body = tk.Frame(right, bg=PANEL)
        self.setup_body.pack(fill='both', expand=True, padx=px(40), pady=(px(10), 0))
        self.setup_step, self.setup_found_game, self.setup_hw = 0, None, None
        self.setup_found_packages = None
        self.setup_show(0)
        # Meanwhile: the game on this PC (offered with one click) and the PC check.
        current = self.var('game_dir').get()
        if not game_info(current):
            def work():
                try:
                    from sotc_discovery import scan
                    games = scan(seconds=25, extra_roots=[str(PORT_DIR.parent)])['games']
                except Exception:  # never stop the launcher over a search problem
                    games = []
                self.setup_found_game = games[0]['path'] if games else None
                if not games:
                    try:
                        from sotc_pkg import find_packages
                        self.setup_found_packages = next((str(Path(p['path']).parent) for p in find_packages(PORT_DIR.parent, 2)
                                                          if p['kind'] == 'game'), None)
                    except Exception:  # never stop the launcher over a search problem
                        self.setup_found_packages = None
                self.ui_calls.put(lambda: self.setup_step == 1 and self.setup_show(1))
            threading.Thread(target=work, daemon=True).start()

    def setup_paint_rail(self):
        px = self.px
        for k, (c, title, sub) in enumerate(self.setup_rail):
            c.delete('all')
            on, done = k == self.setup_step, k < self.setup_step
            mid = px(29)
            if k < len(self.setup_rail) - 1:  # the path down to the next step
                c.create_line(px(22), mid + px(14), px(22), px(58), fill=GOLD if done else LINE, width=max(1, px(1)))
            ring = SIGIL if on else GOLD if done else theme.BORDER_STRONG
            c.create_oval(px(8), mid - px(14), px(36), mid + px(14), outline=ring, width=max(1, px(1.5)),
                          fill=theme.SIGIL if on else BG)
            numeral = ('I', 'II', 'III', 'IV', 'V')[k]
            c.create_text(px(22), mid, text='✓' if done else numeral, fill=theme.TEXT_STRONG if on else GOLD if done else MUTED,
                          font=(title_font(), 9))
            c.create_text(px(50), mid - px(8), text=title, anchor='w', font=(title_font(), 11),
                          fill=theme.TEXT_STRONG if on else TEXT if done else theme.TEXT_SOFT)
            c.create_text(px(50), mid + px(10), text=sub, anchor='w', font=(ui_font(), 9), fill=MUTED)

    def setup_card(self, title=None, warning=False):
        """A rounded card in the step's body."""
        card = self.ttk.Frame(self.setup_body, style='Card.TFrame', padding=(self.px(20), self.px(16)))
        card.pack(fill='x', pady=(0, self.px(12)))
        if title:
            self.ttk.Label(card, text=title, style='Card.Section.TLabel',
                           foreground='#e07a6a' if warning else None).pack(anchor='w', pady=(0, self.px(6)))
        return card

    def setup_line(self, parent, mark, text):
        color = GOLD if mark == '✓' else '#d9b441' if mark == '⚠' else '#d36b5c' if mark == '✗' else MUTED
        row = self.ttk.Frame(parent, style='Inner.TFrame')
        row.pack(anchor='w', pady=self.px(2), fill='x')
        self.ttk.Label(row, text=mark, style='Card.TLabel', foreground=color, width=2).pack(side='left', anchor='n')
        self.ttk.Label(row, text=text, style='Card.TLabel', wraplength=self.px(600), justify='left').pack(side='left')

    def setup_show(self, step):
        tk, ttk, px = self.tk, self.ttk, self.px
        self.setup_step = step
        for child in self.setup_body.winfo_children():
            child.destroy()
        title, sub = self.SETUP_STEPS[step]
        numeral = ('I', 'II', 'III', 'IV', 'V')[step]
        self.setup_kicker.configure(text=f'{numeral} · {_(*title).upper()}')
        self.setup_counter.configure(text=_('Etapa {} de 5', 'Step {} of 5').format(step + 1))
        self.setup_back.configure(state='normal' if step else 'disabled')
        self.setup_next.configure(text=_('Começar a jornada  ›', 'Begin the journey  ›') if step == 4 else _('Avançar  ›', 'Next  ›'),
                                  state='normal')
        self.setup_paint_rail()
        getattr(self, ('setup_welcome', 'setup_game', 'setup_pc', 'setup_controls', 'setup_ready')[step])()

    def setup_go(self, step):
        target = self.setup_step + step
        if step > 0 and self.setup_step == 2 and getattr(self, 'setup_mode_var', None):
            self.var('play_mode').set(self.setup_mode_var.get())  # the chosen mode sets its settings
        if target > 4:
            self.finish_setup()
            return
        self.collect()
        self.setup_show(max(0, target))

    # -- I · the awakening
    def setup_welcome(self):
        tk, ttk = self.tk, self.ttk
        self.setup_title.configure(text=_('Bem-vindo, viajante', 'Welcome, wanderer'))
        self.setup_lead.configure(text=_('Alguns passos até a Terra Proibida. Tudo pode ser mudado depois no launcher.',
                                         'A few steps to the Forbidden Land. Everything can be changed later in the launcher.'))
        card = self.setup_card()
        row = ttk.Frame(card, style='Inner.TFrame')
        row.pack(anchor='w')
        ttk.Label(row, text=_('Idioma do launcher', 'Launcher language'), style='Card.TLabel').pack(side='left', padx=(0, self.px(14)))
        names = [(code, name) for code, name in UI_LANGUAGES if code]
        lang_var = tk.StringVar(value=dict(names).get(LANG, 'English'))
        combo = ttk.Combobox(row, textvariable=lang_var, values=[n for _c, n in names], state='readonly',
                             width=16, style='Card.TCombobox')
        combo.pack(side='left')

        def chosen(_event=None):
            code = next((c for c, n in names if n == lang_var.get()), '')
            if code and code != LANG:
                self.var('ui_language').set(code)
        combo.bind('<<ComboboxSelected>>', chosen)
        card = self.setup_card(_('Só com a sua própria cópia do jogo', 'Only with your own copy of the game'), warning=True)
        ttk.Label(card, text=_('O port não vem com o jogo e não distribui arquivos dele. Use o dump do seu console '
                               '(decriptado), como no shadPS4.',
                               'The port does not come with the game and shares none of its files. Use the dump of your '
                               'own console (decrypted), as with shadPS4.'),
                  style='Card.TLabel', wraplength=self.px(640), justify='left').pack(anchor='w')
        card = self.setup_card(_('O que você precisa', 'What you need'))
        for mark, text in (('•', _('O dump do jogo ou os seus arquivos .pkg: CUSA08809 (europeu, versão 1.01) ou CUSA08034 '
                                   '(americano, 1.00), com a atualização ao lado.',
                                   'The game dump or its .pkg files: CUSA08809 (European, version 1.01) or CUSA08034 '
                                   '(American, 1.00), with its update next to it.')),
                           ('•', _('Uma placa de vídeo com Vulkan 1.3 e o driver atualizado.',
                                   'A video card with Vulkan 1.3 and an up-to-date driver.')),
                           ('•', _('O jogo num SSD ou NVMe: num HD as trocas de área travam muito mais.',
                                   'The game on an SSD or NVMe drive: on a hard disk area changes stutter far more.'))):
            self.setup_line(card, mark, text)

    # -- II · the shrine
    def setup_game(self):
        ttk = self.ttk
        self.setup_title.configure(text=_('Onde está o seu jogo?', 'Where is your game?'))
        self.setup_lead.configure(text=_('A pasta principal do jogo, como no shadPS4: as subpastas do jogo, da atualização '
                                         '(-UPDATE/-patch) e das DLCs são encontradas sozinhas.',
                                         "The game's main folder, as in shadPS4: the game, update (-UPDATE/-patch) and "
                                         'DLC subfolders are found on their own.'))
        card = self.setup_card()
        row = ttk.Frame(card, style='Inner.TFrame')
        row.pack(anchor='w')
        self.setup_button = ttk.Button(row, text=_('Escolher a pasta do jogo…', 'Choose the game folder…'),
                                       style='Card.Accent.TButton', command=self.choose_game_for_setup)
        self.setup_button.pack(side='left')
        self.setup_hint = ttk.Label(card, text='', style='Card.TLabel', cursor='hand2', wraplength=self.px(640))
        self.setup_hint.pack(anchor='w', pady=(self.px(10), 0))
        self.setup_results = ttk.Frame(self.setup_body, style='TFrame')
        self.setup_results.pack(fill='x')
        current = self.var('game_dir').get()
        if game_info(current):
            self.configure_from_game(current)
        elif self.setup_found_game:
            self.offer_game(self.setup_found_game)
        elif self.setup_found_packages:
            self.offer_packages(self.setup_found_packages)
        else:
            self.setup_hint.configure(text=_('Procurando o jogo neste PC…', 'Looking for the game on this PC…'),
                                      foreground=MUTED)
        if not game_info(current):
            self.setup_next.configure(state='disabled')

    # -- III · the ancient sword
    def setup_pc(self):
        ttk = self.ttk
        self.setup_title.configure(text=_('A força da sua espada', "Your sword's strength"))
        self.setup_lead.configure(text=_('O launcher olha o seu PC e escolhe o modo de jogo que ele aguenta. Você pode '
                                         'trocar aqui ou depois na aba Jogar.',
                                         'The launcher looks at your PC and picks the play mode it can hold. You can '
                                         'change it here or later on the Play page.'))
        self.setup_pc_card = self.setup_card(_('O seu PC', 'Your PC'))
        ttk.Label(self.setup_pc_card, text=_('Verificando…', 'Checking…'), style='Card.TLabel').pack(anchor='w')
        card = self.setup_card(_('Modo de jogo', 'Play mode'))
        row = ttk.Frame(card, style='Inner.TFrame')
        row.pack(anchor='w')
        self.setup_mode_var = self.tk.StringVar(value=self.var('play_mode').get() or 'custom')
        values = [v for v, _t in PLAY_MODES]
        combo = ttk.Combobox(row, values=[_(*t) for _v, t in PLAY_MODES], state='readonly', width=30, style='Card.TCombobox')
        combo.pack(side='left')
        combo.current(values.index(self.setup_mode_var.get()) if self.setup_mode_var.get() in values else 3)
        combo.bind('<<ComboboxSelected>>', lambda _e: self.setup_mode_var.set(values[combo.current()]))
        self.setup_mode_combo, self.setup_mode_values = combo, values
        self.setup_mode_hint = ttk.Label(card, text='', style='Card.TLabel', wraplength=self.px(640), justify='left',
                                         foreground=MUTED)
        self.setup_mode_hint.pack(anchor='w', pady=(self.px(8), 0))
        self.setup_mode_var.trace_add('write', lambda *_a: self.setup_mode_hint.configure(
            text=_(*PLAY_MODE_HINTS.get(self.setup_mode_var.get(), ('', '')))) if self.setup_mode_hint.winfo_exists() else None)
        self.setup_mode_hint.configure(text=_(*PLAY_MODE_HINTS.get(self.setup_mode_var.get(), ('', ''))))
        if self.setup_hw:
            self.setup_pc_done(self.setup_hw)
        else:
            game, gpu = self.var('game_dir').get(), self.gpu_text

            def work():
                info = sotc_hardware.check(game, gpu)
                self.ui_calls.put(lambda: self.setup_pc_done(info, fresh=True))
            threading.Thread(target=work, daemon=True).start()

    def setup_pc_done(self, info, fresh=False):
        self.setup_hw = info
        if self.setup_step != 2 or not self.setup_pc_card.winfo_exists():
            return
        for child in list(self.setup_pc_card.winfo_children())[1:]:
            child.destroy()
        threads, memory, video, disk = info['threads'], info['memory'], info['video_memory'], info['disk']
        self.setup_line(self.setup_pc_card, '✓' if (threads or 0) >= 8 else '⚠',
                        _('Processador: {} ({} threads)', 'Processor: {} ({} threads)').format(info['cpu'] or '?', threads or '?'))
        self.setup_line(self.setup_pc_card, '✓' if (memory or 0) >= 12 else '⚠',
                        _('Memória: {} GB', 'Memory: {} GB').format(memory or '?'))
        self.setup_line(self.setup_pc_card, '✓' if (video or 0) >= 6 else '⚠',
                        self.gpu_text.lstrip('✓✗• ') if video else _('Placa de vídeo: ainda verificando', 'Video card: still checking'))
        self.setup_line(self.setup_pc_card, '✓' if disk in ('nvme', 'ssd') else '⚠' if disk == 'hdd' else '•',
                        {'nvme': _('Disco do jogo: NVMe', 'Game disk: NVMe'), 'ssd': _('Disco do jogo: SSD', 'Game disk: SSD'),
                         'hdd': _('Disco do jogo: HD — instale o jogo num SSD ou NVMe para trocas de área sem travar',
                                  'Game disk: hard disk — install the game on an SSD or NVMe for smooth area changes')}
                        .get(disk, _('Disco do jogo: não foi possível saber o tipo', 'Game disk: type unknown')))
        if fresh or self.setup_mode_var.get() == 'custom':
            mode = sotc_hardware.recommend(info)
            self.setup_mode_var.set(mode)
            self.setup_mode_combo.current(self.setup_mode_values.index(mode))
            self.setup_line(self.setup_pc_card, '›', _('Recomendado para este PC: {}', 'Recommended for this PC: {}').format(
                _(*dict(PLAY_MODES)[mode]).split(' (')[0]))

    # -- IV · agro
    def setup_controls(self):
        self.setup_title.configure(text=_('Agro, sua companheira', 'Agro, your companion'))
        self.setup_lead.configure(text=_('Controle ou teclado e mouse: os dois funcionam juntos. Conecte o controle antes '
                                         'de abrir o jogo.', 'Controller or keyboard and mouse: both work together. Connect '
                                         'the controller before starting the game.'))
        card = self.setup_card(_('Controle', 'Controller'))
        row = self.ttk.Frame(card, style='Inner.TFrame')
        row.pack(anchor='w', pady=(0, self.px(6)))
        self.ttk.Label(row, text=_('Ícones dos botões', 'Button icons'), style='Card.TLabel').pack(side='left', padx=(0, self.px(14)))
        box = self.choice(row, 'pad_glyphs', PAD_GLYPHS, 26)
        box.configure(style='Card.TCombobox')
        box.pack(side='left')
        row = self.ttk.Frame(card, style='Inner.TFrame')
        row.pack(anchor='w')
        self.ttk.Label(row, text=_('Intensidade da vibração', 'Vibration strength'), style='Card.TLabel').pack(side='left', padx=(0, self.px(14)))
        box = self.choice(row, 'vibration', VIBRATION, 26)
        box.configure(style='Card.TCombobox')
        box.pack(side='left')
        card = self.setup_card(_('Teclado e mouse', 'Keyboard and mouse'))
        self.ttk.Checkbutton(card, text=_('Câmera no mouse', 'Mouse camera'), variable=self.var('mouse'),
                             style='Card.TCheckbutton').pack(anchor='w')
        self.ttk.Label(card, text=_('W A S D andam, as setas giram a câmera, Espaço é ✕. As teclas mudam na aba Controles.',
                                    'W A S D move, the arrows turn the camera, Space is ✕. The keys change on the Controls page.'),
                       style='Card.TLabel', wraplength=self.px(640), justify='left', foreground=MUTED).pack(anchor='w', pady=(self.px(6), 0))

    # -- V · the forbidden land
    def setup_ready(self):
        self.setup_title.configure(text=_('A Terra Proibida espera', 'The Forbidden Land awaits'))
        self.setup_lead.configure(text=_('Tudo pronto. A primeira abertura monta o cache de shaders: algumas travadas no '
                                         'começo são normais.', 'All set. The first start builds the shader cache: a few '
                                         'stutters at first are normal.'))
        card = self.setup_card(_('Resumo', 'Summary'))
        info = game_info(self.var('game_dir').get())
        self.setup_line(card, '✓' if info else '✗', _('Jogo: {} (versão {})', 'Game: {} (version {})').format(*info)
                        if info else _('Jogo: não escolhido', 'Game: not chosen'))
        self.setup_line(card, '✓', _('Som: módulos do seu console', "Sound: your console's modules") if self.sound_ready()
                        else _('Som: integrado', 'Sound: built-in'))
        mode = self.var('play_mode').get()
        self.setup_line(card, '✓', _('Modo de jogo: {}', 'Play mode: {}').format(_(*dict(PLAY_MODES).get(mode, ('', '')))))
        self.setup_line(card, '✓', self.summary())

    def offer_game(self, path):
        if not getattr(self, 'setup_hint', None) or not self.setup_hint.winfo_exists():
            return
        self.setup_found_game = path
        self.setup_hint.configure(text=_('Encontrado: {}   —   clique aqui para usar', 'Found: {}   —   click here to use it')
                                  .format(path), foreground=GOLD)
        self.setup_hint.bind('<Button-1>', lambda _e: self.configure_from_game(path))

    def offer_packages(self, folder):
        if not getattr(self, 'setup_hint', None) or not self.setup_hint.winfo_exists():
            return
        self.setup_hint.configure(text=_('Pacote do jogo (.pkg) encontrado em {}   —   clique aqui para desempacotar',
                                         'Game package (.pkg) found in {}   —   click here to unpack it').format(folder),
                                  foreground=GOLD)
        self.setup_hint.bind('<Button-1>', lambda _e: self.unpack_for_setup(folder))

    def unpack_for_setup(self, folder):
        """The game's packages in folder (game, update, every DLC) unpacked next to them (sotc_pkg),
        then the setup goes on with the unpacked game."""
        from sotc_pkg import find_packages, needed_bytes, packages_serial, targets, pkgtool_path
        packages = find_packages(folder, 2)
        if not any(p['kind'] == 'game' for p in packages):
            return False
        if not pkgtool_path(PORT_DIR):
            self.messagebox.showerror('Shadow of the Colossus', _(
                'Falta tools\\PkgTool\\PkgTool.exe na pasta do port.',
                'tools\\PkgTool\\PkgTool.exe is missing from the port folder.'))
            return True
        counts = {kind: sum(p['kind'] == kind for p in packages) for kind in ('game', 'update', 'dlc')}
        size = needed_bytes(packages, folder)
        if size and not self.messagebox.askyesno('Shadow of the Colossus', _(
                'Pacotes do jogo (.pkg) encontrados em\n{}\n(jogo: {}, atualizações: {}, DLC: {}).\n\n'
                'Desempacotar para {}? São necessários {:.1f} GB livres, e o processo leva alguns minutos.',
                'Game packages (.pkg) found in\n{}\n(game: {}, updates: {}, DLC: {}).\n\n'
                'Unpack to {}? This requires {:.1f} GB of free space and takes a few minutes.').format(
                    folder, counts['game'], counts['update'], counts['dlc'], folder, size / 2 ** 30)):
            return True
        self.setup_button.configure(state='disabled')
        self.setup_hint.unbind('<Button-1>')
        self.setup_back.configure(state='disabled')
        self.setup_hint.configure(text=_('Desempacotando…', 'Unpacking…'), foreground=MUTED)

        def progress(done, total):
            text = _('Desempacotando o jogo: {:.0f}% ({:.1f} de {:.1f} GB)', 'Unpacking the game: {:.0f}% ({:.1f} of {:.1f} GB)')
            message = text.format(100 * done / max(total, 1), done / 2 ** 30, total / 2 ** 30)
            self.ui_calls.put(lambda: self.setup_hint.configure(text=message))

        def work():
            from sotc_pkg import unpack
            try:
                places = unpack(packages, folder, PORT_DIR, progress)
            except Exception as error:  # shown to the player, the setup stays open
                message = str(error)
                self.ui_calls.put(lambda: (self.setup_button.configure(state='normal'),
                                           self.setup_back.configure(state='normal'),
                                           self.setup_hint.configure(text='', foreground=MUTED),
                                           self.messagebox.showerror('Shadow of the Colossus', _(
                                               'Não foi possível desempacotar:\n{}', 'Could not unpack:\n{}').format(message))))
                return
            game = places.get('game') or str(targets(folder, packages_serial(packages))['game'])
            self.ui_calls.put(lambda: self.configure_from_game(game, folder))
        threading.Thread(target=work, daemon=True).start()
        return True

    def choose_game_for_setup(self):
        chosen = self.filedialog.askdirectory(title=_('Pasta do jogo Shadow of the Colossus', 'Shadow of the Colossus game folder'),
                                              initialdir=self.setup_found_game or str(PORT_DIR.parent))
        if not chosen:
            return
        from sotc_discovery import find_game_in
        game = find_game_in(chosen)
        if not game and self.unpack_for_setup(chosen):
            return
        if not game and next(Path(chosen).glob('eboot.bin'), None) or not game and next(Path(chosen).glob('*/eboot.bin'), None):
            self.messagebox.showerror('Shadow of the Colossus', _(
                'Há um eboot.bin em\n{}\nmas esta versão do jogo não é compatível ou o arquivo está criptografado.\n\n'
                'Use o dump decriptado de CUSA08809 (1.01) ou CUSA08034 (1.00).',
                'An eboot.bin was found in\n{}\nbut this game version is not supported or the file is encrypted.\n\n'
                'Use the decrypted dump of CUSA08809 (1.01) or CUSA08034 (1.00).').format(chosen))
            return
        if not game:
            self.messagebox.showerror('Shadow of the Colossus', _(
                'O jogo (CUSA08809 ou CUSA08034) não foi encontrado em\n{}\n\nSelecione a pasta do jogo.',
                'The game (CUSA08809 or CUSA08034) was not found in\n{}\n\nSelect the game folder.').format(chosen))
            return
        self.configure_from_game(game, chosen)

    def configure_from_game(self, game, chosen=None):
        """Game chosen: DLC from the game folder, saves, sound — in the background."""
        self.setup_button.configure(state='disabled')
        self.setup_hint.unbind('<Button-1>')
        self.setup_hint.configure(text=_('Configurando…', 'Setting up…'), foreground=MUTED)
        user = user_dir({'user_dir': self.var('user_dir').get()})

        def work():
            from sotc_discovery import find_dlc_root, find_update, dlc_count, scan, SYS_MODULES as modules
            dlc_root = ''
            serial = game_serial(game)
            try:
                from sotc_pkg import find_packages, targets, unpack
                packages = [p for p in find_packages(chosen or Path(game).parent, 2)
                            if p['kind'] == 'dlc' and p['serial'] == serial]
                if packages:
                    folder = Path(game).parent
                    unpack(packages, folder, PORT_DIR)
                    dlc_root = str(targets(folder, serial)['dlc'])
            except Exception:  # the packages stay usable as they are (the runtime reads them)
                dlc_root = ''
            result = {'game': game, 'dlc': dlc_root or find_dlc_root(game, chosen), 'update': find_update(game),
                      'sound': None, 'save': None}
            result['dlc_count'] = dlc_count(result['dlc'], serial)
            # The console's modules are used only when chosen in Game & saves: the port's own
            # sound library plays every sound.
            sound_ready = self.app.get('sys_modules') and all(
                (Path(self.app['sys_modules']) / m).is_file() for m in modules)
            saves = user / 'savedata' / '1' / serial
            has_saves = saves.is_dir() and any(saves.iterdir())
            found = {'sys_modules': [], 'saves': []}
            if not has_saves:
                try:
                    found = scan(seconds=30, extra_roots=[str(Path(game).parent), str(PORT_DIR.parent)])
                except Exception:
                    pass
            if sound_ready:
                result['sound'] = self.app['sys_modules']
            if not has_saves:
                own = user.resolve()
                outside = [p for p in found['saves']
                           if Path(p).name == serial and not Path(p).resolve().is_relative_to(own)]
                if outside:
                    try:
                        saves.parent.mkdir(parents=True, exist_ok=True)
                        shutil.copytree(outside[0], saves, dirs_exist_ok=True)
                        result['save'] = outside[0]
                    except OSError:
                        pass
            self.ui_calls.put(lambda: self.setup_done_screen(result))
        threading.Thread(target=work, daemon=True).start()

    def setup_done_screen(self, result):
        tk, ttk = self.tk, self.ttk
        self.var('game_dir').set(result['game'])
        self.var('dlc_dir').set(result['dlc'])
        if self.var('user_dir').get():  # a data folder picked one level too deep, corrected
            self.var('user_dir').set(str(user_dir({'user_dir': self.var('user_dir').get()})))
        if result['sound']:
            self.var('sys_modules').set(result['sound'])
        folders = self.app.setdefault('library_dirs', [])
        if str(Path(result['game']).parent) not in folders:
            folders.append(str(Path(result['game']).parent))
        if self.setup_step != 1 or not self.setup_results.winfo_exists():
            self.collect()
            return
        self.setup_hint.configure(text='')
        for child in self.setup_results.winfo_children():
            child.destroy()
        info = game_info(result['game'])
        saves = self.saves_folder()
        lines = [
            ('✓', _('Jogo: {} (versão {})', 'Game: {} (version {})').format(*info) if info else result['game']),
            ('✓' if result['update'] else '•',
             _('Atualização: {}', 'Update: {}').format(near_path(result['update'], Path(result['game']).parent.parent))
             if result['update']
             else _('Atualização: não encontrada', 'Update: not found')),
            ('✓' if result['dlc_count'] else '•',
             _('DLC: {} encontrada(s)', 'DLC: {} found').format(result['dlc_count']) if result['dlc_count']
             else _('DLC: não encontrada', 'DLC: not found')),
            ('✓', _('Saves: {}', 'Saves: {}').format(short_path(saves)) + (
                _('  (copiado do shadPS4)', '  (copied from shadPS4)') if result['save'] else '')),
            ('✓', _('Som: módulos do seu console', 'Sound: your console\'s modules') if result['sound'] else
             _('Som: integrado', 'Sound: built-in')),
        ]
        card = ttk.Frame(self.setup_results, style='Card.TFrame', padding=(self.px(20), self.px(16)))
        card.pack(fill='x')
        ttk.Label(card, text=_('Encontrado', 'Found'), style='Card.Section.TLabel').pack(anchor='w', pady=(0, self.px(6)))
        for mark, text in lines:
            self.setup_line(card, mark, text)
        self.setup_button.configure(state='normal', text=_('Trocar a pasta do jogo…', 'Change the game folder…'))
        self.setup_next.configure(state='normal')
        self.collect()

    def choose_sound_for_setup(self):
        chosen = self.filedialog.askdirectory(title=_('Pasta com libSceNgs2.sprx e libSceUlt.sprx',
                                                      'Folder with libSceNgs2.sprx and libSceUlt.sprx'))
        if chosen and all((Path(chosen) / m).is_file() for m in SYS_MODULES):
            target = user_dir(self.app) / 'sys_modules'
            target.mkdir(parents=True, exist_ok=True)
            for module in SYS_MODULES:
                shutil.copy2(Path(chosen) / module, target / module)
            self.var('sys_modules').set(str(target))
            self.messagebox.showinfo('Shadow of the Colossus', _('Som pronto.', 'Sound ready.'))
        elif chosen:
            self.messagebox.showerror('Shadow of the Colossus', _(
                'Essa pasta não tem libSceNgs2.sprx e libSceUlt.sprx.',
                'That folder does not have libSceNgs2.sprx and libSceUlt.sprx.'))

    def finish_setup(self):
        self.app['setup_done'] = True
        self.collect()
        self.setup_frame.destroy()
        self.setup_frame = None
        self.refresh_library()
        self.game_changed()
        self.show('play')

    # ---- library and automatic setup ---------------------------------------------------------
    def library_icon(self, path):
        if not path:
            return None
        if path not in self.library_icons:
            image = None
            try:
                from PIL import Image, ImageTk
                with Image.open(path) as source:
                    size = self.px(36)
                    image = ImageTk.PhotoImage(source.convert('RGBA').resize((size, size), Image.LANCZOS))
            except (ImportError, OSError):
                pass
            self.library_icons[path] = image
        return self.library_icons[path]

    def refresh_library(self):
        from sotc_discovery import library_games
        folders = list(self.app.get('library_dirs') or [])
        current = self.var('game_dir').get()
        extra = [str(Path(current).parent)] if game_info(current) else []
        tree = self.library
        tree.delete(*tree.get_children())
        for game in library_games(folders + extra):
            tree.insert('', 'end', iid=game['path'], text='  ' + game['title'],
                        image=self.library_icon(game['icon']) or '',
                        values=(game['id'], game['version'], game['path']))
            if current and os.path.normcase(game['path']) == os.path.normcase(current):
                tree.selection_set(game['path'])
        self.library_label.configure(text=_('Pastas da biblioteca: {}', 'Library folders: {}').format(
            '; '.join(folders) or _('nenhuma ainda', 'none yet')))

    def use_selected_game(self):
        selection = self.library.selection()
        if selection:
            self.var('game_dir').set(selection[0])

    def add_library_dir(self):
        chosen = self.filedialog.askdirectory(title=_('Pasta com os seus jogos de PS4', 'Folder with your PS4 games'),
                                              initialdir=str(PORT_DIR.parent))
        if chosen:
            folders = self.app.setdefault('library_dirs', [])
            if str(Path(chosen)) not in folders:
                folders.append(str(Path(chosen)))
            self.refresh_library()

    def remove_library_dir(self):
        folders = self.app.get('library_dirs') or []
        selection = self.library.selection()
        chosen = next((f for f in folders if selection and Path(selection[0]).is_relative_to(f)),
                      folders[-1] if folders else None)
        if chosen and self.messagebox.askyesno('Shadow of the Colossus', _(
                'Remover {} da biblioteca? Nenhum arquivo será excluído.',
                'Remove {} from the library? No files will be deleted.').format(chosen)):
            folders.remove(chosen)
            self.refresh_library()

    def sound_ready(self):
        folder = self.var('sys_modules').get()
        return bool(folder) and all((Path(folder) / m).is_file() for m in SYS_MODULES)

    def saves_folder(self):
        return user_dir({'user_dir': self.var('user_dir').get()}) / 'savedata' / '1' / game_serial(self.var('game_dir').get())

    def has_saves(self):
        saves = self.saves_folder()
        return saves.is_dir() and any(saves.iterdir())

    def start_discovery(self, manual=False):
        """Looks for the game, the user's sound modules and shadPS4 saves in the background."""
        if getattr(self, 'discovering', False):
            return
        self.discovering = True
        self.library_label.configure(text=_('Procurando o jogo, os módulos de som e os saves neste PC…',
                                            'Looking for the game, the sound modules and saves on this PC…'))
        roots = [str(PORT_DIR.parent), *(self.app.get('library_dirs') or [])]

        def work():
            try:
                from sotc_discovery import scan
                found = scan(seconds=40, extra_roots=roots)
            except Exception:  # a scan problem never stops the launcher
                found = {'games': [], 'sys_modules': [], 'saves': [], 'dlc': []}
            self.ui_calls.put(lambda: self.apply_discovery(found, manual))
        threading.Thread(target=work, daemon=True).start()

    def apply_discovery(self, found, manual):
        self.discovering = False
        done = []
        folders = self.app.setdefault('library_dirs', [])
        for game in found['games']:
            parent = str(Path(game['path']).parent)
            if parent not in folders:
                folders.append(parent)
        if found['games'] and not game_info(self.var('game_dir').get()):
            best = min(found['games'], key=lambda g: not game_supported(g['path']))
            self.var('game_dir').set(best['path'])
            done.append(_('jogo em {}', 'game in {}').format(best['path']))
        serial = game_serial(self.var('game_dir').get())
        dlc = next((path for path, dlc_serial in found.get('dlc', []) if dlc_serial == serial), None)
        if not self.var('dlc_dir').get() and dlc:
            self.var('dlc_dir').set(dlc)
            done.append(_('DLC em {}', 'DLC in {}').format(dlc))
        if not self.has_saves():
            own = user_dir({'user_dir': self.var('user_dir').get()}).resolve()
            outside = [p for p in found['saves']
                       if Path(p).name == serial and not Path(p).resolve().is_relative_to(own)]
            if outside and self.messagebox.askyesno('Shadow of the Colossus', _(
                    'Foi encontrado um save do jogo em\n{}\nImportar?',
                    'A game save was found in\n{}\nImport it?').format(outside[0])):
                self.copy_save(Path(outside[0]))
                done.append(_('save importado', 'save imported'))
        self.collect()
        self.refresh_library()
        self.refresh_status()
        if done:
            text = _('Configurado automaticamente: {}', 'Set up automatically: {}').format('; '.join(done))
        elif manual:
            text = _('Nada novo encontrado.', 'Nothing new found.')
        else:
            text = self.library_label.cget('text')
        self.library_label.configure(text=text)
        if done:
            self.append(text + '\n')

    def copy_save(self, source):
        target = self.saves_folder()
        if target.exists() and any(target.iterdir()):
            backup = target.with_name(target.name + '.bak')
            shutil.rmtree(backup, ignore_errors=True)
            target.rename(backup)
        target.parent.mkdir(parents=True, exist_ok=True)
        shutil.copytree(source, target, dirs_exist_ok=True)

    # ---- game process ------------------------------------------------------------------------
    def play(self):
        if self.process or self.updating:
            return
        self.collect()
        if not game_info(self.app['game_dir']):
            self.messagebox.showerror('Shadow of the Colossus', _('Selecione a pasta do jogo (CUSA08809 ou CUSA08034).',
                                                                  'Select the game folder (CUSA08809 or CUSA08034).'))
            self.show('game')
            return
        self.set_log('')
        try:
            write_key_bindings(self.app.get('keys'), self.app.get('mouse_buttons'))
        except OSError as error:
            self.append(_('Não foi possível gravar as teclas no sotc.ini: {}',
                          'Could not write the keys to sotc.ini: {}').format(error) + '\n')
        try:
            env = game_environment(self.app)
            env['SOTC_EVENT_LOG'] = start_event_log(user_dir(self.app), self.app, env)
            allow_game_foreground()
            self.process = subprocess.Popen(run_command(), cwd=PORT_DIR, env=env,
                                            stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                            stderr=subprocess.STDOUT, creationflags=NO_WINDOW)
        except OSError as error:
            self.append(_('Não foi possível iniciar: {}', 'Could not start: {}').format(error) + '\n')
            self.process = None
            return
        self.pad_stop()  # the controllers are the game's now
        self.job = GameJob(self.process)
        self.live_at_start = {key: self.app.get(key) for key in self.LIVE_KEYS}
        threading.Thread(target=self.read_output, args=(self.process,), daemon=True).start()
        self.play_button.configure(state='disabled')
        self.stop_button.configure(state='normal')
        self.status.configure(text=_('Iniciando o jogo…', 'Starting the game…'), fg=GOLD)
        if self.app.get('close_on_play'):
            self.root.after(5000, self.root.destroy)

    def read_output(self, process):
        # The output also goes to user/last_run.log, so a crash can be looked at afterwards.
        log_dir = user_dir(self.app)
        try:
            log = open_session_log(log_dir)
        except OSError:
            log = None
        for raw in iter(process.stdout.readline, b''):
            text = raw.decode('utf-8', errors='replace')
            self.output.put(text)
            if log:
                try:
                    log.write(text)
                except (OSError, ValueError):
                    pass
        code = process.wait()
        if log:
            try:
                log.write(f'[exit code {code} = {code & 0xffffffff:#x}]\n')
                log.close()
            except (OSError, ValueError):
                pass
            if not self.stopping:
                keep_crash_log(log_dir, code)
        end_event_log(user_dir(self.app), code, self.stopping)
        self.stopping = False
        self.output.put((code,))

    # Quick settings the in-game menu may have changed (it writes them to the settings file).
    LIVE_KEYS = ('fps_mode', 'framegen', 'dlssg_mult', 'textures', 'shadow_reuse', 'volume', 'upscaler',
                 'sharpness', 'fx_vignette', 'fx_fog', 'fx_ssao', 'fx_shadows', 'perf_dma', 'vibration',
                 'grass_distance', 'grass_height', 'weed_distance', 'weed_height', 'grass_detail',
                 'grass_shadows', 'grass_fade')

    def reload_live_settings(self):
        saved = load_json(settings_in_use, {})
        for key in self.LIVE_KEYS:
            # Only what the game changed: one changed here while it ran stays.
            if (key in saved and key in self.vars and key in self.live_at_start
                    and saved[key] != self.live_at_start[key] and saved[key] != self.vars[key].get()):
                try:
                    self.vars[key].set(saved[key])
                except Exception:  # noqa: BLE001 - a value the widget does not take
                    continue
                self.app[key] = saved[key]
        self.refresh_gpu_choices()

    def drain_output(self):
        while not self.ui_calls.empty():
            self.ui_calls.get_nowait()()
        try:
            for _i in range(500):
                item = self.output.get_nowait()
                if isinstance(item, tuple):
                    self.append(_('\n— o jogo terminou (código {}) —\n', '\n— the game exited (code {}) —\n').format(
                        exit_text(item[0])))
                    self.process = None
                    self.reload_live_settings()
                    if self.job:
                        self.job.close()
                    self.stop_button.configure(state='disabled')
                    self.refresh_status()
                    self.exit_backup()
                else:
                    if 'Entering original x86-64 code' in item:
                        self.status.configure(text=_('O jogo está em execução.', 'The game is running.'), fg=GOLD)
                    self.append(item)
        except queue.Empty:
            pass
        self.root.after(100, self.drain_output)

    def stop(self):
        if self.job:
            self.stopping = True  # stopped from the launcher: not a crash
            self.job.terminate()

    def append(self, text):
        self.log.configure(state='normal')
        self.log.insert('end', text)
        lines = int(self.log.index('end-1c').split('.')[0])
        if lines > MAX_LOG_LINES:
            self.log.delete('1.0', f'{lines - MAX_LOG_LINES}.0')
        self.log.see('end')
        self.log.configure(state='disabled')

    def set_log(self, text):
        self.log.configure(state='normal')
        self.log.delete('1.0', 'end')
        self.log.insert('end', text)
        self.log.configure(state='disabled')

    def copy_log(self):
        self.root.clipboard_clear()
        self.root.clipboard_append(self.log.get('1.0', 'end'))

    def open_path(self, path):
        path = Path(path or PORT_DIR)
        if not path.exists():
            path.mkdir(parents=True, exist_ok=True)
        os.startfile(str(path))

    def clear_cache(self):
        if self.process:
            self.messagebox.showwarning('Shadow of the Colossus', _(
                'Feche o jogo primeiro: o cache de shaders só pode ser limpo com o jogo fechado.',
                'Close the game first: the shader cache can only be cleared while the game is closed.'))
            return
        for name in ('cache', 'shader'):
            shutil.rmtree(user_dir(self.app) / name, ignore_errors=True)
        self.messagebox.showinfo('Shadow of the Colossus', _(
            'Cache de shaders limpo. O próximo início pode apresentar engasgos por alguns minutos.',
            'Shader cache cleared. The next start may stutter for a few minutes.'))

    def shortcut(self, play=False):
        self.collect()
        script = _shortcut_script(play, FROZEN, sys.executable, PORT_DIR, self.game_icon_file())
        result = subprocess.run(['powershell', '-NoProfile', '-Command', script], capture_output=True,
                                creationflags=NO_WINDOW)
        if result.returncode == 0:
            self.messagebox.showinfo('Shadow of the Colossus', _(
                'Atalho criado na área de trabalho. Ele abre o jogo direto e pode ser adicionado ao Steam como '
                'jogo não-Steam.',
                'Shortcut created on the desktop. It starts the game directly and can be added to Steam as a '
                'non-Steam game.') if play else _('Atalho criado na área de trabalho.',
                                                  'Shortcut created on the desktop.'))
        else:
            self.messagebox.showerror('Shadow of the Colossus', result.stderr.decode(errors='replace')[:400])

    # sotc: self-update from the GitHub releases (sotc_update.py).
    def check_updates(self, manual):
        if self.updating:
            return
        self.updating = True
        if manual:
            self.status.configure(text=_('Procurando atualização…', 'Checking for updates…'), fg=MUTED)

        def work():
            plan = None
            try:
                release, error = sotc_update.latest_release(), None
            except Exception as problem:  # offline or rate-limited: only a manual check says so
                release, error = None, problem
            declined = not manual and release and release['version'] == self.app.get('skip_version')
            if FROZEN and sotc_update.is_newer(release, VERSION) and not declined:
                # Only the changed files when the release has an update manifest (the whole zip
                # otherwise): read by range from the release zip, compared with these files.
                try:
                    plan = sotc_update.plan_delta(release, PORT_DIR)
                except Exception:
                    plan = None
            self.ui_calls.put(lambda: self.offer_update(release, error, manual, plan))
        threading.Thread(target=work, daemon=True).start()

    def offer_update(self, release, error, manual, plan=None):
        self.updating = False
        if manual:
            self.refresh_status()
        if error or not sotc_update.is_newer(release, VERSION):
            if manual and error and sotc_update.is_cert_error(error):
                self.messagebox.showwarning('Shadow of the Colossus', _(
                    'Não foi possível verificar a conexão com o GitHub (certificado não reconhecido).\n'
                    'Baixe a versão nova manualmente em:\n{}',
                    'The connection to GitHub could not be verified (certificate not trusted).\n'
                    'You can download the new version manually from:\n{}').format(sotc_update.RELEASES_PAGE))
            elif manual and error:
                self.messagebox.showwarning('Shadow of the Colossus', _(
                    'Não foi possível procurar atualização:\n{}', 'Could not check for updates:\n{}').format(error))
            elif manual:
                self.messagebox.showinfo('Shadow of the Colossus', _(
                    'Você já tem a versão mais recente (v{}).', 'You have the latest version (v{}).').format(VERSION))
            return
        if not manual and release['version'] == self.app.get('skip_version'):
            return
        notes = sotc_update.release_notes(release['notes'], LANG)
        if len(notes) > 900:
            notes = notes[:900].rsplit('\n', 1)[0] + '\n…'
        question = _('Nova versão disponível: v{new} (versão atual: v{cur}).\n\n{notes}\n\n',
                     'A new version is available: v{new} (current version: v{cur}).\n\n{notes}\n\n').format(
            new=release['version'], cur=VERSION, notes=notes)
        if FROZEN:
            size = plan['bytes'] if plan else release['size']
            question += _('Tamanho do download: {}.\n\n', 'Download size: {}.\n\n').format(
                f'{size / 2 ** 20:.1f} MB' if size >= 1 << 20 else f'{max(size >> 10, 1)} KB')
        if not FROZEN:  # running from the sources: nothing to replace, show the release
            if self.messagebox.askyesno('Shadow of the Colossus', question + _('Abrir a página?', 'Open the page?')):
                os.startfile(release['page'])
            return
        if self.messagebox.askyesno('Shadow of the Colossus', question + _(
                'Baixar e instalar agora? Os saves e as configurações serão mantidos.',
                'Download and install now? Your saves and settings will be kept.')):
            self.install_update(release, plan)
        else:
            self.app['skip_version'] = release['version']  # no question at startup for this one again

    def install_update(self, release, plan=None):
        if self.process:
            self.messagebox.showinfo('Shadow of the Colossus', _('Feche o jogo antes de atualizar.',
                                                                 'Close the game before updating.'))
            return
        self.updating = True
        self.play_button.configure(state='disabled')

        def progress(done, total):
            text = _('Baixando a atualização: {} de {} KB', 'Downloading the update: {} of {} KB').format(
                done >> 10, (total >> 10) or '?') if total and total < 1 << 20 else _(
                'Baixando a atualização: {} de {} MB', 'Downloading the update: {} of {} MB').format(
                done >> 20, (total >> 20) or '?')
            self.ui_calls.put(lambda: self.status.configure(text=text, fg=GOLD))

        def work():
            try:
                new_root, error = sotc_update.download(release, PORT_DIR / 'update', progress, plan), None
            except Exception as problem:
                new_root, error = None, problem
            self.ui_calls.put(lambda: self.finish_update(new_root, error))
        threading.Thread(target=work, daemon=True).start()

    def finish_update(self, new_root, error):
        if not error:
            try:
                sotc_update.start_applier(new_root, PORT_DIR)
            except Exception as problem:
                error = problem
        if error and sotc_update.is_cert_error(error):
            error = _('não foi possível verificar a conexão com o GitHub. Baixe manualmente em:\n{}',
                      'the connection to GitHub could not be verified. Download it manually from:\n{}').format(
                sotc_update.RELEASES_PAGE)
        if error:
            self.updating = False
            self.refresh_status()
            self.messagebox.showerror('Shadow of the Colossus', _(
                'A atualização não foi instalada:\n{}', 'The update was not installed:\n{}').format(error))
            return
        self.status.configure(text=_('Instalando a atualização…', 'Installing the update…'), fg=GOLD)
        self.root.after(300, self.close)  # the applier waits for this process to end

    def close(self):
        try:
            self.collect()
        except Exception:  # never keep the window open over a settings problem
            pass
        self.root.destroy()

    def switch_language(self):
        """A new launcher language: the window is built again in it (main), unless the game runs
        from it (its log would be lost); then it applies the next time."""
        if self.process or self.var('ui_language').get() == self.app.get('ui_language', ''):
            return
        self.restart = True
        self.root.after_idle(self.close)


class GameJob:
    """A Windows job holding sotc.py and the bb-probe.exe it starts (Stop ends both)."""

    def __init__(self, process):
        kernel32 = ctypes.windll.kernel32
        kernel32.CreateJobObjectW.restype = ctypes.c_void_p
        self.handle = kernel32.CreateJobObjectW(None, None)
        if self.handle:
            kernel32.AssignProcessToJobObject(ctypes.c_void_p(self.handle), ctypes.c_void_p(int(process._handle)))

    def terminate(self):
        if self.handle:
            ctypes.windll.kernel32.TerminateJobObject(ctypes.c_void_p(self.handle), 1)

    def close(self):
        if self.handle:
            ctypes.windll.kernel32.CloseHandle(ctypes.c_void_p(self.handle))
            self.handle = None


def play_without_window(settings):
    """--play: the game with the saved settings (shortcuts, Steam); output to user/last_run.log."""
    attach_stdio()
    log_dir = user_dir(settings)
    log_dir.mkdir(parents=True, exist_ok=True)
    try:
        write_key_bindings(settings.get('keys'), settings.get('mouse_buttons'))
    except OSError:
        pass
    # The in-game menu replaces only keys already in the settings file: the ones added since the
    # file was last written by the launcher window go in first, or their changes were lost.
    saved = load_json(settings_in_use, {})
    if saved and any(key not in saved for key in Launcher.LIVE_KEYS):
        try:
            missing = {key: settings[key] for key in Launcher.LIVE_KEYS if key not in saved}
            settings_in_use.write_text(json.dumps({**saved, **missing}, indent=2, ensure_ascii=False),
                                       encoding='utf-8')
        except OSError:
            pass
    if not settings.get('gpu_vram_mb'):
        # sotc: the video memory profile's Auto needs the card (the window finds it in detect_gpu).
        exe = PORT_DIR / 'bin' / 'bb-gpu-capabilities.exe'
        try:
            result = subprocess.run([str(exe), '--live-resolution'], capture_output=True, text=True,
                                    timeout=30, creationflags=NO_WINDOW)
            found = re.search(r'^GPU: .*?(\d+)\s*MiB', result.stderr, re.MULTILINE)
            if found:
                settings['gpu_vram_mb'] = int(found.group(1))
        except (OSError, subprocess.TimeoutExpired):
            pass
    with open_session_log(log_dir) as log:
        env = game_environment(settings)
        env['SOTC_EVENT_LOG'] = start_event_log(log_dir, settings, env)
        allow_game_foreground()
        process = subprocess.Popen(run_command(), cwd=PORT_DIR, env=env,
                                   stdin=subprocess.DEVNULL, stdout=subprocess.PIPE,
                                   stderr=subprocess.STDOUT, creationflags=NO_WINDOW)
        for raw in iter(process.stdout.readline, b''):
            text = raw.decode('utf-8', errors='replace')
            log.write(text)
            try:
                sys.stdout.write(text)
            except (OSError, ValueError):
                pass
        code = process.wait()
        log.write(f'[exit code {code} = {code & 0xffffffff:#x}]\n')
    keep_crash_log(log_dir, code)
    end_event_log(log_dir, code)
    return code


def main():
    args = sys.argv[1:]
    if args and args[0] in ('--run', '--script'):
        sys.exit(exit_value(run_role(args)))
    # sotc: --apply-update <new folder> <port folder> <launcher pid>: the copied launcher that
    # installs an update (sotc_update.py). --update-now: check, download and install without
    # the window (scripted tests; SOTC_UPDATE_URL can point at a test release).
    if args and args[0] == '--apply-update' and len(args) == 4:
        sys.exit(0 if sotc_update.apply_update(args[1], args[2], int(args[3])) else 1)
    if args and args[0] == '--update-now':
        attach_stdio()
        try:
            release = sotc_update.latest_release()
            if not sotc_update.is_newer(release, VERSION):
                print(f'up to date (v{VERSION}, latest {release and release["version"]})')
                sys.exit(0)
            try:
                plan = sotc_update.plan_delta(release, PORT_DIR)
            except Exception as problem:
                print(f'no delta ({problem}): the whole release')
                plan = None
            size = plan['bytes'] if plan else release['size']
            print(f'updating v{VERSION} -> v{release["version"]}, {size / 2 ** 20:.1f} MB')
            new_root = sotc_update.download(release, PORT_DIR / 'update', plan=plan)
        except OSError as error:
            if not sotc_update.is_cert_error(error):
                raise
            print(f'the connection to GitHub could not be verified ({error}); '
                  f'download the release manually from {sotc_update.RELEASES_PAGE}')
            sys.exit(1)
        if FROZEN:
            sotc_update.start_applier(new_root, PORT_DIR)
        sys.exit(0)
    settings = load_settings()
    set_language(settings.get('ui_language') or windows_language())
    if '--play' in args and (Path(settings['game_dir'] or '.') / 'eboot.bin').is_file():
        sys.exit(exit_value(play_without_window(settings)))
    try:
        ctypes.windll.shcore.SetProcessDpiAwareness(1)
    except (AttributeError, OSError):
        pass
    import tkinter as tk
    from tkinter import filedialog, messagebox, ttk
    while True:
        root = tk.Tk()
        launcher = Launcher(root, tk, ttk, filedialog, messagebox)
        root.mainloop()
        if not launcher.restart:
            break
        set_language(launcher.app.get('ui_language') or windows_language())


if __name__ == '__main__':
    main()
