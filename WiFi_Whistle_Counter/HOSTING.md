# Permanent notification hosting

The phone runs the installable app and receives Web Push. The backend needs an
always-running host so it can detect an ESP8266 outage while the phone is asleep.
Running it on Android would require a separate continuously running server and
would depend on the phone's process, battery, and network state. Use the phone as
the notification client and host the backend separately.

## Recommended: one paid Render web service

Use one 0.5 CPU / 512 MB service (formerly named Starter), in Singapore, with a
1 GB persistent disk. At the prices checked on 23 October 2026, the base cost is
US$7/month plus US$0.25/month for the disk, before applicable tax or extra usage.
Render supplies a stable `https://YOUR-SERVICE.onrender.com` address and TLS, so
you do not need to buy a domain. Review the price shown before creating it.

Sources: [pricing](https://render.com/pricing),
[first deployment and supplied URL](https://render.com/docs/your-first-deploy),
[persistent disks](https://render.com/docs/disks).

Avoid the free service for this project: it sleeps after idle periods and cannot
attach a persistent disk. When the counter loses power, its traffic stops; the
watchdog must stay running to send that alert. Ephemeral storage also loses the
SQLite event history and phone subscriptions after a restart/redeployment.
[Free-service limitations](https://render.com/docs/free).

## Deployment files prepared in this repository

- `render.yaml`: one paid web service and persistent disk; no secret values.
- `webapp/serve.py`: runs Gunicorn and one notification/watchdog worker together.
  If either exits, it stops the other and exits so Render can restart the service.
- `webapp/.python-version`: Python 3.12, matching the local test environment.

Both processes use the same SQLite database on the disk. Keep **one instance**.
Do not create a separate Render worker: a Render disk cannot be shared between
services. Brief deployment/maintenance interruptions are possible; the device
outbox and database preserve pending events. Do not deploy during a cooking test.

## Create the service

1. Put the reviewed source changes on a Git branch that Render can access.
   Private configuration, signing keys, Wi-Fi headers, firmware binaries, and
   runtime databases must stay excluded from Git.
2. Sign in to [Render](https://dashboard.render.com/). Choose **New → Blueprint**,
   connect the repository and branch, and set the Blueprint path to
   `WiFi_Whistle_Counter/render.yaml`.
3. Review the single paid service and 1 GB disk before approving creation. The
   Blueprint does not contain the private files; the service needs them before
   it can start successfully. Its first deploy may fail until step 4 is done.
4. In the service's **Environment → Secret Files**, add these two files using
   their existing contents from this computer:

   | Render secret filename | Local source |
   | --- | --- |
   | `config.json` | `webapp/.local/config.json` |
   | `vapid.pem` | `webapp/.local/vapid.pem` |

   Keep the existing signing key and identity values. Render mounts these as
   `/etc/secrets/config.json` and `/etc/secrets/vapid.pem`. Never paste their
   contents into Git, chat, or public logs. See
   [Render secret files](https://render.com/docs/configure-environment-variables#secret-files).
5. Save and deploy. `app.py` uses Render's `RENDER_EXTERNAL_URL` as the HTTPS app
   origin and VAPID contact URI, overriding the old test origin in the secret
   JSON. The device token and phone pairing/signing keys remain unchanged.
6. Verify `https://YOUR-SERVICE.onrender.com/healthz` returns `{"ok":true}` and
   logs say the web app and notification worker started. The database is at
   `/var/data/counter.sqlite3`, on the persistent disk.

For a custom domain, set `COUNTER_PUBLIC_ORIGIN=https://YOUR-DOMAIN`; it takes
priority over Render's default URL. `COUNTER_VAPID_SUBJECT` optionally supplies
your real `mailto:` or HTTPS contact URI. Keep these origins free of paths.

## Move the phone and ESP8266 to the permanent address

The temporary app and permanent app have different origins. Open the new HTTPS
address in Android Chrome, install it, pair with the same local phone-pairing key,
and enable notifications again. Run the ten-second locked-phone test first.
Existing test subscriptions/history stay in the temporary test database; the
permanent service begins with an empty database.

Then configure the **same ESP8266 sketch** with the new server origin and verified
root CA. Check the deployed server's actual certificate chain and obtain its root
certificate from the issuing CA; do not assume it matches the tunnel certificate.
From `webapp`:

```sh
.venv/bin/python setup.py --board esp8266 \
  --origin https://YOUR-SERVICE.onrender.com \
  --contact https://YOUR-SERVICE.onrender.com \
  --ca-file /path/to/verified-root-ca.pem
```

Upload `Whistle_counter_esp8266_wifi_multimode` with Generic ESP8266, 160 MHz,
4 MB flash, the **existing filesystem layout**, DOUT, and Erase Flash **Only
Sketch**. Do not rerun the outbox initializer during this change. Once this
deployment works, the computer and temporary tunnel can be shut down.

## End-to-end checks

1. With notifications enabled, boot the counter: receive **Counter got connected**.
2. Select a target of two and start listening. Confirm the dashboard count follows
   the LCD, one notification reports count 1, another reports count 2, and a
   separate **Whistle target reached** notification arrives.
3. Reset the counter: receive **Counter powered on or restarted** and its startup
   notification. An interrupted session must not be marked completed.
4. Turn the counter off: after roughly 45 seconds plus delivery latency, receive
   **Counter disconnected or powered off**. Keep the phone online for this test.
5. Restore power: receive a startup/reset notification. Test Wi-Fi loss separately
   by disconnecting the counter while leaving the phone online, for example on
   mobile data. Missing heartbeats cannot identify the exact cause of an outage.
6. If TLS drops while listening, finish or cancel the session to allow a new TLS
   handshake and replay of saved events. ESP8266 intentionally avoids blocking
   microphone sampling with a reconnect handshake during listening.

Back up the private configuration/signing key and SQLite database. Use SQLite's
online backup API while the service is running, rather than copying only the
database file while its WAL may contain recent changes.
