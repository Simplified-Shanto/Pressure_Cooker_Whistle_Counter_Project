import json
import os
import tempfile
import unittest
from pathlib import Path
from unittest.mock import patch

from app import load_config


class HostedConfigTests(unittest.TestCase):
    def test_hosting_origin_preserves_existing_device_and_phone_keys(self):
        with tempfile.TemporaryDirectory() as directory:
            path = Path(directory) / 'config.json'
            original = {'public_origin': 'https://old.trycloudflare.com', 'pairing_key': 'pair',
                        'device_token': 'device', 'session_secret': 'session',
                        'vapid_public_key': 'public', 'vapid_subject': 'https://old.trycloudflare.com'}
            path.write_text(json.dumps(original))
            with patch.dict(os.environ, {
                    'COUNTER_CONFIG': str(path), 'COUNTER_DATABASE': '/var/data/counter.sqlite3',
                    'RENDER_EXTERNAL_URL': 'https://counter.onrender.com'}, clear=True):
                hosted = load_config()
            self.assertEqual(hosted['public_origin'], 'https://counter.onrender.com')
            self.assertEqual(hosted['vapid_subject'], hosted['public_origin'])
            self.assertEqual(hosted['database'], '/var/data/counter.sqlite3')
            self.assertEqual(hosted['vapid_private_key'], str(path.parent / 'vapid.pem'))
            for name in ['pairing_key', 'device_token', 'session_secret', 'vapid_public_key']:
                self.assertEqual(hosted[name], original[name])
            self.assertEqual(json.loads(path.read_text()), original)
            with patch.dict(os.environ, {'COUNTER_CONFIG': str(path),
                    'COUNTER_PUBLIC_ORIGIN': 'https://custom.example.com/',
                    'RENDER_EXTERNAL_URL': 'https://counter.onrender.com'}, clear=True):
                self.assertEqual(load_config()['public_origin'], 'https://custom.example.com')
            with patch.dict(os.environ, {'COUNTER_CONFIG': str(path),
                    'COUNTER_PUBLIC_ORIGIN': 'http://counter.example.com'}, clear=True):
                with self.assertRaises(ValueError):
                    load_config()


if __name__ == '__main__':
    unittest.main()
