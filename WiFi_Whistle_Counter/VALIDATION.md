# Integration checks — 23 October 2026

Hardware: ESP8266EX, physically verified 4 MB flash, Generic ESP8266 Module,
core 3.1.2, 160 MHz, DOUT, 4MB (FS:1MB), installed LiquidCrystal_I2C 2.0.0.

| Check | Result |
| --- | --- |
| Arduino compile and serial upload | Passed; upload data hash verified |
| Backend unit tests | 25 passed |
| Render deployment manifest | Passed Render's published JSON schema |
| Hosting launcher | HTTP became healthy; killing its worker stopped the service and cleaned up both child processes |
| Firmware HTTPS | Actual heartbeat acknowledged using verified GTS Root R4 trust anchor; no insecure TLS mode |
| Existing phone app | Original HTTPS test address, pairing, and signing keys retained |
| Background phone push | User confirmed the Android locked-phone test worked |
| Startup | Connected alert accepted by push service |
| Missing heartbeat | Real idle counter held in bootloader; offline watchdog alert accepted by push service after timeout |
| Recovery/reset | Counter returned to firmware; fresh heartbeat, restart alert, and connected alert accepted by push service |
| Two-whistle session | User confirmed count-1, count-2, and separate completion alerts all arrived |
| Local functionality | User confirmed display and local HTTP page work; read-only status reports Wi-Fi/backend reachable and storage healthy |

The old firmware had used a 1 MB / 64 KB layout even though the physical chip
is 4 MB. The original filesystem and EEPROM, plus the destination region, were
backed up under the ignored `webapp/.local/flash-backup/` directory. All 12
queued event files and the boot-generation record were extracted and verified
byte-for-byte in a new filesystem image. The saved threshold (32) was preserved
in the new EEPROM location. Flash writes were hash-verified, storage then mounted
successfully, and all 12 preserved events were received by the backend.

The device is currently configured for the temporary HTTPS tunnel. That path
depends on this computer remaining online. Permanent hosting files are prepared
in `render.yaml` and `HOSTING.md`; a paid hosted service has **not** been created.
After deployment, install/subscribe at its new origin and configure the firmware
with that stable URL and its verified certificate root.

Push-service acceptance is distinct from display on a phone. The phone receipt
checks above are based on the user's confirmations. The outage test paused the
counter's CPU without changing flash; physically unplugging power and a router
outage are additional optional checks. ESP8266 TLS reconnects are deferred during
listening to protect sampling; queued alerts replay after completion/cancellation.
