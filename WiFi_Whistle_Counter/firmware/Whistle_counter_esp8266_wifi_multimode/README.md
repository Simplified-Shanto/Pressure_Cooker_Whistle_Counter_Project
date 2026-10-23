# Generic ESP8266 Wi-Fi whistle counter (installed hardware)

Use this sketch for your **Generic ESP8266 Module with 4 MB flash**. The
separate ESP32 folder targets different hardware and is not required here.

## Arduino IDE

1. Boards Manager URL:
   `https://arduino.esp8266.com/stable/package_esp8266com_index.json`
2. Install **esp8266 by ESP8266 Community 3.1.2** (already installed here).
3. Select **Tools > Board > esp8266 > Generic ESP8266 Module**.
4. Set **CPU Frequency: 160 MHz**, **Flash Size: 4MB (FS:1MB)**, and
   **Flash Mode: DOUT**. The flash menu may also include the OTA size in its label.
   Keep the original working reset/upload settings for your USB adapter.
5. Keep the installed LCD library. This sketch was compiled with
   **LiquidCrystal_I2C 2.0.0** (Martin Kubovcik / Frank de Brabander).
   PlatformIO pins the original Marco Schwartz implementation at **1.1.4**.
6. Open `Whistle_counter_esp8266_wifi_multimode.ino` and click Verify.

ESP8266 core libraries used: `ESP8266WiFi.h`, `WiFiClientSecureBearSSL.h`,
`LittleFS.h`, `Wire.h`, and `EEPROM.h`. The cooperative HTTPS transport includes
its own response-framing helper `HttpResponse.h`; no extra HTTP library is
needed. This sketch does not use ESP32 headers, Preferences, or FreeRTOS.

## Existing wiring

| Connection | ESP8266 pin |
| --- | --- |
| Microphone AO | A0 |
| LCD SDA / SCL | GPIO4 / GPIO5 |
| Top / middle / bottom button | GPIO12 / GPIO0 / GPIO2 |
| Active buzzer | GPIO14 |

These match `MCU_code/Whistle_counter_esp8266_multimode/Config.h`. Buttons connect
to GND; do not hold GPIO0 or GPIO2 LOW while booting normally. LCD initialization
tries the configured address and the common `0x27`/`0x3F` addresses, reporting
the selected responder in Serial Monitor. Use the actual ADC input range of your board: a bare ESP8266 ADC is
0–1 V; some development boards add a divider. Do not assume an A0 header accepts
3.3 V without checking the board. Recalibrate the whistle threshold with Wi-Fi
active; the ADC is sampled at 5 ms intervals to allow for the ESP8266's ADC cache.

## One-time filesystem setup and upload

1. Select the board, CPU, and flash settings above.
2. Once, upload `../../tools/InitializeOutbox_esp8266/InitializeOutbox_esp8266.ino`.
   Wait for **SUCCESS** in Serial Monitor at 115200 baud. This explicitly erases
   old queued events. Do not run it during ordinary firmware updates.
3. Keep the same **4MB (FS:1MB)** setting and upload this counter sketch. Leave
   Erase Flash at **Only Sketch** during updates, retaining the outbox/settings.
4. For push notifications, wait for **Counter got connected** before starting
   a listening session; it confirms the Wi-Fi, HTTPS, and backend path works.

## Test Wi-Fi locally, without the hosted notification app

The ESP8266 now serves a read-only local status page. After uploading the latest
main sketch, open Serial Monitor at **115200 baud**. On Wi-Fi connection it prints:

```text
Wi-Fi connected. IP: 192.168.1.123
Open local Wi-Fi status page: http://192.168.1.123/
```

Use the actual printed IP, not this example. Connect the phone to **Frozen** and
open `http://<printed-IP>/` in its browser. JSON diagnostics are at
`http://<printed-IP>/status`. The router may list the device as `whistle-counter`.
The address is assigned by DHCP and can change after a reconnect.

The page shows local count/target/mode, signal strength, free heap, I2C address,
outbox readiness, and whether the backend has acknowledged a request recently.
It does not expose passwords, device tokens, or signing keys. It still works
when the remote HTTPS origin/CA have not been configured. It does not provide
phone lock-screen push: that remains the separate HTTPS app/backend workflow.

If it does not open, confirm the printed IP and that the phone is on the same
LAN. Guest-network isolation or router client isolation can block access.
Do not forward this diagnostic HTTP port to the internet.

## LCD shows random letters or symbols

The startup probe confirms an I2C responder, not correct LCD text or backpack
pin mapping. The main sketch uses GPIO4 SDA, GPIO5 SCL, 100 kHz I2C, and explicit
LCD initialization. If it still garbles:

1. Verify SDA/SCL and common GND, stable supply, and compatible I2C voltage.
   A 5 V-powered backpack may need a level shifter for ESP8266 signal pins.
2. Fully power-cycle the board and LCD together, rather than just resetting MCU.
3. Upload `../../tools/LcdDiagnostic_esp8266/LcdDiagnostic_esp8266.ino` with the
   same board/flash settings and Erase Flash **Only Sketch**. This test has no
   Wi-Fi or filesystem activity. It scans I2C and should display
   `0123456789ABCDEF` on row 1 and a tick counter on row 2.
4. If this test also garbles, investigate the wiring, supply, and backpack pin
   mapping. If it is readable but the main sketch is not, compare under Wi-Fi
   load and report the detected I2C address and when corruption starts.
5. Upload the main sketch again after the test; no outbox reformat is needed.

The supplied Wi-Fi details and existing device token have been copied into
this folder's ignored local headers. For a fresh checkout, copy
`WiFiSecrets.example.h` to `WiFiSecrets.h`, then generate `ServerConfig.h`:

```sh
# From WiFi_Whistle_Counter/webapp, after installing its Python dependencies:
.venv/bin/python setup.py --board esp8266 \
  --origin https://YOUR-REAL-DOMAIN \
  --contact mailto:YOUR-EMAIL@example.com \
  --ca-file /path/to/your-server-root-ca.pem
```

`setup.py` now defaults to `--board esp8266`; ESP32 remains an explicit option.
This computer's ignored header is configured for the temporary HTTPS integration
test, with a verified GTS Root R4 trust anchor. A permanent deployment needs its
own stable HTTPS address and verified root CA; see the
[hosting guide](../../HOSTING.md). After changing them, upload the same main
sketch again with Erase Flash **Only Sketch**.

The installed device was migrated from its legacy 1 MB / 64 KB filesystem
layout to **4MB (FS:1MB)** on 23 October 2026. Its saved event files, boot record,
and threshold were preserved. Keep the 4 MB / 1 MB setting for all future
uploads; do not rerun the initializer.

### Linux: permission denied opening /dev/ttyUSB0

The compile summary can succeed even when upload fails. If the serial device
is owned by `root:dialout` and your user is not a member, run this in your
computer's terminal:

```sh
sudo usermod -a -G dialout "$USER"
```

Reboot (or fully log out and back in), reopen Arduino IDE, reconnect the board,
select its current port, and retry Upload. Confirm `id -nG` includes `dialout`.
Arduino's Flatpak must also allow device access; this installation already does.
An `esptool.py ... Permission denied` message means that upload failed even if
the progress window says done. Python `elf2bin.py` SyntaxWarning messages are
separate warnings and did not cause this port-access failure.

## ESP8266 timing and reconnection

The persistent HTTPS connection sends individual events and ten-second
heartbeats, polling response bytes from `loop()`. Certificate checks remain
enabled. Writes on an existing connection have a short 20 ms timeout; flash
writes and TLS record processing can still introduce sampling jitter.

ESP8266 TLS handshakes can take seconds on its single CPU. A new handshake is
therefore deferred while listening. If TLS drops mid-session, counting and the
local alarm continue, saved events remain in LittleFS, and the backend's
45-second watchdog can report the outage. Reconnection/replay occurs after the
target is reached or listening is cancelled. This delay is intentional; test
real whistles under Wi-Fi traffic before relying on the detector. The phone may
show the last heartbeat until replay. A power interruption stops the session;
it is not marked completed or automatically resumed.

Your HTTPS host must support persistent HTTP/1.1 connections for live delivery
throughout a session. The provided Caddy/Gunicorn deployment does. Preserve the
75-second Gunicorn keep-alive setting when hosting it directly behind TLS.
Old queued events are marked as earlier session events when appropriate.

The [main setup guide](../../README.md) covers the server, installable phone
app, notification permissions, and lock-screen delivery tests. Those components
are shared by the ESP8266 and ESP32 firmware.
