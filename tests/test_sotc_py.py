"""sotc.py: sotc.ini from its template."""
from paths import ROOT
import importlib.util
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('sotc_main', ROOT / 'sotc.py')
sotc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sotc)


class IniTests(unittest.TestCase):
    def test_template_copied_once(self):
        with tempfile.TemporaryDirectory() as tmp, patch.object(sotc, 'PORT', Path(tmp)):
            ini, template = Path(tmp) / 'sotc.ini', Path(tmp) / 'sotc.ini.example'
            self.assertEqual(sotc.ini_file(), ini)
            self.assertFalse(ini.exists())
            template.write_text('SOTC_MODES=af16\n', encoding='utf-8')
            self.assertEqual(sotc.ini_file(), ini)
            self.assertEqual(ini.read_text(encoding='utf-8'), 'SOTC_MODES=af16\n')
            # The player's own sotc.ini is never replaced by a newer template (#136).
            ini.write_text('SOTC_MODES=\n', encoding='utf-8')
            template.write_text('SOTC_MODES=fps60\n', encoding='utf-8')
            self.assertEqual(sotc.ini_file(), ini)
            self.assertEqual(ini.read_text(encoding='utf-8'), 'SOTC_MODES=\n')


if __name__ == '__main__':
    unittest.main()
