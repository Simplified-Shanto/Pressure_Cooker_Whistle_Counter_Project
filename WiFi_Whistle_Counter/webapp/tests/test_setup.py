import contextlib
import importlib.util
import io
import json
import sys
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('counter_setup', Path(__file__).resolve().parents[1] / 'setup.py')
setup = importlib.util.module_from_spec(spec)
spec.loader.exec_module(setup)


class SetupTests(unittest.TestCase):
    def setUp(self):
        self.folder = tempfile.TemporaryDirectory()
        self.addCleanup(self.folder.cleanup)
        self.root = Path(self.folder.name) / 'webapp'
        self.root.mkdir()

    def run_setup(self, *extra):
        args = ['setup.py', '--origin', 'https://counter.example.com', '--contact', 'mailto:test@example.com', *extra]
        with patch.object(setup, 'ROOT', self.root), patch.object(sys, 'argv', args), contextlib.redirect_stdout(io.StringIO()):
            setup.main()

    def test_default_generates_esp8266_header(self):
        self.run_setup()
        header = self.root.parent / 'firmware/Whistle_counter_esp8266_wifi_multimode/ServerConfig.h'
        self.assertTrue(header.exists())
        self.assertIn('PROGMEM', header.read_text())
        self.assertFalse((self.root.parent / 'firmware/Whistle_counter_esp32_wifi_multimode/ServerConfig.h').exists())

    def test_board_switch_preserves_identity_and_push_keys(self):
        self.run_setup()
        config = json.loads((self.root / '.local/config.json').read_text())
        key = (self.root / '.local/vapid.pem').read_bytes()
        self.run_setup('--board', 'esp32')
        after = json.loads((self.root / '.local/config.json').read_text())
        for name in ['device_token', 'pairing_key', 'session_secret', 'vapid_public_key']:
            self.assertEqual(config[name], after[name])
        self.assertEqual(key, (self.root / '.local/vapid.pem').read_bytes())
        self.assertTrue((self.root.parent / 'firmware/Whistle_counter_esp32_wifi_multimode/ServerConfig.h').exists())


if __name__ == '__main__':
    unittest.main()
