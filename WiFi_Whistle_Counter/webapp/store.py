"""Durable device state, event deduplication, and per-phone push outbox."""
import json
import sqlite3
import time
from contextlib import contextmanager
from pathlib import Path

OFFLINE_SECONDS = 45
PUSH_LIFETIME = 3600


class Store:
    def __init__(self, path, clock=time.time):
        self.path, self.clock = str(path), clock
        Path(path).parent.mkdir(parents=True, exist_ok=True)
        with self.connect() as db:
            db.executescript("""
                PRAGMA journal_mode=WAL;
                CREATE TABLE IF NOT EXISTS device (
                  id INTEGER PRIMARY KEY CHECK(id=1), boot_id TEXT NOT NULL,
                  last_seen REAL NOT NULL, online INTEGER NOT NULL,
                  snapshot TEXT NOT NULL, outage INTEGER NOT NULL DEFAULT 0);
                CREATE TABLE IF NOT EXISTS subscriptions (
                  endpoint TEXT PRIMARY KEY, subscription TEXT NOT NULL);
                CREATE TABLE IF NOT EXISTS events (
                  id INTEGER PRIMARY KEY AUTOINCREMENT, event_id TEXT UNIQUE NOT NULL,
                  type TEXT NOT NULL, payload TEXT NOT NULL, received_at REAL NOT NULL);
                CREATE TABLE IF NOT EXISTS jobs (
                  id INTEGER PRIMARY KEY AUTOINCREMENT,
                  event_id INTEGER NOT NULL REFERENCES events(id),
                  endpoint TEXT NOT NULL, status TEXT NOT NULL DEFAULT 'pending',
                  attempts INTEGER NOT NULL DEFAULT 0, next_attempt REAL NOT NULL DEFAULT 0,
                  last_error TEXT, UNIQUE(event_id, endpoint));
                CREATE INDEX IF NOT EXISTS jobs_pending ON jobs(status, id);
            """)

    @contextmanager
    def connect(self):
        db = sqlite3.connect(self.path, timeout=10)
        db.row_factory = sqlite3.Row
        db.execute("PRAGMA foreign_keys=ON")
        try:
            # Serialize read-modify-write operations across web and worker processes.
            db.execute("BEGIN IMMEDIATE")
            yield db
            db.commit()
        except BaseException:
            db.rollback()
            raise
        finally:
            db.close()

    def _record(self, db, event_id, kind, payload, notify=True, endpoint=None):
        result = db.execute(
            "INSERT OR IGNORE INTO events(event_id,type,payload,received_at) VALUES(?,?,?,?)",
            (event_id, kind, json.dumps(payload), self.clock()))
        if result.rowcount == 0:
            return False
        if notify:
            if endpoint:
                destinations = [endpoint]
            else:
                destinations = [r[0] for r in db.execute("SELECT endpoint FROM subscriptions")]
            db.executemany("INSERT INTO jobs(event_id,endpoint) VALUES(?,?)",
                           [(result.lastrowid, e) for e in destinations])
        return True

    def _offline(self, db, row, now):
        if row and row['online'] and now - row['last_seen'] >= OFFLINE_SECONDS:
            outage = row['outage'] + 1
            db.execute("UPDATE device SET online=0,outage=? WHERE id=1", (outage,))
            self._record(db, f"offline:{row['boot_id']}:{outage}", 'offline', {
                'title': 'Counter disconnected or powered off',
                'body': 'No heartbeat for 45 seconds. Wi-Fi, power, or internet may be unavailable.',
                'last_seen': row['last_seen'], 'snapshot': json.loads(row['snapshot'])})
            return True
        return False

    def heartbeat(self, snapshot):
        now = self.clock()
        with self.connect() as db:
            previous = db.execute("SELECT * FROM device WHERE id=1").fetchone()
            stale = self._offline(db, previous, now)
            boot = snapshot['boot_id']
            if not previous or previous['boot_id'] != boot:
                if previous:
                    self._record(db, f'restarted:{boot}', 'restarted', {
                        'title': 'Counter powered on or restarted',
                        'body': f"New boot detected. Reason: {snapshot['reset_reason']}. Previous session is not resumed.",
                        'boot_id': boot})
                self._record(db, f'connected:{boot}', 'connected', {
                    'title': 'Counter got connected',
                    'body': 'The whistle counter has connected to Wi-Fi and the notification server.',
                    'boot_id': boot})
            elif not previous['online'] or stale:
                outage = previous['outage'] + (1 if stale else 0)
                self._record(db, f'reconnected:{boot}:{outage}', 'reconnected', {
                    'title': 'Counter reconnected',
                    'body': f"Monitoring restored. Total: {snapshot['count']} of {snapshot['target']} whistles.",
                    'boot_id': boot})
            db.execute("""INSERT INTO device(id,boot_id,last_seen,online,snapshot) VALUES(1,?,?,1,?)
                ON CONFLICT(id) DO UPDATE SET boot_id=excluded.boot_id,last_seen=excluded.last_seen,
                  online=1,snapshot=excluded.snapshot""", (boot, now, json.dumps(snapshot)))

    def sweep_offline(self):
        with self.connect() as db:
            return self._offline(db, db.execute("SELECT * FROM device WHERE id=1").fetchone(), self.clock())

    def record_device_event(self, event):
        kind, count, target = event['type'], event['count'], event['target']
        messages = {
            'session_started': ('Listening started', f'Target: {target} whistles.'),
            'whistle': ('Whistle detected', f'Total: {count} of {target} whistles.'),
            'completed': ('Whistle target reached', f'Completed: {count} of {target} whistles.'),
            'cancelled': ('Listening cancelled', f'Session stopped at {count} of {target} whistles.'),
            'timer_completed': ('Timer finished', 'The counter timer has reached zero.'),
        }
        title, body = messages[kind]
        with self.connect() as db:
            current = db.execute("SELECT boot_id,snapshot FROM device WHERE id=1").fetchone()
            occurred = event.get('occurred_at')
            if (current and current['boot_id'] != event['boot_id']) or (occurred and self.clock() - occurred > 60):
                body = 'Earlier session event: ' + body
            inserted = self._record(db, event['event_id'], kind, {**event, 'title': title, 'body': body})
            if inserted and current and current['boot_id'] == event['boot_id']:
                snapshot = json.loads(current['snapshot'])
                # Compare wrapping 32-bit ESP32 uptime. Replayed older events
                # never rewind live state, even within the same boot/session.
                delta = (event['uptime_ms'] - snapshot['uptime_ms']) % (2 ** 32)
                if delta < 2 ** 31:
                    modes = {'session_started': 'listening', 'whistle': 'listening',
                             'completed': 'alarm', 'cancelled': 'home', 'timer_completed': 'timer'}
                    snapshot.update(mode=modes[kind], count=0 if kind == 'cancelled' else count,
                                    target=target, uptime_ms=event['uptime_ms'])
                    db.execute("UPDATE device SET snapshot=? WHERE id=1", (json.dumps(snapshot),))
            return inserted

    def subscribe(self, subscription):
        endpoint = subscription['endpoint']
        with self.connect() as db:
            existed = db.execute("SELECT 1 FROM subscriptions WHERE endpoint=?", (endpoint,)).fetchone()
            db.execute("INSERT OR REPLACE INTO subscriptions VALUES(?,?)", (endpoint, json.dumps(subscription)))
            if not existed:
                self._record(db, f'subscribed:{time.time_ns()}', 'subscribed', {
                    'title': 'Notifications enabled',
                    'body': 'This phone will receive connection, whistle, completion, and disconnect alerts.'
                }, endpoint=endpoint)

    def unsubscribe(self, endpoint):
        with self.connect() as db:
            db.execute("DELETE FROM subscriptions WHERE endpoint=?", (endpoint,))
            db.execute("UPDATE jobs SET status='cancelled' WHERE endpoint=? AND status='pending'", (endpoint,))

    def test_notification(self, endpoint):
        with self.connect() as db:
            if not db.execute("SELECT 1 FROM subscriptions WHERE endpoint=?", (endpoint,)).fetchone():
                return False
            self._record(db, f'test:{time.time_ns()}', 'test', {
                'title': 'Whistle Counter test',
                'body': 'Background push is set up. Lock your phone to check delivery.'}, endpoint=endpoint)
            db.execute("UPDATE jobs SET next_attempt=? WHERE id=(SELECT MAX(id) FROM jobs)", (self.clock() + 10,))
            return True

    def state(self):
        with self.connect() as db:
            device = db.execute("SELECT * FROM device WHERE id=1").fetchone()
            events = db.execute("SELECT * FROM events ORDER BY id DESC LIMIT 60").fetchall()
            jobs = dict(db.execute("SELECT status,COUNT(*) FROM jobs GROUP BY status").fetchall())
            return {
                'device': None if not device else {
                    **json.loads(device['snapshot']), 'last_seen': device['last_seen'],
                    'online': bool(device['online']) and self.clock() - device['last_seen'] < OFFLINE_SECONDS},
                'events': [{**json.loads(e['payload']), 'event_id': e['event_id'],
                            'type': e['type'], 'received_at': e['received_at']} for e in events],
                'delivery': jobs, 'offline_after_seconds': OFFLINE_SECONDS}

    def next_job(self):
        with self.connect() as db:
            # Preserve each phone's event order even when its earlier push is retrying.
            row = db.execute("""SELECT j.*,e.payload,e.event_id AS external_id,e.received_at,s.subscription
              FROM jobs j JOIN events e ON e.id=j.event_id
              JOIN subscriptions s ON s.endpoint=j.endpoint
              WHERE j.status='pending' AND j.next_attempt<=? AND NOT EXISTS (
                SELECT 1 FROM jobs older WHERE older.endpoint=j.endpoint AND older.status='pending' AND older.id<j.id)
              ORDER BY j.id LIMIT 1""", (self.clock(),)).fetchone()
            return dict(row) if row else None

    def finish_job(self, job, status, error=None):
        with self.connect() as db:
            attempts = job['attempts'] + 1
            db.execute("UPDATE jobs SET status=?,attempts=?,next_attempt=?,last_error=? WHERE id=?",
                       (status, attempts, self.clock() + min(300, 2 ** min(attempts, 8)), error, job['id']))
