import json
import tempfile
import unittest
from pathlib import Path

from run_phone_test import prepare_config


class PhoneTestConfigTests(unittest.TestCase):
    def test_test_origin_preserves_existing_setup_and_isolates_runtime(self):
        with tempfile.TemporaryDirectory() as directory:
            root = Path(directory)
            private = root / '.local'
            private.mkdir()
            original = json.dumps({'pairing_key': 'pair', 'session_secret': 'session',
                                   'device_token': 'device', 'vapid_public_key': 'public',
                                   'public_origin': 'https://production.example',
                                   'vapid_subject': 'mailto:owner@example.com'})
            (private / 'config.json').write_text(original)
            (private / 'vapid.pem').write_bytes(b'private key')
            config_path, database = prepare_config(root, 'https://one.trycloudflare.com')
            test = json.loads(config_path.read_text())
            self.assertEqual((private / 'config.json').read_text(), original)
            for name in ['pairing_key', 'session_secret', 'device_token', 'vapid_public_key']:
                self.assertEqual(test[name], json.loads(original)[name])
            self.assertEqual(test['public_origin'], 'https://one.trycloudflare.com')
            self.assertEqual(test['vapid_subject'], test['public_origin'])
            self.assertEqual((config_path.parent / 'vapid.pem').read_bytes(), b'private key')
            self.assertEqual(config_path.stat().st_mode & 0o777, 0o600)
            other_path, other_database = prepare_config(root, 'https://two.trycloudflare.com')
            self.assertNotEqual(database, other_database)
            self.assertNotEqual(config_path, other_path)


if __name__ == '__main__':
    unittest.main()
