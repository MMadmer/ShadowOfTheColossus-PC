"""sotc.py: sotc.ini from its template, exit codes and the Windows crash event."""
from paths import ROOT
import importlib.util
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('sotc_main', ROOT / 'sotc.py')
sotc = importlib.util.module_from_spec(spec)
spec.loader.exec_module(sotc)

EVENT = ("<Event xmlns='http://schemas.microsoft.com/win/2004/08/events/event'><System>"
         "<Provider Name='Application Error'/><EventID>1000</EventID>"
         "<TimeCreated SystemTime='2026-10-28T19:02:11.5000000Z'/></System><EventData>{}</EventData></Event>")
NAMED = ("<Data Name='AppName'>bb-probe.exe</Data><Data Name='AppVersion'>0.0.0.0</Data>"
         "<Data Name='AppTimeStamp'>00000000</Data><Data Name='ModuleName'>ntdll.dll</Data>"
         "<Data Name='ModuleVersion'>10.0.26100.9711</Data><Data Name='ModuleTimeStamp'>1a2b3c4d</Data>"
         "<Data Name='ExceptionCode'>c0000409</Data><Data Name='FaultingOffset'>000000000005c955</Data>"
         "<Data Name='PackageFullName'></Data><Data Name='PackageRelativeAppId'/>")


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


class CvarTests(unittest.TestCase):
    def test_extra_cvars_after_the_settings(self):
        culling = 'doIndexBufferCulling=0 doSunShadowIndexBufferCulling=0'
        self.assertEqual(sotc.engine_cvars({}), culling)
        self.assertEqual(sotc.engine_cvars({'SOTC_CVARS': ''}), '')
        self.assertEqual(sotc.engine_cvars({'SOTC_CVARS': 'fog_enable=0', 'SOTC_EXTRA_CVARS': ' nointro '}),
                         'fog_enable=0 nointro')
        self.assertEqual(sotc.engine_cvars({'SOTC_EXTRA_CVARS': 'nointro'}), culling + ' nointro')


class CrashEventTests(unittest.TestCase):
    LINE = ('Windows Application Error (event 1000) at 2026-10-28 19:02:11 UTC: bb-probe.exe, module ntdll.dll '
            '10.0.26100.9711, exception 0xc0000409, offset 0x5c955')

    def test_named_data(self):
        self.assertEqual(sotc.crash_event_line(EVENT.format(NAMED)), self.LINE)

    def test_unnamed_data_of_older_windows(self):
        values = ('bb-probe-compat.exe', '0.0.0.0', '0', 'ntdll.dll', '10.0.26100.9711', '0', 'c0000409',
                  '000000000005c955', '0x1f0')
        line = sotc.crash_event_line(EVENT.format(''.join(f'<Data>{v}</Data>' for v in values)))
        self.assertEqual(line, self.LINE.replace('bb-probe.exe', 'bb-probe-compat.exe'))

    def test_other_programs_ignored(self):
        self.assertEqual(sotc.crash_event_line(EVENT.format(NAMED.replace('bb-probe.exe', 'game.exe'))), '')
        self.assertEqual(sotc.crash_event_line('not xml'), '')

    @unittest.skipIf(os.name != 'nt', 'Windows exit codes')
    def test_report(self):
        with patch.object(sotc, 'crash_event', side_effect=['', self.LINE]) as event, \
                patch.object(sotc.time, 'sleep'):
            self.assertEqual(sotc.exit_report(0xC0000409, 30),
                             ['bb-probe exited with code 0xc0000409 (fail-fast / stack buffer overrun)', self.LINE])
            self.assertEqual(event.call_count, 2)  # written a moment after the exit: asked again
        with patch.object(sotc, 'crash_event') as event:
            self.assertEqual(sotc.exit_report(3, 30), ['bb-probe exited with code 0x00000003'])
            self.assertEqual(sotc.exit_report(0, 30), [])
            event.assert_not_called()  # the port's own handler reported it: no Windows event

    def test_query_never_fails(self):
        self.assertIsInstance(sotc.crash_event(5), str)


@unittest.skipIf(os.name != 'nt', 'Windows exit codes')
class ExitCodeTests(unittest.TestCase):
    def test_values(self):
        self.assertEqual(sotc.exit_value(0xC0000409), -0x3FFFFBF7)
        self.assertEqual(sotc.exit_value(-0x3FFFFBF7), -0x3FFFFBF7)
        self.assertEqual(sotc.exit_value(3), 3)
        self.assertEqual(sotc.exit_value(0), 0)

    def test_ntstatus_survives_python(self):
        # sotc.py's way: the game's code through sys.exit() (Python 3.10 turned 0xC0000409 into -1).
        crash = 'import ctypes; ctypes.windll.kernel32.ExitProcess(0xC0000409)'
        relay = ('import importlib.util, subprocess, sys\n'
                 f'spec = importlib.util.spec_from_file_location("s", {str(ROOT / "sotc.py")!r})\n'
                 's = importlib.util.module_from_spec(spec); spec.loader.exec_module(s)\n'
                 f'sys.exit(s.exit_value(subprocess.run([sys.executable, "-c", {crash!r}]).returncode))\n')
        self.assertEqual(subprocess.run([sys.executable, '-c', relay]).returncode, 0xC0000409)


if __name__ == '__main__':
    unittest.main()
