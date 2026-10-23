# Native alternative validation — 23 October 2026

## Tunable whistle timing update

Research and the reasoning for defaults are recorded in `DETECTION_TUNING.md`.
The native firmware now uses 500 ms minimum sound, 1,500 ms continuous ending
quiet, and a 10,000 ms cooldown after a count. All three timings and the existing
threshold can be adjusted on the physical counter and saved on explicit settings
exit. The old eight-byte threshold record is retained; timing is a separate
versioned, CRC-checked EEPROM record. Boot performs no migration writes.

Cooldown additionally requires quiet before rearming so that a burst spanning
cooldown expiry cannot be counted halfway through. Detector reset clears its
sampling window and timing state for a new session.

Verification completed:

- **23 host firmware tests passed**, compiling the actual detector/settings
  sources with warnings as errors and undefined-behavior sanitization. Coverage
  includes short noise, continuous bursts, short internal gaps, all runtime
  timings, first-whistle behavior, cooldown-spanning sounds, session resets,
  clock wraparound, legacy threshold preservation, persistence, corruption,
  invalid ranges, unknown format, and commit failure.
- ESP8266 core 3.1.2 build passed with the existing board settings. RAM:
  32,512 / 80,192 bytes; IRAM including cache: 61,199 / 65,536 bytes; flash code:
  314,472 / 1,048,576 bytes. Existing LCD architecture and Python escape warnings
  remain; no new library dependencies were introduced.
- Uploaded successfully to the idle physical ESP8266 with Only Sketch erase
  policy; flash hash verified. The previous native binary is saved locally as
  `.local/rollback/before-tunable-timing.ino.bin`.
- Live `/status` verified threshold **32** retained, timings **500 / 1500 /
  10000 ms**, Wi-Fi connected, LCD **0x27**, and healthy storage. The existing
  pairing key still authenticated the native API. Snapshots are stored in
  `.local/before-timing-update.json` and `.local/after-timing-update.json`.
- All 57 recorded original web/PWA source files remain unchanged. No Android
  source, APK, pairing key, or filesystem initializer was changed for this update.

Real cooker calibration and visual/button testing of the new settings pages
remain to be performed on the physical setup. Synthetic ADC tests verify timing
behavior, not acoustic discrimination or notification delivery on the phone.

## Firmware reupload after LCD showed a row of blocks

At the user's request, the unchanged native firmware was rebuilt and reuploaded
over `/dev/ttyUSB0` with the same Generic ESP8266, 160 MHz, DOUT, 4MB (FS:1MB),
Only Sketch settings. The counter was idle before upload. The upload completed
with a verified flash hash; no filesystem format or pairing-key regeneration
was performed.

Before upload, `/status` reported `lcd_address: 0`. After upload and hardware
reset, it reported LCD responder **0x27**, Wi-Fi connected, healthy storage,
home mode, and 45,824 bytes free heap. The authenticated native API accepted the
existing pairing key. These checks confirm LCD bus detection; readable text
still requires visual confirmation on the physical display.

## Android 1.1 update: automatic recovery and alarm tone selection

The user confirmed the native app worked when opened but reported no automatic
alerts without opening it. No phone was attached to ADB, so the actual stopped
service/battery/autostart state could not be inspected.

Version 1.1 (version code 2) preserves the same package and signing identity.
It adds persisted recovery jobs, resumption on APK replacement as well as boot,
service health reporting, and a one-time explicit battery-exemption request.
The saved automatic-monitoring preference is retained after a transient startup
failure. Background jobs respect Android's foreground-start restrictions, and
explicit pauses prevent recovery. The existing connected-device foreground
service remains the primary always-running monitor; recovery jobs can be delayed
and do not circumvent force-stop or manufacturer restrictions.

The alarm picker supports the bundled sound, Android alarm tones, and an audio
document. Selections are size-limited, decoded for validation, and copied into
private storage before being selected. Inaccessible saved audio falls back to
the bundled tone. Existing physical acknowledgement/reset/unreachable behavior
is retained, with no phone silence control.

Final verification:

- Android APK assembly: passed.
- All **21 unit tests** passed: existing 12 plus 5 recovery-policy tests and
  4 persistent-tone file tests.
- Android lint: **0 errors, 3 pinned SDK/tool version warnings**.
- Update certificate matches version 1.0; native pairing secret is absent from
  the packaged APK; the verified copy is in the user's Downloads folder.
- No ESP8266 firmware changes or reflash were needed for this update.

Actual-phone locked-screen, reboot, APK-update, manufacturer-autostart, and
selected-tone playback tests still require installing version 1.1 and approving
the background settings on the phone. An initial launch/pairing and permission
approval are required by Android. A force-stopped app cannot promise unattended
notifications until the user opens/interacts with it again.

## Initial version 1.0 build

- ESP8266 firmware compiled successfully with core 3.1.2, Generic ESP8266,
  160 MHz, 4MB (FS:1MB), DOUT, and installed LiquidCrystal_I2C 2.0.0.
  Build report: RAM 32,136 / 80,192 bytes; IRAM including cache reservation
  61,199 / 65,536 bytes; flash code 313,496 / 1,048,576 bytes.
- Android debug APK assembled with AGP 8.13.0, Gradle 8.14.3, JDK 21,
  Android SDK 35 and Build Tools 35.0.0. The Java compatibility level is 17.
- All 12 JUnit tests passed: 8 alarm/connection policy tests and 4 protocol tests.
  They cover physical acknowledgement, reset, outage timeout, alarm restart
  after reconnect, cursor continuity, stale acknowledgement rejection, device
  identity checks, malformed state rejection, initial connection timeout,
  RFC 4231 HMAC compatibility, tampered responses, request binding, and host rules.
- Android lint passed: **0 errors**, with 4 informational version-update warnings
  for the deliberately pinned compile/target SDK, Gradle, and AGP versions.
- APK signing verified with Android `apksigner`; the final local release copy's
  SHA-256 is recorded in `releases/SHA256SUMS`.
- Checked the generated APK archive: the private native pairing key is absent.
- Gradle wrapper JAR verified against the official release checksum, and the
  distribution checksum is pinned in wrapper properties.
- All 57 recorded original web/PWA source files verified unchanged during the
  alternative build. That initial build did not replace the running firmware.

## Native firmware uploaded at the user's request

On 23 October 2026, the idle ESP8266EX (verified 4 MB flash) was flashed with the
native firmware using the same 160 MHz, DOUT, 4MB (FS:1MB) settings and Only
Sketch erase policy. Upload hash verification succeeded. No filesystem
initializer or format was run. A local rollback copy of the previous compiled
web firmware is stored in `.local/rollback/`.

Post-upload checks at `192.168.0.116` confirmed native variant, Wi-Fi connected,
home mode, healthy outbox, no storage error, LCD responder 0x27, and 45,824 bytes
free heap. The challenge/HMAC authenticated API returned a valid native snapshot,
with hardware-reset reason and healthy storage. An incorrect pairing signature
was rejected with HTTP 401. The original web backend may now report a counter
outage because the native firmware communicates directly with the phone.

No Android device was attached to ADB for this build. The app has not yet been
tested on the user's phone. Build/unit/lint and API results do not prove
locked-screen scheduling, audible volume, or microphone timing on the physical
setup. Follow the acceptance test in README.md after installing and pairing the
APK; the native firmware is now installed and ready for that test.
