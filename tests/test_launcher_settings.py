"""Launcher settings: DLSS and frame generation by graphics card, vibration, mouse button bindings
in sotc.ini, and the update folder search."""
from paths import ROOT
import struct
import sys
import tempfile
import unittest
from pathlib import Path

sys.path.insert(0, str(ROOT / 'launcher'))
try:
    import sotc_launcher_win as launcher  # noqa: E402
except ImportError:  # no Tkinter
    launcher = None
import sotc_discovery  # noqa: E402

RTX_3070 = 'NVIDIA GeForce RTX 3070 Laptop GPU'
RTX_4070 = 'NVIDIA GeForce RTX 4070'
GTX_1050 = 'NVIDIA GeForce GTX 1050'
RX_6600 = 'AMD Radeon RX 6600'


def make_sfo(values):
    """A param.sfo with the given strings (format 0x204), as prepare.sfo reads it."""
    keys = b''.join(k.encode() + b'\0' for k in values)
    entries, data, key_off = b'', b'', 0
    for k, v in values.items():
        raw = v.encode() + b'\0'
        entries += struct.pack('<HHIII', key_off, 0x204, len(raw), len(raw), len(data))
        key_off += len(k) + 1
        data += raw
    keys_at = 20 + len(entries)
    return struct.pack('<4sIIII', b'\0PSF', 0x101, keys_at, keys_at + len(keys), len(values)) + entries + keys + data


@unittest.skipIf(launcher is None, 'launcher not importable')
class GpuChoiceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)

    def env(self, **settings):
        base = {**launcher.APP_DEFAULTS, 'user_dir': self.tmp.name, 'game_dir': str(Path(self.tmp.name) / 'none')}
        return launcher.game_environment({**base, **settings})

    def test_gpu_names(self):
        self.assertTrue(launcher.gpu_has_dlss(RTX_3070))
        self.assertTrue(launcher.gpu_has_dlss('NVIDIA RTX A4000'))
        self.assertFalse(launcher.gpu_has_dlss(GTX_1050))
        self.assertFalse(launcher.gpu_has_dlss(RX_6600))
        self.assertTrue(launcher.gpu_has_dlss_g(RTX_4070))
        self.assertTrue(launcher.gpu_has_dlss_g('NVIDIA GeForce RTX 5070 Ti'))
        self.assertFalse(launcher.gpu_has_dlss_g(RTX_3070))
        self.assertFalse(launcher.gpu_has_dlss_g('NVIDIA RTX A4000'))

    def test_dlss_modes(self):
        for mode, render in (('quality', 'res720'), ('balanced', 'res630'), ('performance', 'res540'),
                             ('ultra', 'res360')):
            env = self.env(dlss=mode, render_res='res900', gpu_name=RTX_3070)
            self.assertEqual(env['SOTC_DLSS'], mode)
            self.assertEqual(env['SOTC_MODES'].split(','), ['af16', render])

    def test_dlss_off_keeps_render_resolution(self):
        env = self.env(dlss='off', render_res='res900')
        self.assertNotIn('SOTC_DLSS', env)
        self.assertEqual(env['SOTC_MODES'], 'af16,res900')

    def test_dlss_needs_16_9_rtx_and_no_pro_mode(self):
        self.assertEqual(self.env(dlss='quality', output_res='2560x1440', gpu_name=RTX_3070)['SOTC_DLSS'], 'quality')
        self.assertEqual(self.env(dlss='quality', gpu_name='')['SOTC_DLSS'], 'quality')  # card not known yet
        for settings, reason in (({'output_res': '2560x1080'}, 'output'), ({'output_res': '1920x1200'}, 'output'),
                                 ({'neo': True}, 'neo'), ({'gpu_name': GTX_1050}, 'gpu'),
                                 ({'gpu_name': RX_6600}, 'gpu')):
            s = {'dlss': 'quality', 'gpu_name': RTX_3070, **settings}
            self.assertEqual(launcher.dlss_blocked({**launcher.APP_DEFAULTS, **s}), reason)
            env = self.env(**s)
            self.assertNotIn('SOTC_DLSS', env, settings)
            self.assertNotIn('res720', env['SOTC_MODES'], settings)

    def test_frame_generation_by_card(self):
        self.assertEqual(self.env(framegen='dlss', gpu_name=RTX_4070)['SOTC_FRAMEGEN'], 'dlss')
        self.assertEqual(self.env(framegen='dlss', gpu_name=RTX_3070)['SOTC_FRAMEGEN'], 'auto')
        self.assertEqual(self.env(framegen='dlss', gpu_name='')['SOTC_FRAMEGEN'], 'dlss')
        self.assertEqual(self.env(framegen='fsr', gpu_name=RX_6600)['SOTC_FRAMEGEN'], 'fsr')

    def test_vibration(self):
        self.assertEqual(self.env()['SOTC_VIBRATION'], '100')
        self.assertEqual(self.env(vibration='0')['SOTC_VIBRATION'], '0')
        self.assertEqual(self.env(vibration='25')['SOTC_VIBRATION'], '25')


@unittest.skipIf(launcher is None, 'launcher not importable')
class MouseBindingTests(unittest.TestCase):
    def test_bind_moves_the_button(self):
        custom = launcher.bind_mouse_button({}, 'cross', 'left')
        self.assertEqual(custom, {'cross': 'left', 'r1': ''})
        # Back to the default: nothing custom is left.
        self.assertEqual(launcher.bind_mouse_button(custom, 'r1', 'left'), {})
        custom = launcher.bind_mouse_button({'triangle': 'x1,x2'}, 'circle', 'x2')
        self.assertEqual(custom, {'triangle': 'x1', 'circle': 'x2'})
        self.assertEqual(launcher.mouse_binding({}, 'square'), 'right')
        self.assertEqual(launcher.mouse_binding({}, 'cross'), '')

    def test_only_button_inputs(self):
        self.assertIn('r1', launcher.MOUSE_INPUTS)
        self.assertIn('touchpad_right', launcher.MOUSE_INPUTS)
        self.assertFalse([n for n in launcher.MOUSE_INPUTS if n.startswith(('move_', 'look_'))])
        self.assertEqual(launcher.TK_MOUSE_BUTTONS[1], 'left')
        self.assertEqual(launcher.TK_MOUSE_BUTTONS[3], 'right')
        self.assertEqual((launcher.TK_MOUSE_BUTTONS[4], launcher.TK_MOUSE_BUTTONS[9]), ('x1', 'x2'))

    def test_ini_lines(self):
        with tempfile.TemporaryDirectory() as tmp:
            ini = Path(tmp) / 'sotc.ini'
            ini.write_text('pad.circle=b\n# key bindings (launcher)\nkey.cross=X\nmouse.l1=x1\n', encoding='utf-8')
            self.assertEqual(launcher.ini_mouse_buttons(ini), {'l1': 'x1'})
            custom = launcher.bind_mouse_button({}, 'cross', 'left')
            launcher.write_key_bindings({'cross': 'V'}, {**custom, 'move_up': 'left'}, ini)
            self.assertEqual(ini.read_text(encoding='utf-8').splitlines(),
                             ['pad.circle=b', '# key bindings (launcher)', 'key.cross=V',
                              '# mouse buttons (launcher)', 'mouse.cross=left', 'mouse.r1='])
            launcher.write_key_bindings({}, {}, ini)
            self.assertEqual(ini.read_text(encoding='utf-8').splitlines(), ['pad.circle=b'])


class FindUpdateTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.root = Path(self.tmp.name)
        self.game = self.root / 'CUSA08809'
        (self.game / 'sce_sys').mkdir(parents=True)

    def folder(self, path, title='CUSA08809', category='gp'):
        (path / 'sce_sys').mkdir(parents=True)
        (path / 'sce_sys' / 'param.sfo').write_bytes(make_sfo({'CATEGORY': category, 'TITLE_ID': title}))
        return str(path)

    def test_none(self):
        self.assertEqual(sotc_discovery.find_update(self.game), '')

    def test_sibling_first(self):
        inside = self.folder(self.game / 'update')
        sibling = self.root / 'CUSA08809-UPDATE'
        sibling.mkdir()
        self.assertEqual(sotc_discovery.find_update(self.game), str(sibling))
        sibling.rmdir()
        self.assertEqual(sotc_discovery.find_update(self.game), inside)

    def test_inside_and_next_to_the_game(self):
        for path in (self.game / 'patch', self.root / 'update', self.root / 'patch'):
            with self.subTest(path=path):
                found = self.folder(path)
                self.assertEqual(sotc_discovery.find_update(self.game), found)
                for item in sorted(Path(found).rglob('*'), reverse=True):
                    item.unlink() if item.is_file() else item.rmdir()
                Path(found).rmdir()

    def test_only_patches_of_the_game(self):
        self.folder(self.game / 'update', category='gd')          # a base game
        self.folder(self.root / 'update', title='CUSA00001')     # another game's patch
        (self.root / 'patch').mkdir()                             # no param.sfo
        self.assertEqual(sotc_discovery.find_update(self.game), '')


if __name__ == '__main__':
    unittest.main()
