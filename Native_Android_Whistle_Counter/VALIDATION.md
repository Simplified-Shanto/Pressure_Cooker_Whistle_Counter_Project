# Native alternative validation — 23 October 2026

## Android 1.4: acknowledge an intentional or resolved outage

The user requested an acknowledgement for a critical connection-loss alert after
power-off during unfinished counting. Version 1.4 (version code 5) adds an
**Acknowledge** notification action and a Home-screen button with **Intentional
power-off** / **Checked and resolved** choices.

Each offline transition has a persisted unique outage identity. Acknowledgement
stores only that identity, removes its critical notice, and updates the dashboard
and foreground indicator to acknowledged/offline. Monitoring, pairing, count,
cursor, and notification-category preferences are retained. Reconnection clears
the current outage; a subsequent timeout gets a different identity. Old pending
actions cannot acknowledge the new outage. Persisted acknowledgements are
restored across service/app restarts rather than recreating the critical warning.

The notification uses an immutable PendingIntent addressed to a non-exported
BroadcastReceiver, with a distinct data URI per outage. It works without opening
an Activity or starting a new service. The action validates offline unfinished
counting and refuses a live/active completion alarm; it sends no counter command.
The token is saved before the action is posted. Log writes from the service and
receiver are serialized, and repeated acknowledgement does not append repeatedly.
The acknowledgement preserves an explicitly paused monitoring preference as well.

Verification completed:

- APK assembly and **53 unit tests passed**: previous 44 plus 9 outage tests.
  New coverage includes current-outage acknowledgement, preserving count/cursor,
  repeated calls, long offline periods, process-state restoration, old actions
  after recovery/new outage, active completion-alarm protection, invalid/idle
  state rejection, and stable migration of a pre-1.4 outage identity.
- Android lint: **0 errors, 4 existing pinned SDK/tool/dependency warnings**.
- APK version **1.4 / code 5**, signature verified against 1.3; unpacked APK
  contains neither the native pairing key nor Wi-Fi password. Previous APK is
  retained privately as `.local/rollback/Whistle-Counter-Local-v1.3.apk`.
- Release copy and Downloads copy hash-verified. SHA-256:
  `dabc5f5fc407ab3f6f3d04d7e8fe580417ff0f676f3b8f1143de4ad4b3f2c7ff`.
- This update changed only Android code/docs: **no ESP8266 reflash was performed**.

No Android phone was attached to ADB. Actual notification-button delivery with
the phone locked and the Home-screen interaction still need the phone acceptance
test in README.md after installing the update.

## Fixed 0x27 LCD initialization and reflash

At the user's request, the firmware now explicitly initializes the LCD at the
previously working **0x27** address rather than skipping initialization if a
startup scan fails. Startup allows 250 ms for power settling and retries the
configured address up to five times with 100 ms between unsuccessful probes.
Initialization is attempted even after those probes fail. The post-init probe
remains diagnostic: `/status` reports both `lcd_configured_address` and the
actually acknowledged `lcd_address`, without treating configuration as detection.

The new firmware compiled and was uploaded to the idle counter; the 375,056-byte
flash image's hash verified. **After this upload, the LCD acknowledged 0x27**
(`lcd_address: 39`, `lcd_configured_address: 39`). Wi-Fi and discovery were
ready, storage was healthy, and the existing native pairing key authenticated.
Threshold **29** and timings **800 / 2000 / 10000 ms** were preserved. Snapshots
are `.local/before-fixed-lcd.json` and `.local/after-fixed-lcd.json`; the private
binary is `.local/fixed-lcd-firmware.bin`, with the previous discovery firmware
retained in `.local/rollback/before-fixed-lcd.ino.bin`.

Build usage: RAM 32,988 / 80,192, IRAM including cache 61,567 / 65,536, flash code
336,300 / 1,048,576 bytes. There were only the existing LCD architecture/Python
escape warnings. Visual confirmation of readable text/IP menu remains a
physical-device check; the responding address alone does not prove rendering.

## Android 1.3 and IP/discovery firmware

The user requested a live IP address in the physical settings menu, easier
pairing, separate Settings/Logs screens, and a visible app notification icon.

Changes implemented:

- Separate Home, Settings, and Logs activities share bottom navigation. Home
  contains the session count/progress and connection summary; configuration and
  history no longer fill the same screen. Existing preferences/tone/history are
  retained. QR/file pairing replaces typing as the primary setup path, with
  manual fields available as a fallback.
- Local QR scanning uses ZXing Android Embedded 4.3.0 and core 3.5.3. The strict
  setup format permits only a native key, optional private IPv4 hint, device ID,
  and supported format version. Imports are size limited and error messages
  never include their contents. The QR exporter runs locally with qrcode 8.2
  and pypng 0.20220715.0; it preserves the existing key. The private QR/text file
  are ignored by directory and filename. No key download endpoint was added.
- Android discovers `_whistle._tcp` services in bounded 15-second bursts,
  retries after communication failures, and pins known counter identity. Hints
  are accepted as a new stored address only after HMAC/identity validation.
  Address changes preserve the cursor, pairing, and last session state.
- Firmware advertises `WhistleCounter-<chip-id>` and
  `whistle-counter-<chip-id>.local` with the core-bundled ESP8266mDNS library.
  Settings has a fifth, read-only **WiFi address** page showing the live address
  or `Not connected`. Detection, alarm acknowledgement, and flash layout remain.
- Counter notifications now use a white silhouette on a transparent background
  for the small icon, with the colored cooker icon available as the large icon.
  The old filled background was unsuitable for Android's monochrome masking.

Verification completed:

- Android assembly and **44 unit tests passed** (34 previous tests plus 6 setup
  import/parser and 4 discovery/address-selection tests).
- Android lint passed: **0 errors, 4 pinned SDK/tool/dependency warnings**.
  Android 8 compatibility was retained using a version-qualified navigation-bar
  theme; new screen text placeholders are Android string resources.
- Offline QR was generated and decoded locally with ZXing; decoded data exactly
  matched the private setup file without printing credentials.
- APK version **1.3 / code 4** verified, signing certificate matches 1.2, and
  decompressed APK contains neither local Wi-Fi password nor native pairing key.
  The APK, private QR, and setup file were copied and hash-verified in Downloads.
  APK SHA-256: `23453568d3d8fa742bcb1393d8fd10a5f11e074d0feb759f1fb2714c5c7b113a`.
- ESP8266 firmware compiled: RAM 32,848 / 80,192; IRAM including cache
  61,567 / 65,536; flash code 336,380 / 1,048,576 bytes. All **23 firmware
  detector/settings regression tests passed**. Binary retained privately in
  `.local/discovery-firmware.bin`.

After the user connected the idle counter, **the IP/discovery firmware was
uploaded successfully** over `/dev/ttyUSB0` with the existing 160 MHz, DOUT,
4MB (FS:1MB), Only Sketch settings. The 375,008-byte binary's flash hash verified.
A live UDP DNS-SD query received `WhistleCounter-99eb61._whistle._tcp.local`,
target `whistle-counter-99eb61.local`, address **192.168.0.116**, port 80. HTTP
status confirmed discovery ready and healthy storage. The authenticated native
API still accepted the existing pairing key. Saved threshold **29** and timings
**800 / 2000 / 10000 ms** were retained. The snapshot is saved privately as
`.local/after-discovery-upload.json`.

Initially the LCD was **not detected** (`lcd_address: 0`) after upload or a second idle
hardware reset with stable ESP power. Serial confirmed no ACK at 0x27 or 0x3F;
the user then requested the prior address explicitly. The fixed-0x27 reflash
described above restored its ACK; readable text and the IP page still need
visual confirmation.
No filesystem initializer, format, or pairing-key change was performed.

DHCP-change recovery, camera scan/navigation, and notification appearance still
need an Android device check; no ADB phone or Android emulator/KVM is available.
This build does not claim screenshot-based verification of the new layout.

## Android 1.2: graceful shutdown and per-type notification settings

The user reported critical power-off alerts even after a complete operation.
The previous service classified every timeout as critical and reposted its
offline notification on service recovery regardless of the last counter mode.

Version 1.2 (version code 3) classifies an outage as critical only when the last
authenticated state was `listening` with count below target. Completed, idle,
cancelled, and first-contact outages use an optional ordinary category, off by
default. Timeout still stops the completion player/vibration. The dashboard and
foreground monitoring indicator distinguish critical unfinished counting from
ordinary offline, and normal offline hides expected connection-error text.
Persisted outages are not reposted or sounded on service restart; old version
1.1's unconditional offline notice is removed during upgrade.

Twelve notification categories have independent saved switches and Android
channels, including separate active/idle disconnect controls. Existing Android
settings for legacy shared channels are carried into new categories when first
created. Muted activity still advances the event cursor and remains in history;
reenabling does not replay it. Turning off a category removes its tagged notices.
The foreground indicator and continuous physical-counter-controlled completion
alarm remain independent of the optional banner controls.

Verification completed:

- APK assembly passed; all **34 JUnit tests** passed (17 monitor policy,
  4 notification category, 4 protocol, 5 recovery, 4 tone-file tests).
- Regressions cover completion without acknowledgement followed by shutdown,
  physical acknowledgement/reset/cancellation before shutdown, idle and initial
  unreachable states, a subsequent unfinished session, reconnect/new outages,
  and persisted graceful/critical outages. Notification tests verify quiet
  shutdown defaults, event routing, and independent preference/channel IDs.
- Android lint passed: **0 errors, 3 existing pinned SDK/tool version warnings**.
- APK signature verified and certificate matches version 1.1. The decompressed
  APK contains neither the local pairing key nor Wi-Fi password. Version 1.1 is
  retained locally under `.local/rollback/Whistle-Counter-Local-v1.1.apk`.
- No firmware or board settings changed, and no ESP8266 upload was performed.
- No Android device was attached to ADB. Actual-phone shutdown timing, category
  switches, channel sounds, and background recovery need the acceptance check
  documented in README.md after installing this update.

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
