"""Run a temporary HTTPS Android push test without changing the counter firmware."""
import argparse
import json
import os
import re
import shutil
import signal
import socket
import subprocess
import sys
import time
from pathlib import Path
from urllib.request import urlopen

ROOT = Path(__file__).resolve().parent


def prepare_config(root, origin):
    """Keep test state separate and preserve the existing pairing and signing keys."""
    source = root / '.local'
    config = json.loads((source / 'config.json').read_text())
    folder = source / 'phone-test' / origin.removeprefix('https://')
    folder.mkdir(parents=True, mode=0o700, exist_ok=True)
    config.update(public_origin=origin, vapid_subject=origin)
    path = folder / 'config.json'
    path.write_text(json.dumps(config, indent=2) + '\n')
    path.chmod(0o600)
    key = folder / 'vapid.pem'
    key.write_bytes((source / 'vapid.pem').read_bytes())
    key.chmod(0o600)
    return path, folder / 'counter.sqlite3'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--cloudflared', default=shutil.which('cloudflared') or str(ROOT / '.local/bin/cloudflared'))
    parser.add_argument('--port', type=int, default=8000)
    args = parser.parse_args()
    if not Path(args.cloudflared).is_file():
        parser.error('Install cloudflared from https://developers.cloudflare.com/tunnel/downloads/ '
                     'or supply --cloudflared /path/to/cloudflared')
    if not (ROOT / '.local/config.json').exists() or not (ROOT / '.local/vapid.pem').exists():
        parser.error('Run setup.py first to generate private keys; see the project README.')
    # Fail before creating a tunnel if another service is already using this port.
    with socket.socket() as probe:
        probe.bind(('127.0.0.1', args.port))

    folder = ROOT / '.local/phone-test'
    folder.mkdir(parents=True, mode=0o700, exist_ok=True)
    processes, handles = [], []

    def start(command, log_name, env=None):
        log = (folder / log_name).open('w')
        handles.append(log)
        process = subprocess.Popen(command, cwd=ROOT, env=env, stdout=log, stderr=subprocess.STDOUT)
        processes.append(process)
        return process

    def stop(signum, frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, stop)
    try:
        tunnel = start([args.cloudflared, '--no-autoupdate', 'tunnel', '--protocol', 'http2',
                        '--url', f'http://127.0.0.1:{args.port}'], 'tunnel.log')
        print('Creating temporary HTTPS tunnel...', flush=True)
        deadline = time.monotonic() + 90
        origin = None
        while time.monotonic() < deadline:
            match = re.search(r'https://[a-z0-9-]+\.trycloudflare\.com',
                              (folder / 'tunnel.log').read_text())
            if match:
                origin = match.group()
                break
            if tunnel.poll() is not None:
                raise RuntimeError(f'Tunnel exited; see {folder / "tunnel.log"}')
            time.sleep(0.5)
        if not origin:
            raise RuntimeError(f'Tunnel timed out; see {folder / "tunnel.log"}')

        config_path, database = prepare_config(ROOT, origin)
        env = dict(os.environ, COUNTER_CONFIG=str(config_path), COUNTER_DATABASE=str(database))
        start([sys.executable, '-m', 'gunicorn', '--bind', f'127.0.0.1:{args.port}',
               '--workers', '1', '--threads', '4', '--keep-alive', '75', 'wsgi:application'], 'app.log', env)
        start([sys.executable, str(ROOT / 'worker.py')], 'worker.log', env)
        ready = False
        for attempt in range(40):
            try:
                with urlopen(f'http://127.0.0.1:{args.port}/healthz', timeout=2) as response:
                    ready = json.load(response).get('ok') is True
                if ready:
                    break
            except OSError:
                pass
            if any(p.poll() is not None for p in processes):
                raise RuntimeError(f'A test process exited; inspect logs in {folder}')
            time.sleep(0.25)
        if not ready:
            raise RuntimeError(f'App did not become ready; inspect logs in {folder}')

        (folder / 'latest.json').write_text(json.dumps({
            'origin': origin, 'config': str(config_path), 'database': str(database),
            'supervisor_pid': os.getpid(), 'child_pids': [p.pid for p in processes],
        }, indent=2) + '\n')
        print(f'\nOpen in Android Chrome: {origin}\n'
              f'Pairing key file: {ROOT / ".local/phone-pairing.txt"}\n'
              'Install app, pair phone, enable notifications, then tap Test in 10 seconds and lock the phone.\n'
              'Keep this computer awake and this command running. Press Ctrl+C to stop.\n'
              'This tests phone push only. The counter firmware is unchanged; Waiting for counter is expected.\n'
              'The temporary URL changes each run; install and enable notifications at the new URL.\n', flush=True)
        while True:
            for process in processes:
                if process.poll() is not None:
                    raise RuntimeError(f'A test process exited; inspect logs in {folder}')
            time.sleep(1)
    except KeyboardInterrupt:
        print('\nStopping phone test.', flush=True)
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                process.terminate()
        for process in processes:
            try:
                process.wait(timeout=5)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()
        for handle in handles:
            handle.close()


if __name__ == '__main__':
    main()
