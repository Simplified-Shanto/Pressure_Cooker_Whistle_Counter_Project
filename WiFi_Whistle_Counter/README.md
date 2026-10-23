# Wi-Fi Whistle Counter — Generic ESP8266, 4 MB

Your installed hardware is a **Generic ESP8266 Module with 4 MB flash**. Use
[the ESP8266 firmware](firmware/Whistle_counter_esp8266_wifi_multimode/README.md).
The earlier ESP32 folder is an alternative for different hardware.

## 1. Arduino IDE and wiring

| Setting | Value for your device |
| --- | --- |
| Boards Manager package | esp8266 by ESP8266 Community **3.1.2** |
| Board | **Generic ESP8266 Module** |
| CPU Frequency | **160 MHz** |
| Flash Size | **4MB (FS:1MB)**; label may also show OTA size |
| Flash Mode | **DOUT** |
| Upload/reset | Keep the original working adapter settings |
| Serial Monitor | **115200 baud** |

Boards Manager URL: `https://arduino.esp8266.com/stable/package_esp8266com_index.json`

Keep your installed **LiquidCrystal_I2C 2.0.0** (Martin Kubovcik / Frank de
Brabander). PlatformIO pins the original Marco Schwartz implementation at 1.1.4.
The ESP8266 core supplies Wi-Fi, BearSSL, LittleFS, Wire, and EEPROM. A local
`HttpResponse.h` handles incremental HTTP framing; no extra HTTP library is needed.

| Connection | ESP8266 pin |
| --- | --- |
| Microphone AO | A0 |
| LCD SDA / SCL | GPIO4 / GPIO5 |
| Top / middle / bottom button | GPIO12 / GPIO0 / GPIO2 |
| Active buzzer | GPIO14 |

These match the original ESP8266 multimode sketch; LCD address defaults to `0x27`.
The updated firmware also probes `0x3F` and reports the selected I2C responder.
Buttons connect to GND with internal pull-ups. Keep GPIO0/GPIO2 HIGH at normal
boot. A bare ESP8266 ADC accepts 0–1 V; check whether your board adds an input
divider before assuming A0 accepts 3.3 V. Samples are spaced 5 ms apart while
Wi-Fi is active. Recalibrate the threshold in the intended kitchen position.

Home controls: Top/Middle short adjusts target; Bottom short starts; Bottom
hold opens settings; Top hold opens Stopwatch; Middle hold opens Timer.
Bottom hold cancels listening; any button acknowledges a completion alarm.

## 2. Shared backend and notifications

~~~text
Microphone → ESP8266 → Wi-Fi → HTTPS backend → push service → phone
                                  └─ SQLite + always-running watchdog/push worker
~~~

- First successful heartbeat each boot: **Counter got connected**.
- Each whistle: **Whistle detected — Total: N of T whistles**.
- Target reached: a separate **Whistle target reached** notification.
- No heartbeat for 45 seconds: **Counter disconnected or powered off**.
- Same boot returns after an outage: **Counter reconnected**.
- New boot ID: **Counter powered on or restarted**, including reset reason,
  followed by the startup notification. Quick resets do not wait for a timeout.
- Cancelled listening and timer completion also produce notifications.

The always-on server must remain online to report a counter outage. Missing
heartbeats cannot distinguish power loss, Wi-Fi loss, and internet loss.
Worker scheduling and phone push delivery add latency. One backend instance
supports one counter and multiple paired phones.

## 3. Configure HTTPS hosting

For a managed service with a stable HTTPS address and no domain purchase, see
[the prepared Render deployment and migration guide](HOSTING.md). Review the
paid plan before creating it. The temporary phone test can continue independently.

From `WiFi_Whistle_Counter/webapp`, using Python 3.12:

~~~sh
python3 -m venv .venv
.venv/bin/pip install -r requirements.txt
.venv/bin/python setup.py --board esp8266 \
  --origin https://YOUR-REAL-DOMAIN \
  --contact mailto:YOUR-EMAIL@example.com \
  --ca-file /path/to/your-server-root-ca.pem
~~~

Use your actual domain, contact address, and the PEM root CA trust anchor for
the HTTPS certificate chain. Obtain/verify the CA from its issuing authority;
it is not the server leaf certificate. The device validates TLS and obtains
network time with NTP first. It never uses insecure TLS mode.

`setup.py` now defaults to ESP8266. Reruns preserve tokens and VAPID keys.
Continue supplying `--ca-file`; omitting it writes a placeholder CA.
The supplied Wi-Fi settings and server header are in the new folder's local
ignored headers. For this device's integration test, the header now points to
the active temporary HTTPS tunnel and trusts its verified GTS Root R4 CA.
The already installed phone app uses the same backend and signing keys.
Configure a stable production HTTPS address before everyday use; restarting
a Quick Tunnel changes its address and requires updating the device header.

Private files (excluded from Git):

- `webapp/.local/config.json`: device, phone pairing, session secrets and origin.
- `webapp/.local/vapid.pem`: Web Push signing key; back it up with the config.
- `webapp/.local/phone-pairing.txt`: key to enter on the phone.
- `webapp/.env`: Docker domain.
- `firmware/Whistle_counter_esp8266_wifi_multimode/ServerConfig.h`: URL/token/CA.
- `firmware/Whistle_counter_esp8266_wifi_multimode/WiFiSecrets.h`: Wi-Fi credentials.

For a fresh checkout, copy `WiFiSecrets.example.h` to `WiFiSecrets.h` and fill
it in. The generated CA uses PROGMEM to save ESP8266 RAM. Do not publish private
headers or binaries containing credentials. Phone pairing uses an HttpOnly
same-origin cookie; Wi-Fi settings and the device token are not sent to the phone.

### Docker deployment

Use an always-on server with Docker Compose, a domain resolving to it, and
inbound TCP ports 80/443. Securely copy `webapp`, including its private
`.local` and `.env` files, to that host. From that directory:

~~~sh
docker compose up --build -d
docker compose logs -f worker
~~~

Caddy obtains/renews the public certificate; confirm its chain matches the
firmware CA. Keep the app and **one** worker running with persistent SQLite
storage. Do not expose the internal HTTP port to the internet.
`https://YOUR-DOMAIN/healthz` checks the HTTP service.

For an existing HTTPS reverse proxy, supervise both processes:

~~~sh
.venv/bin/gunicorn --bind 127.0.0.1:8000 --workers 1 --threads 4 --keep-alive 75 wsgi:application
.venv/bin/python worker.py
~~~

`COUNTER_CONFIG` can select another private config file; keep `vapid.pem`
beside it. `COUNTER_DATABASE` selects persistent SQLite storage. Back up all
three. The frontend uses plain HTML/CSS/JavaScript, with no Node build.

For computer-only development, use `--origin http://localhost:8000` and
Gunicorn locally. That URL is not a phone/device deployment address. Use the
configured hostname exactly; localhost and 127.0.0.1 are different origins.

## 4. Initialize and upload the ESP8266

1. Select **Generic ESP8266 Module, 160 MHz, 4MB (FS:1MB), DOUT**.
2. Once, upload
   [`InitializeOutbox_esp8266.ino`](tools/InitializeOutbox_esp8266/InitializeOutbox_esp8266.ino).
   Wait for **SUCCESS** in Serial Monitor at 115200. This explicitly erases
   old queued events; do not repeat during normal updates.
3. Keep the same flash/filesystem settings and open
   [`Whistle_counter_esp8266_wifi_multimode.ino`](firmware/Whistle_counter_esp8266_wifi_multimode/Whistle_counter_esp8266_wifi_multimode.ino).
   Keep all accompanying source files and local headers in its folder. Upload.
4. On later updates, use **Erase Flash: Only Sketch** to preserve outbox/settings.
5. Wait for the startup connection notification before starting listening. It
   confirms Wi-Fi, HTTPS, and the backend are reachable.

This device's previous sketch used the 1 MB / 64 KB filesystem layout despite
its physical 4 MB flash. On 23 October 2026, its 12 saved events, boot generation,
and saved threshold were backed up and migrated to **4MB (FS:1MB)**. That is now
the installed layout. Keep this setting for future updates; no initializer is
needed. The migration backups are local, ignored files under
`webapp/.local/flash-backup/`.

PlatformIO: `pio run -e generic_esp8266_4m` in the ESP8266 firmware folder.
Do not upload a replacement filesystem image during ordinary updates.

## 5. Install on the phone

### Android notification test without a domain

For a temporary phone-only test, install
[cloudflared](https://developers.cloudflare.com/tunnel/downloads/) and run
the following from `WiFi_Whistle_Counter/webapp`, after installing the Python
dependencies and generating the private setup files described above:

~~~sh
.venv/bin/python run_phone_test.py
~~~

The helper also accepts a binary at `.local/bin/cloudflared`, or an explicit
`--cloudflared /path/to/cloudflared`. It starts the HTTP app, notification
worker, and a Cloudflare Quick Tunnel, and prints an HTTPS address. Open that
address in **Android Chrome**, install the app using its Install app button
or Chrome's menu, and open the installed app. Pair using the key from
`webapp/.local/phone-pairing.txt`, tap **Enable notifications** and **Allow**,
then tap **Test in 10 seconds** and immediately lock the phone. A confirmation
notification is also sent when notifications are first enabled.

Keep the computer awake, connected to the internet, and the helper running.
Press Ctrl+C in its terminal to stop all three processes. The temporary URL
changes each time; use the new URL and enable notifications there again.
This test uses separate storage under `.local/phone-test/` and preserves the
existing private setup and firmware headers. **Waiting for counter** is
expected: testing real whistle and disconnect alerts requires configuring
the ESP8266 with the backend HTTPS URL and its trusted root CA, then uploading
the main sketch again. Use a stable HTTPS address for everyday operation.

The test notification can arrive while the phone is locked; it still needs
internet access. If it does not appear, check Android notification permission
for Chrome/the installed app, lock-screen notification settings, and Do Not
Disturb. Do not force-stop Chrome or the app during the test.

### Counter status and normal installation

For a **local Wi-Fi test**, upload the updated main sketch, open Serial Monitor
at 115200, and copy its `Open local Wi-Fi status page: http://.../` address.
Connect the phone to Frozen and open that HTTP address. This local page shows
counts and diagnostics without HTTPS hosting. It is separate from the installed
push-notification app below. Details and the LCD-only test are in the
[firmware guide](firmware/Whistle_counter_esp8266_wifi_multimode/README.md).

1. Open the public HTTPS address on the phone.
2. iPhone: Safari > Share > Add to Home Screen, then launch the new icon.
   Android: Chrome > Install app / Add to Home screen.
3. Enter the key in `webapp/.local/phone-pairing.txt`.
4. Tap **Enable notifications**, then Allow.
5. Tap **Test in 10 seconds**, then lock the phone; the worker sends the test.
6. Reboot the counter to verify its startup notification.

Subscribe before rebooting to receive the startup alert. Newly subscribed
phones get a confirmation and current dashboard, not every historical push.
On iPhone/iPad, background push requires iOS/iPadOS 16.4 or later and installation
on the Home Screen. Internet and permission are required; Focus settings,
platform throttling, or a force-stopped browser may delay/suppress delivery.
See [Apple's guidance](https://webkit.org/blog/13878/web-push-for-web-apps-on-ios-and-ipados/).

## Timing and recovery limits

The ESP8266 polls a persistent HTTP/1.1 connection over BearSSL TLS from `loop()`.
Responses are read incrementally; established-connection writes have a short
20 ms timeout. Flash writes and TLS record processing can still interrupt
ADC sampling briefly. Verify real whistle detection under Wi-Fi traffic.

TLS handshakes can take seconds on the ESP8266's single CPU. New handshakes
are deferred while listening. If TLS drops mid-session, local counting/alarm
continue and events save to flash. Reconnection/replay happens after the
target is reached or listening is cancelled; the backend can report an outage
meanwhile. The host must support persistent HTTP connections for continuous
live delivery; the included Caddy and Gunicorn configuration supports this.
Keep Gunicorn's 75-second keep-alive when hosting behind your own TLS proxy.

Saved LittleFS events survive reset; counting sessions do not resume.
Old queued events cannot overwrite newer heartbeat state and are labelled
as earlier session events when appropriate. A power cut during a flash write
can lose that event. Full/corrupt storage is reported on Serial/Home; counting
continues locally. Capacity is finite. An interrupted session is not marked
completed automatically.

SQLite deduplicates retries and retains pending pushes across server restarts.
Push jobs expire after one hour to avoid very stale alerts; history remains.
Each count and completion has its own notification tag. A push accepted by
the service is not proof the phone displayed it.

## Verification

From `webapp`:

~~~sh
.venv/bin/python -m unittest discover -s tests -v
~~~

From `WiFi_Whistle_Counter`:

~~~sh
g++ -std=c++11 -Wall -Wextra -pedantic tests/http_response_test.cpp -o /tmp/whistle-http-test
/tmp/whistle-http-test
~~~

Arduino CLI target: `esp8266:esp8266:generic:xtal=160,eesz=4M1M,FlashMode=dout`.
Compile success does not verify physical ADC calibration or real phone push.
Acceptance checks with a locked, subscribed phone: startup alert, three real
whistles plus a separate final alert, Wi-Fi drop, quick reset, power removal
for over 45 seconds, restoration, and queued-event replay. Keep the worker online.

## ESP32 alternative

Only for actual ESP32 hardware: `firmware/Whistle_counter_esp32_wifi_multimode`,
ESP32 core 2.0.17, `setup.py --board esp32`, and `tools/InitializeOutbox`.
Its pinout, ADC, NVS/FreeRTOS transport, and initializer differ.

## Shared protocol

Device requests use `Authorization: Bearer <device_token>`.

- `POST /api/device/heartbeat`: boot_id, mode, count, target, reset_reason,
  wrapping 32-bit uptime_ms; every ten seconds while connected.
- `POST /api/device/events`: event_id, boot_id, session_id, type, count, target,
  uptime_ms, optional Unix-seconds occurred_at. Types:
  session_started/whistle/completed/cancelled/timer_completed.
  HTTP 200 acknowledges durable insertion or a stored duplicate.
- Phone routes: /api/login, /api/config, /api/state, /api/subscriptions,
  /api/test, /api/logout. Mutations require the configured same Origin.

Web Push: [pywebpush](https://github.com/web-push-libs/pywebpush).
