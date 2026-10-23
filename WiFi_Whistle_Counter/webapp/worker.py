"""Run one instance alongside the HTTP server, including while phones are asleep."""
import json
import logging
import time

from pywebpush import webpush

from app import load_config
from store import PUSH_LIFETIME, Store

log = logging.getLogger('counter.worker')


def deliver_one(store, config, sender=webpush):
    job = store.next_job()
    if not job:
        return False
    age = store.clock() - job['received_at']
    if age >= PUSH_LIFETIME:
        store.finish_job(job, 'expired', 'Push expired after one hour; event remains in history')
        return True
    payload = json.loads(job['payload'])
    payload.update(event_id=job['external_id'], received_at=job['received_at'])
    if age > 60:
        payload['body'] = 'Delayed alert: ' + payload['body']
    try:
        # No Topic header: each count and the final completion remain distinct.
        sender(subscription_info=json.loads(job['subscription']), data=json.dumps(payload),
               vapid_private_key=config['vapid_private_key'],
               vapid_claims={'sub': config['vapid_subject']}, ttl=max(1, int(PUSH_LIFETIME - age)),
               headers={'Urgency': 'high'}, timeout=10)
    except Exception as error:
        response = getattr(error, 'response', None)
        code = getattr(response, 'status_code', None)
        # Do not log subscription endpoints or keys contained in exception text.
        if code in (404, 410):
            store.unsubscribe(job['endpoint'])
        elif code and 400 <= code < 500 and code not in (408, 429):
            store.finish_job(job, 'failed', f'Push service rejected request: HTTP {code}')
            log.error('Push job %s rejected with HTTP %s', job['id'], code)
        else:
            store.finish_job(job, 'pending', f'Transient push failure: {type(error).__name__}')
            log.warning('Push job %s will retry (%s)', job['id'], type(error).__name__)
    else:
        store.finish_job(job, 'sent')
    return True


def main():
    logging.basicConfig(level=logging.INFO)
    config = load_config()
    store = Store(config['database'])
    while True:
        try:
            store.sweep_offline()
            if not deliver_one(store, config):
                time.sleep(1)
        except Exception:
            log.exception('Worker iteration failed; retrying')
            time.sleep(2)


if __name__ == '__main__':
    main()
