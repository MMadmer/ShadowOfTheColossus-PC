"""sotc_modes.py: engine cvars on the command line, the page allocator fix switch."""
from paths import ROOT  # noqa: F401
import contextlib
import io
import os
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch
import sotc_modes

BASE = sotc_modes.BASE
ORIGINALS = {**{address: original for address, original in
                [*sotc_modes.VERSION_CHECKS, sotc_modes.POOL_FIX[:2]]},
             **{address: bytes.fromhex(original) for address, _kind, original in
                [*sotc_modes.RENDER_SITES, sotc_modes.WIDE_ASPECT]}}


def fake_image_bytes(elf, ph, vaddr, length):
    """The original bytes at the version checks, the page allocator and the render size sites of
    EU 1.01 / US 1.00."""
    original = ORIGINALS.get(vaddr + BASE)
    return original[:length] if original else None


def run_modes(*args, env=None):
    """sotc_modes.py on a fake supported eboot: (printed text, {offset: data} written, cmdline text)."""
    clean = {k: v for k, v in os.environ.items() if not k.startswith('SOTC_')}
    with tempfile.TemporaryDirectory() as tmp, \
            patch.object(sotc_modes, 'parse_self', return_value=(b'', None, [], [], [])), \
            patch.object(sotc_modes, 'image_bytes', fake_image_bytes), \
            patch.dict(os.environ, {**clean, **(env or {})}, clear=True), \
            patch.object(sys, 'argv', ['sotc_modes.py', tmp, '--out', tmp, *args]), \
            contextlib.redirect_stdout(io.StringIO()) as printed:
        (Path(tmp) / 'eboot.bin').write_bytes(b'')
        sotc_modes.main()
        blob = (Path(tmp) / 'patches.bin').read_bytes()
        cmdline = Path(tmp) / 'cmdline.bin'
        text = cmdline.read_bytes()[16:].decode('ascii') if cmdline.is_file() else ''
    writes, at = {}, 24
    for _ in range(struct.unpack_from('<8sQQ', blob)[2]):
        offset, size = struct.unpack_from('<QQ', blob, at)
        writes[offset] = blob[at + 16:at + 16 + size]
        at += 16 + size
    return printed.getvalue(), writes, text


class CvarTests(unittest.TestCase):
    def test_forms(self):
        for good in ('nointro', 'r_motionBlurEnable=0', 'VSyncWindow=10%', 'r_sunDir(0.5,-1,0.25)', 'reset()'):
            self.assertTrue(sotc_modes.CVAR.match(good), good)
        for bad in ('1abc=2', 'a=b,c', 'a=(1)', 'a(1', 'a(1);b', 'a=$x'):
            self.assertFalse(sotc_modes.CVAR.match(bad), bad)

    def test_command_line(self):
        _printed, _writes, text = run_modes('--cvars', '+fog_enable=0 r_sunDir(0.5,-1,0.25)')
        self.assertTrue(text.endswith(' +fog_enable=0 +r_sunDir(0.5,-1,0.25)'), text)
        with self.assertRaises(SystemExit):
            run_modes('--cvars', 'a=b,c')


class PoolFixTests(unittest.TestCase):
    FIX = (sotc_modes.POOL_FIX[0] - BASE, sotc_modes.POOL_FIX[2])

    def test_on_by_default(self):
        printed, writes, _text = run_modes()
        self.assertEqual(writes.get(self.FIX[0]), self.FIX[1])
        self.assertIn('Page allocator fix: on', printed)

    def test_switched_off(self):
        # Diagnostics for green or garbage texture reports (AjncSystem #326, #360).
        printed, writes, text = run_modes('--render', '3840x1600', env={'SOTC_POOL_FIX': '0', 'SOTC_NEO': '1'})
        self.assertNotIn(self.FIX[0], writes)
        self.assertIn('Page allocator fix: off (SOTC_POOL_FIX=0)', printed)
        # The texture pool reserve stays: dropping it needs the fix.
        self.assertNotIn('texturePoolReserveSystemMemMiBs=0', text)

    def test_reserve_dropped_with_the_fix(self):
        _printed, _writes, text = run_modes(env={'SOTC_NEO': '1'})
        self.assertIn('+texturePoolReserveSystemMemMiBs=0', text)


if __name__ == '__main__':
    unittest.main()
