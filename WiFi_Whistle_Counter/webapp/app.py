import hmac
import json
import os
import re
import time
from collections import defaultdict, deque
from datetime import timedelta
from functools import wraps
from pathlib import Path
from threading import Lock
from urllib.parse import urlparse

from flask import Flask, abort, jsonify, request, send_from_directory, session

from store import Store

ROOT = Path(__file__).resolve().parent
IDENTIFIER = re.compile(r'^[a-zA-Z0-9:_-]{1,100}$')
EVENT_TYPES = {'session_started', 'whistle', 'completed', 'cancelled', 'timer_completed'}
MODES = {'home', 'listening', 'alarm', 'settings', 'stopwatch', 'timer'}


def load_config():
    path = Path(os.environ.get('COUNTER_CONFIG', ROOT / '.local/config.json'))
    config = json.loads(path.read_text())
    config['database'] = os.environ.get('COUNTER_DATABASE', str(ROOT / 'data/counter.sqlite3'))
    config['vapid_private_key'] = str(path.parent / 'vapid.pem')
    origin = os.environ.get('COUNTER_PUBLIC_ORIGIN') or os.environ.get('RENDER_EXTERNAL_URL')
    if origin:
        parsed = urlparse(origin)
        if parsed.scheme != 'https' or not parsed.hostname or parsed.path not in ('', '/') or \
                parsed.username or parsed.password or parsed.query or parsed.fragment:
            raise ValueError('Hosted COUNTER_PUBLIC_ORIGIN must be an HTTPS origin without a path')
        config['public_origin'] = origin.rstrip('/')
        config['vapid_subject'] = os.environ.get('COUNTER_VAPID_SUBJECT') or config['public_origin']
    return config


def validate_snapshot(value):
    if not isinstance(value, dict):
        abort(400, 'Expected a JSON object')
    if not IDENTIFIER.fullmatch(str(value.get('boot_id', ''))):
        abort(400, 'Invalid boot ID')
    for name in ['count', 'target']:
        if type(value.get(name)) is not int:
            abort(400, f'Invalid {name}')
    if not 1 <= value['target'] <= 999 or not 0 <= value['count'] <= value['target']:
        abort(400, 'Count outside target range')
    if type(value.get('uptime_ms')) is not int or not 0 <= value['uptime_ms'] < 2 ** 32:
        abort(400, 'Invalid uptime')


def validate_subscription(value):
    if not isinstance(value, dict):
        abort(400, 'Expected subscription object')
    parsed = urlparse(str(value.get('endpoint', '')))
    host = parsed.hostname or ''
    try:
        port = parsed.port
    except ValueError:
        abort(400, 'Invalid push endpoint port')
    # A subscription must not turn the push worker into a general HTTP client.
    allowed = host == 'fcm.googleapis.com' or host == 'updates.push.services.mozilla.com' or host.endswith('.push.apple.com')
    if not allowed or parsed.scheme != 'https' or port not in (None, 443) or parsed.username or parsed.password:
        abort(400, 'Unsupported push service; use Chrome, Firefox, or Safari')
    keys = value.get('keys')
    if not isinstance(keys, dict) or any(not isinstance(keys.get(k), str) or
            not re.fullmatch(r'[A-Za-z0-9_-]{16,200}={0,2}', keys[k]) for k in ['auth', 'p256dh']):
        abort(400, 'Invalid subscription keys')
    return value


def create_app(config=None):
    config = config or load_config()
    app = Flask(__name__, static_folder=None)
    app.secret_key = config['session_secret']
    app.config.update(MAX_CONTENT_LENGTH=16384, SESSION_COOKIE_HTTPONLY=True,
                      SESSION_COOKIE_SECURE=config['public_origin'].startswith('https://'),
                      SESSION_COOKIE_SAMESITE='Strict', PERMANENT_SESSION_LIFETIME=timedelta(days=180))
    store = Store(config['database'])
    app.extensions['counter_store'] = store
    attempts, limit_lock = defaultdict(deque), Lock()

    def phone(fn):
        @wraps(fn)
        def wrapped(*args, **kwargs):
            if not session.get('paired'):
                abort(401, 'Pair this phone first')
            return fn(*args, **kwargs)
        return wrapped

    def device(fn):
        @wraps(fn)
        def wrapped(*args, **kwargs):
            token = request.headers.get('Authorization', '')
            if not hmac.compare_digest(token, 'Bearer ' + config['device_token']):
                abort(401, 'Invalid device token')
            return fn(*args, **kwargs)
        return wrapped

    @app.before_request
    def origin_check():
        if request.method in {'POST', 'DELETE'} and not request.path.startswith('/api/device/'):
            if request.headers.get('Origin') != config['public_origin']:
                abort(403, 'Use the configured app origin')

    @app.after_request
    def headers(response):
        response.headers['X-Content-Type-Options'] = 'nosniff'
        response.headers['Referrer-Policy'] = 'no-referrer'
        response.headers['Content-Security-Policy'] = (
            "default-src 'self'; script-src 'self'; style-src 'self'; img-src 'self'; "
            "connect-src 'self'; worker-src 'self'; manifest-src 'self'; frame-ancestors 'none'; base-uri 'none'")
        if request.path.startswith('/api/') or request.path == '/sw.js':
            response.headers['Cache-Control'] = 'no-store'
        return response

    @app.errorhandler(400)
    @app.errorhandler(401)
    @app.errorhandler(403)
    @app.errorhandler(404)
    @app.errorhandler(413)
    @app.errorhandler(429)
    def api_error(error):
        return jsonify(error=error.description), error.code

    @app.get('/healthz')
    def health():
        return jsonify(ok=True)

    @app.post('/api/login')
    def login():
        # Deliberately use the direct peer, not an untrusted forwarded header.
        address, now = request.remote_addr, time.monotonic()
        with limit_lock:
            bucket = attempts[address]
            while bucket and bucket[0] < now - 60:
                bucket.popleft()
            if len(bucket) >= 5:
                abort(429, 'Too many attempts. Wait one minute.')
            bucket.append(now)
        body = request.get_json()
        key = body.get('pairing_key') if isinstance(body, dict) else None
        if not isinstance(key, str) or not hmac.compare_digest(key, config['pairing_key']):
            abort(401, 'Incorrect pairing key')
        session.clear()
        session['paired'] = True
        session.permanent = True
        return jsonify(ok=True)

    @app.post('/api/logout')
    @phone
    def logout():
        session.clear()
        return jsonify(ok=True)

    @app.get('/api/config')
    @phone
    def phone_config():
        return jsonify(vapid_public_key=config['vapid_public_key'])

    @app.get('/api/state')
    @phone
    def state():
        return jsonify(store.state())

    @app.post('/api/subscriptions')
    @phone
    def subscribe():
        store.subscribe(validate_subscription(request.get_json()))
        return jsonify(ok=True)

    @app.delete('/api/subscriptions')
    @phone
    def unsubscribe():
        value = validate_subscription(request.get_json())
        store.unsubscribe(value['endpoint'])
        return jsonify(ok=True)

    @app.post('/api/test')
    @phone
    def test():
        value = validate_subscription(request.get_json())
        if not store.test_notification(value['endpoint']):
            abort(400, 'Enable notifications first')
        return jsonify(ok=True)

    @app.post('/api/device/heartbeat')
    @device
    def heartbeat():
        value = request.get_json()
        validate_snapshot(value)
        if value.get('mode') not in MODES or not isinstance(value.get('reset_reason'), str) or len(value['reset_reason']) > 60:
            abort(400, 'Invalid mode or reset reason')
        store.heartbeat(value)
        return jsonify(ok=True)

    @app.post('/api/device/events')
    @device
    def event():
        value = request.get_json()
        validate_snapshot(value)
        if value.get('type') not in EVENT_TYPES or not IDENTIFIER.fullmatch(str(value.get('event_id', ''))):
            abort(400, 'Invalid event')
        if not IDENTIFIER.fullmatch(str(value.get('session_id', ''))):
            abort(400, 'Invalid session ID')
        if value['type'] == 'completed' and value['count'] != value['target']:
            abort(400, 'Completion requires reaching target')
        occurred = value.get('occurred_at')
        if occurred is not None and (type(occurred) is not int or occurred < 1700000000):
            abort(400, 'Invalid event timestamp')
        return jsonify(ok=True, new=store.record_device_event(value))

    @app.get('/')
    def index():
        return send_from_directory(ROOT / 'public', 'index.html')

    @app.get('/<path:name>')
    def assets(name):
        return send_from_directory(ROOT / 'public', name)

    return app
