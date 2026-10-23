"""Supervise the web app and one push/watchdog worker on a single host."""
import os
import signal
import subprocess
import sys
import time
from pathlib import Path

from app import ROOT, load_config


def main():
    config = load_config()
    if not Path(config['vapid_private_key']).is_file():
        raise RuntimeError('Missing vapid.pem beside COUNTER_CONFIG; upload both private secret files.')
    port = int(os.environ.get('PORT', '8000'))
    if not 1 <= port <= 65535:
        raise ValueError('PORT must be between 1 and 65535')
    processes = []

    def stop(signum, frame):
        raise KeyboardInterrupt

    signal.signal(signal.SIGTERM, stop)
    try:
        for command in [
                [sys.executable, '-m', 'gunicorn', '--bind', f'0.0.0.0:{port}',
                 '--workers', '1', '--threads', '4', '--keep-alive', '75', 'wsgi:application'],
                [sys.executable, str(ROOT / 'worker.py')]]:
            processes.append(subprocess.Popen(command, cwd=ROOT))
        print('Whistle Counter web app and notification worker started.', flush=True)
        while True:
            for process in processes:
                if process.poll() is not None:
                    print('A required process exited; stopping the service so the host can restart it.',
                          file=sys.stderr, flush=True)
                    return 1
            time.sleep(0.5)
    except KeyboardInterrupt:
        return 0
    finally:
        for process in reversed(processes):
            if process.poll() is None:
                process.terminate()
        for process in processes:
            try:
                process.wait(timeout=10)
            except subprocess.TimeoutExpired:
                process.kill()
                process.wait()


if __name__ == '__main__':
    sys.exit(main())
