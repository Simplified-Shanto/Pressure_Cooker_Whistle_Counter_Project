import base64
import json
import os
import tempfile
import unittest
from pathlib import Path

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec
from pywebpush import webpush

from app import create_app
from store import Store
from worker import deliver_one


def subscription():
    key = ec.generate_private_key(ec.SECP256R1()).public_key()
    public = key.public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
    encode = lambda b: base64.urlsafe_b64encode(b).rstrip(b'=').decode()
    return {'endpoint': 'https://fcm.googleapis.com/fcm/send/test-phone',
            'keys': {'auth': encode(b'a' * 16), 'p256dh': encode(public)}}


class CounterTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.now = 1800000000.0
        self.config = {'database': str(Path(self.tmp.name) / 'counter.sqlite3'),
                       'device_token': 'device-test-secret', 'pairing_key': 'phone-test-secret',
                       'session_secret': 'session-test-secret', 'public_origin': 'https://counter.example.com',
                       'vapid_public_key': 'test-public-key', 'vapid_subject': 'mailto:test@example.com',
                       'vapid_private_key': str(Path(self.tmp.name) / 'vapid.pem')}
        key = ec.generate_private_key(ec.SECP256R1())
        Path(self.config['vapid_private_key']).write_bytes(key.private_bytes(
            serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8, serialization.NoEncryption()))
        self.app = create_app(self.config)
        self.client = self.app.test_client()
        self.store = self.app.extensions['counter_store']
        self.store.clock = lambda: self.now
        self.sub = subscription()
        self.store.subscribe(self.sub)
        self.sent = []
        self.drain()

    def drain(self):
        while deliver_one(self.store, self.config, lambda **kw: self.sent.append(json.loads(kw['data']))):
            pass

    def heartbeat(self, boot='boot1', count=0):
        return {'boot_id': boot, 'mode': 'listening', 'count': count, 'target': 3, 'reset_reason': 'software reset', 'uptime_ms': 500}

    def event(self, seq=1, kind='whistle', count=1, boot='boot1'):
        return {'event_id': f'{boot}:{seq}', 'boot_id': boot, 'session_id': f'{boot}:s1',
                'type': kind, 'count': count, 'target': 3, 'uptime_ms': 1000, 'occurred_at': int(self.now)}

    def phone_post(self, path, body, method='POST'):
        return self.client.open(path, method=method, json=body, headers={'Origin': self.config['public_origin']})

    def login(self):
        return self.phone_post('/api/login', {'pairing_key': self.config['pairing_key']})

    def test_startup_connection_only_once(self):
        self.store.heartbeat(self.heartbeat())
        self.store.heartbeat(self.heartbeat())
        self.drain()
        self.assertEqual([p['title'] for p in self.sent], ['Notifications enabled', 'Counter got connected'])

    def test_each_count_and_separate_final_notification(self):
        for i in range(1, 4):
            self.store.record_device_event(self.event(i, count=i))
        self.store.record_device_event(self.event(4, 'completed', 3))
        self.drain()
        self.assertEqual([p['body'] for p in self.sent[1:]], [
            'Total: 1 of 3 whistles.', 'Total: 2 of 3 whistles.', 'Total: 3 of 3 whistles.',
            'Completed: 3 of 3 whistles.'])
        self.assertEqual(len({p['event_id'] for p in self.sent}), 5)

    def test_retried_event_is_deduplicated(self):
        self.assertTrue(self.store.record_device_event(self.event()))
        self.assertFalse(self.store.record_device_event(self.event()))
        self.drain()
        self.assertEqual(len(self.sent), 2)

    def test_disconnect_after_timeout_only_once(self):
        self.store.heartbeat(self.heartbeat())
        self.now += 44
        self.assertFalse(self.store.sweep_offline())
        self.now += 1
        self.assertTrue(self.store.sweep_offline())
        self.assertFalse(self.store.sweep_offline())
        self.assertFalse(self.store.state()['device']['online'])
        self.drain()
        self.assertEqual(sum(p['title'] == 'Counter disconnected or powered off' for p in self.sent), 1)

    def test_same_boot_reconnection_then_second_outage(self):
        self.store.heartbeat(self.heartbeat())
        self.now += 46
        self.store.sweep_offline()
        self.store.heartbeat(self.heartbeat(count=2))
        self.store.heartbeat(self.heartbeat(count=2))
        self.now += 46
        self.store.sweep_offline()
        self.drain()
        self.assertEqual(sum(p['title'] == 'Counter reconnected' for p in self.sent), 1)
        self.assertEqual(sum(p['title'] == 'Counter disconnected or powered off' for p in self.sent), 2)

    def test_quick_reset_detected_without_timeout(self):
        self.store.heartbeat(self.heartbeat())
        self.now += 2
        self.store.heartbeat(self.heartbeat('boot2'))
        self.drain()
        self.assertEqual(sum(p['title'] == 'Counter powered on or restarted' for p in self.sent), 1)
        self.assertEqual(sum(p['title'] == 'Counter got connected' for p in self.sent), 2)

    def test_old_outbox_event_does_not_overwrite_current_state(self):
        self.store.heartbeat(self.heartbeat('boot2', 0))
        self.store.record_device_event(self.event(kind='completed', count=3))
        self.assertEqual(self.store.state()['device']['boot_id'], 'boot2')
        self.assertEqual(self.store.state()['device']['count'], 0)
        self.assertTrue(self.store.state()['events'][0]['body'].startswith('Earlier session event:'))

    def test_new_event_updates_count_without_waiting_for_heartbeat(self):
        self.store.heartbeat(self.heartbeat())
        self.store.record_device_event(self.event())
        self.assertEqual(self.store.state()['device']['count'], 1)
        self.store.record_device_event(self.event(4, 'completed', 3))
        self.assertEqual(self.store.state()['device']['mode'], 'alarm')

    def test_old_event_within_same_boot_does_not_rewind_state(self):
        self.store.heartbeat({**self.heartbeat(count=2), 'uptime_ms': 2000})
        self.store.record_device_event(self.event())
        self.assertEqual(self.store.state()['device']['count'], 2)

    def test_uptime_comparison_handles_millis_wrap(self):
        self.store.heartbeat({**self.heartbeat(count=1), 'uptime_ms': 2 ** 32 - 100})
        self.store.record_device_event({**self.event(2, count=2), 'uptime_ms': 100})
        self.assertEqual(self.store.state()['device']['count'], 2)

    def test_state_and_pending_push_survive_backend_restart(self):
        self.store.heartbeat(self.heartbeat())
        self.store.record_device_event(self.event())
        reopened = Store(self.config['database'], lambda: self.now)
        self.assertEqual(reopened.state()['device']['boot_id'], 'boot1')
        self.assertIsNotNone(reopened.next_job())
        self.now += 46
        self.assertTrue(reopened.sweep_offline())

    def test_transient_failure_retries_in_order(self):
        self.store.record_device_event(self.event())
        self.store.record_device_event(self.event(2, count=2))
        def fail(**kw):
            raise TimeoutError()
        deliver_one(self.store, self.config, fail)
        self.assertIsNone(self.store.next_job())
        self.now += 3
        self.drain()
        self.assertEqual([p['body'] for p in self.sent[1:]], ['Total: 1 of 3 whistles.', 'Total: 2 of 3 whistles.'])

    def test_expired_subscription_removed(self):
        self.store.record_device_event(self.event())
        class Gone(Exception):
            response = type('Response', (), {'status_code': 410})()
        def fail(**kw):
            raise Gone()
        deliver_one(self.store, self.config, fail)
        self.assertIsNone(self.store.next_job())
        self.assertEqual(self.store.state()['delivery']['cancelled'], 1)

    def test_stale_push_expires_but_history_remains(self):
        self.store.record_device_event(self.event())
        self.now += 3601
        self.drain()
        self.assertEqual(self.store.state()['delivery']['expired'], 1)
        self.assertEqual(self.store.state()['events'][0]['type'], 'whistle')

    def test_delayed_lock_screen_test(self):
        self.store.test_notification(self.sub['endpoint'])
        self.assertIsNone(self.store.next_job())
        self.now += 10
        self.drain()
        self.assertEqual(self.sent[-1]['title'], 'Whistle Counter test')

    def test_phone_and_device_use_separate_authentication(self):
        self.assertEqual(self.client.get('/api/state').status_code, 401)
        self.assertEqual(self.client.post('/api/device/heartbeat', json=self.heartbeat()).status_code, 401)
        self.assertEqual(self.login().status_code, 200)
        self.assertEqual(self.client.get('/api/state').status_code, 200)
        headers = {'Authorization': 'Bearer ' + self.config['pairing_key']}
        self.assertEqual(self.client.post('/api/device/heartbeat', json=self.heartbeat(), headers=headers).status_code, 401)
        headers['Authorization'] = 'Bearer ' + self.config['device_token']
        self.assertEqual(self.client.post('/api/device/heartbeat', json=self.heartbeat(), headers=headers).status_code, 200)

    def test_pairing_requires_same_origin_and_rejects_wrong_key(self):
        self.assertEqual(self.client.post('/api/login', json={'pairing_key': 'wrong'}).status_code, 403)
        self.assertEqual(self.phone_post('/api/login', {'pairing_key': 'wrong'}).status_code, 401)
        for _ in range(4):
            self.phone_post('/api/login', {'pairing_key': 'wrong'})
        self.assertEqual(self.login().status_code, 429)

    def test_invalid_event_and_completion_rejected(self):
        headers = {'Authorization': 'Bearer ' + self.config['device_token']}
        for value in [[], {}, self.event(kind='unknown'), self.event(kind='completed', count=1),
                      self.event(count=4), self.event(count=True), {**self.event(), 'occurred_at': 'bad'}]:
            response = self.client.post('/api/device/events', json=value, headers=headers)
            self.assertEqual(response.status_code, 400)

    def test_push_endpoint_cannot_target_local_network(self):
        self.login()
        for endpoint in ['http://127.0.0.1:8000', 'https://192.168.1.1',
                         'https://fcm.googleapis.com.evil.example/push', 'https://fcm.googleapis.com:8080/push']:
            value = {**self.sub, 'endpoint': endpoint}
            self.assertEqual(self.phone_post('/api/subscriptions', value).status_code, 400)

    def test_static_assets_and_manifest(self):
        with self.client.get('/') as response:
            self.assertEqual(response.status_code, 200)
        with self.client.get('/manifest.webmanifest') as response:
            manifest = response.json
        self.assertEqual(manifest['display'], 'standalone')
        for icon in manifest['icons']:
            with self.client.get(icon['src']) as response:
                self.assertEqual(response.status_code, 200)
        with self.client.get('/sw.js') as response:
            self.assertEqual(response.headers['Cache-Control'], 'no-store')
        self.assertEqual(self.client.get('/.local/config.json').status_code, 404)

    def test_real_webpush_payload_encryption_without_network(self):
        # Exercises the actual library, VAPID signing, and encryption; curl mode
        # builds the request without sending a notification to an external phone.
        original = os.getcwd()
        try:
            os.chdir(self.tmp.name)
            command = webpush(subscription_info=self.sub, data=json.dumps({'title': 'Whistle detected', 'body': 'Total: 1 of 3.'}),
                              vapid_private_key=self.config['vapid_private_key'],
                              vapid_claims={'sub': self.config['vapid_subject']}, curl=True)
        finally:
            os.chdir(original)
        self.assertIn('curl', command)
        self.assertIn('aes128gcm', command)


if __name__ == '__main__':
    unittest.main()
