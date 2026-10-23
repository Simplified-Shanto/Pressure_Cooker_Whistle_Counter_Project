# Whistle Counter Local — native Android alternative

This version connects an Android app directly to the existing Generic ESP8266
over home Wi-Fi. It needs **no cloud service, hosting subscription, internet
connection, or computer left running**. The phone and counter must be on the
same reachable Wi-Fi network; the Wi-Fi router must stay powered.

The `WiFi_Whistle_Counter/` web/PWA source and configuration are preserved. This
alternative supplies a separate firmware build and Android APK. At the user's
request, the physical ESP8266 was switched to native firmware on 23 October
2026; its verified local IP was `192.168.0.116`. Only one firmware can run on
the physical ESP8266 at a time. See `VALIDATION.md` for the checks performed.

## Whistle detection settings

The native firmware now defaults to **0.5 seconds minimum sound**, **1.5 seconds
of ending quiet**, and **10 seconds cooldown after each count**. These are
adjustable starting values, not universal timings for every cooker. The existing
amplitude threshold is preserved when upgrading.

From Home, **hold Bottom** to open settings. **Tap Bottom** to cycle through
Threshold, Min sound, End quiet, and Cooldown. **Top increases**, **Middle
decreases**, and **hold Bottom** again to save all changes and exit. Saved values
survive power-off. The phone app requires no update. See
[detection tuning](DETECTION_TUNING.md) for research sources, ranges, and counting
tradeoffs. In particular, the ending-quiet interval delays the emitted count,
while cooldown suppresses subsequent bursts after that count.

## Alarm behavior

| Action or event | Physical counter | Android phone |
| --- | --- | --- |
| Counter first becomes reachable | Normal controls | Connected notification |
| Each detected whistle | LCD count increases | One notification with the total count |
| Target is reached | Buzzer alarm runs | Separate completion notification plus a looping alarm tone and vibration |
| Press a counter button to acknowledge | Alarm stops; returns home | Authenticated next snapshot stops the completion alarm |
| Press the counter hardware RST button | Reboots; stops its alarm | New authenticated boot identity stops the phone alarm and reports restart |
| No authenticated contact for 45 seconds during unfinished whistle counting | Continues locally if it still has power | One critical **Connection lost during counting** notification, enabled by default |
| Acknowledge a connection-loss alert on the phone | No command sent; counter may still be off | Current outage becomes quiet; monitoring remains enabled and the acknowledgement is logged |
| No authenticated contact for 45 seconds after reaching the target, cancellation, or while idle | Off or unreachable | Completion tone stops; waits quietly for the next connection by default |
| Connection returns, counter still alarming | Buzzer continues | Phone completion alarm starts again |
| Connection returns after power cycle | Fresh boot/home | Restart/reconnection notification; critical unreachable alert clears |

There is **no phone silence, snooze, or counter-reset action**. Stop monitoring
and connection edits are disabled during a reachable listening/alarm session;
the service enforces this independently of the screen. Swiping an activity or
opening a notification does not stop the monitoring service or its alarm.
Android's system controls, force-stop, volume, and phone shutdown remain under
the phone owner's control; an ordinary app cannot prevent those actions.

Power-off detection is inferred from missing contact. It takes about 45 seconds
plus request/scheduling latency, and cannot distinguish counter power loss,
router failure, phone Wi-Fi loss, or an unreachable counter. If unexpected,
the critical alert during unfinished counting tells the user to check the cooker.
After a confirmed completion, power-off is handled as a normal offline state.
The app uses the last authenticated counter state: if power disappears before
the phone observes completion, it cannot infer that the target was reached and
still treats the last observed unfinished session as critical. Cancellation and
idle shutdown are quiet by default; stopwatch/timer mode also does not imply an
unfinished whistle-counting session.

## Install the Android app

### Version 1.4: acknowledge an intentional or resolved power-off

Install **1.4 (version code 5)** over the existing native app to preserve pairing,
tones, notification choices, logs, and automatic monitoring. The updated APK is
in this computer's `Downloads/Whistle-Counter-Local-debug.apk`. **No ESP8266
reflash is needed.**

If contact is lost during unfinished counting, expand the critical notification
and tap **Acknowledge** after checking the cooker. Alternatively, open Home and
tap **Acknowledge power-off alert**, then choose **Intentional power-off** or
**Checked and resolved**. The critical notification disappears and Home/the
monitoring indicator show the outage as acknowledged. It can remain offline;
acknowledgement does not claim the counter has power or change its count.

This silences **only that outage**. It does not disable notification categories
or automatic monitoring. The acknowledgement survives app/service restarts and
phone reboot. A fresh authenticated counter response clears the old outage;
another later loss during unfinished counting gets a new alert and a new
acknowledgement action. Stale actions from an earlier outage cannot dismiss it.
Logs record acknowledgement and the selected reason without replaying counts.

**This is separate from the completion alarm.** The acknowledgement action is
only accepted for an offline, unfinished session and cannot silence a reachable
counter's target-completion alarm or reset the physical counter.

To check the update, power off during a test session, wait for the critical alert,
and acknowledge it. Keep the counter off while reopening the app: the same
outage should stay quiet and monitoring should remain enabled. Power it on,
start a new test session, and power off again to check that the new outage still
alerts. Home provides the same action if notification controls are unavailable.

### Version 1.3: separate screens, QR pairing, and automatic address discovery

Install **1.3 (version code 4)** over the existing app to keep pairing, tones,
notification choices, and monitoring enabled. The app now has three screens:

- **Home**: current count, target, progress, connection, and session guidance.
- **Settings**: connection/pairing, notification switches, tone, background access.
- **Logs**: recent counter events, including those with muted notifications.

The bottom navigation switches screens. Notifications use a transparent,
monochrome cooker silhouette in the status bar and the colored app icon in the
notification itself. The completion alarm and version 1.2 shutdown behavior are
retained.

For a new phone, open **Settings → Scan pairing QR** and scan the private setup
image displayed on this computer. The scanner operates inside the app, without
an online QR service. Alternatively, transfer `Whistle-Counter-Setup.txt` to the
phone and select **Import setup file**; no camera permission is needed for that
route. Confirm **Connect** and grant notifications/background access if asked.
The 64-character manual key remains under **Manual connection details** as a
fallback. Existing paired phones do not need to scan or import again.

Generate the private artifacts locally without replacing the existing key:

```sh
python3 -m venv .local/qr-venv
.local/qr-venv/bin/pip install -r tools/pairing-requirements.txt
.local/qr-venv/bin/python tools/export_pairing.py --device <six-hex-chip-id> --host <current-ip>
```

The host hint is optional; discovery works without it after the firmware update.
The QR and text file contain the pairing secret: keep them private, like the old
key. Both are generated in ignored `.local/` and excluded by filename from Git.
The APK does not contain these credentials. No unauthenticated pairing-key
download endpoint was added to the counter.

**This feature needs the new ESP8266 firmware for LCD IP display and discovery.**
Using the same board/Only Sketch settings, upload
`firmware/Whistle_counter_esp8266_native_multimode/`. The counter advertises
`WhistleCounter-<chip-id>` as `_whistle._tcp` on Wi-Fi. The phone uses short,
bounded discovery bursts at startup and after connection failures; an address is
saved only after the existing HMAC and device-identity checks succeed. Changing
IP does not reset pairing, the event cursor, or the last session state. If a
router blocks multicast discovery, use **Settings → Manual connection details**
and read the current IP on the physical counter.

To see the IP on the LCD: from Home **hold Bottom**, then **tap Bottom four
times** to reach **5/5 WiFi address**. It updates while the menu is open. Hold
Bottom to save any detector edits and exit; Top/Middle do nothing on this page.
The installed LCD uses a fixed **0x27** address on GPIO4 SDA / GPIO5 SCL.
Startup waits for power to settle and retries the address before initializing
there even if the initial probe failed. `/status` distinguishes the configured
address (`lcd_configured_address`) from an acknowledged responder (`lcd_address`,
zero when none responds); neither field proves that the text is readable.
`whistle-counter-<chip-id>.local` is also advertised, where the network supports
mDNS hostnames. [Android's discovery API](https://developer.android.com/develop/connectivity/wifi/use-nsd).

Before the firmware update, the new APK still works with an existing known IP,
and QR/file import can carry that IP as a hint. LCD/discovery firmware upload and
actual-phone camera/navigation/notification appearance need physical acceptance
testing; see `VALIDATION.md` for what was checked here.

### Version 1.2: graceful shutdown and notification choices

Install **1.2 (version code 3)** over the existing native app without uninstalling
to retain pairing, automatic monitoring, and the selected alarm tone. The update
copy is in this computer's `Downloads/Whistle-Counter-Local-debug.apk`. **No
ESP8266 reflash is needed.**

The **Notifications** card has separate switches for connection/reconnection,
restarts, each whistle, target reached, listening started, listening cancelled,
physical acknowledgement, timer finished, connection loss during counting,
idle/completed shutdown, missing history, and background-monitoring attention.
Choices save immediately and survive reopening/reboot. All default on except
**Offline while idle or finished**, which defaults off.

Turning a type off removes its currently displayed notifications and prevents
future notifications of that type. Events remain in Activity, and reenabling a
type does not replay its past events or an already recorded outage. The normal
monitoring indicator remains while automatic monitoring is enabled. Android's
notification settings expose the categories separately for sound/vibration and
other system preferences; in-app switches do not override Android permissions
or blocked channels. [Android notification channels](https://developer.android.com/develop/ui/views/notifications/channels).

These switches control notification banners and their sounds. They do **not**
silence the continuous completion alarm; that still follows physical
acknowledgement/reset and the missing-contact timeout. In particular, disabling
**Target reached notification** disables its ordinary banner, not the looping
completion tone. Losing contact after a target was reached stops that tone at
the existing 45-second timeout without replacing it with a critical alarm.
Service recovery no longer sounds the same persisted outage again.

To check this update on the phone:

1. Finish a test session, verify the completion alert, turn the counter off,
   and wait at least 45 seconds. The tone should stop with no critical popup;
   the app should show **Counter offline** and continue monitoring.
2. Start another session, leave its target unfinished, then turn the counter
   off. After 45 seconds, one critical notification should appear if
   **Connection lost during counting** is enabled. It should not repeat for the
   same outage when the app is reopened or its service recovers.
3. Toggle **Each whistle count** off and check another session. Counts should
   update in the dashboard and Activity without whistle popups; completion
   should still ring. Toggle it back on for subsequent counts.

### Version 1.1: automatic monitoring and selectable alarm sound

Install the new APK **over the existing app**, without uninstalling it, to retain
the native pairing key and settings. The verified update copy is also in this
computer's `Downloads/Whistle-Counter-Local-debug.apk`. **No ESP8266 reflash is
needed for this app update.**

Open version 1.1 once and approve **Allow automatic background use**. If already
paired, automatic monitoring keeps its saved enabled setting; otherwise enter
the IP/key and tap **Save & enable automatically** once. After that, leave
automatic monitoring enabled, close the screen, and use the counter normally.
The ongoing **Whistle Counter monitoring** notification confirms the foreground
service is running. Monitoring continues while the counter is off so it can
notice the next power-on without opening the app again.

On Xiaomi / Redmi / POCO phones, also enable the app in **Settings → Apps →
Permissions → Background autostart** (some HyperOS versions place Background
autostart directly under Apps). Set its battery mode to **No restrictions** and
exclude it from the phone's cleaner. These manufacturer controls cannot be
granted by the app itself. [Xiaomi's instructions](https://www.mi.com/global/support/faq/details/KA-507608/).

Version 1.1 resumes monitoring after a normal phone reboot (after the first
unlock) and an app update. It keeps the existing sticky foreground service,
registers a persisted 15-minute recovery job, and requests a best-effort recovery
check after service/task removal. A background recovery job needs the battery
exemption on Android 12+, and Android may defer jobs. These are recovery paths,
not a guaranteed immediate wake-up mechanism. Normally the foreground monitor
stays running and polls the counter once per second.
[Android background-start rules](https://developer.android.com/develop/background-work/services/fgs/restrictions-bg-start).

Android requires an initial app launch/pairing and permission approval. An app
that is force-stopped, disabled, uninstalled, or restricted by the phone cannot
guarantee automatic alerts. Android force-stop blocks further automatic work
until the user opens/interacts with the app again; this update does not attempt
to bypass that control. The app shows when background access needs attention.
[Android stopped-state behavior](https://developer.android.com/about/versions/15/behavior-changes-all#stopped-state).

Use **Choose alarm tone** to select the built-in sound, an Android alarm tone,
or an audio file up to 8 MB. Selected audio is copied into private app storage,
validated before selection, and retained through reboot and app updates. No
storage permission is needed beyond the system picker's grant. If a selected
file becomes unreadable, the alarm falls back to the built-in sound and reports
the problem. Tone changes are blocked during a reachable listening/alarm session;
there is still no phone silence or snooze control. This setting controls the
looping completion alarm; the separate critical unreachable notification uses
its own Android notification channel.

### Initial installation

1. Transfer `releases/Whistle-Counter-Local-debug.apk` to the phone and open it.
   Allow installation from the file manager/browser you use when Android asks.
   The app installs separately as **Whistle Counter Local**; it does not replace
   the installed web app. Android 8.0 or later is required.
2. Connect the phone to **Frozen**, the counter's Wi-Fi network.
3. After uploading the alternative firmware below, enter its local IP address.
   The existing device has used `192.168.0.116`; use the address printed by the
   newly running firmware, because DHCP can change it. A router DHCP reservation
   is useful to keep this address stable. Do not enter an HTTPS/tunnel address.
4. Paste the **native** key from `.local/phone-pairing.txt` in this folder. This
   is separate from the Wi-Fi password and the web app's pairing key.
5. Tap **Save & enable automatically**, and allow notifications when prompted.
6. Approve **Allow automatic background use**. If Android instead opens its battery
   optimization list, show all apps, select **Whistle Counter Local**, and allow
   unrestricted use / **Don't optimize**. On Xiaomi/other phones, also
   allow autostart and background running in the app's system settings.
7. Enable lock-screen notifications, keep Wi-Fi enabled, and set Android's alarm
   volume above zero. The app warns when alarm volume is zero. Android DND and
   system restrictions can still affect sound/delivery; test your actual phone.

The ongoing monitoring notification is expected. Monitoring keeps a bounded,
renewed partial wake lock and a Wi-Fi lock, so it consumes more phone battery
than cloud push. Enable it once during setup; the app stores that preference and
resumes it through the documented recovery paths. Pausing automatic monitoring
while idle is an explicit opt-out, and recovery jobs respect that preference.

## Prepare and upload the alternative firmware

The local Wi-Fi credentials were copied into this alternative's ignored
`WiFiSecrets.h`. Fresh checkouts must copy `WiFiSecrets.example.h` and fill in
the network details. Generate/recover the private native pairing key with:

```sh
python3 tools/prepare_device.py
```

The command preserves an existing native key and writes
`firmware/Whistle_counter_esp8266_native_multimode/NativeSecrets.h` plus
`.local/phone-pairing.txt`. Neither is committed to Git. The APK contains no
preconfigured Wi-Fi password or device pairing secret.

In Arduino IDE open:

`firmware/Whistle_counter_esp8266_native_multimode/Whistle_counter_esp8266_native_multimode.ino`

Use **esp8266 core 3.1.2 → Generic ESP8266 Module → 160 MHz → 4MB (FS:1MB) →
DOUT → Erase Flash: Only Sketch**. Keep installed LiquidCrystal_I2C 2.0.0.
The full pins and version list is in `requirements.txt`; wiring matches the
working counter. The generated native header must stay in the sketch folder.

Upload only when no real cooking session is running. The working device's
LittleFS has already been initialized and migrated to the 4 MB layout; **do not
rerun its initializer**. The native journal uses only `/native-q/`, separately
from the web firmware's `/q/` and `/boot.bin`; the saved threshold is preserved.

Open Serial Monitor at **115200** and look for:

```text
Native app counter IP: 192.168.0.116
```

Its read-only diagnostics remain available at `http://<IP>/status`.
The phone app requires the native API and will explain a 404 if pointed at the
still-running web firmware. The web backend will naturally see an outage when
you switch to native firmware; this does not remove its configuration or data.

## Build the APK yourself

Use JDK 17+ (built here with JDK 21), Android SDK platform 35 / Build Tools 35.0.0,
Android Gradle Plugin 8.13.0, and Gradle 8.14.3. Open `android/` in Android Studio
or set the SDK path in its ignored `local.properties`:

```text
sdk.dir=/absolute/path/to/Android/Sdk
```

Then:

```sh
cd android
./gradlew assembleDebug testDebugUnitTest lintDebug
```

Output: `android/app/build/outputs/apk/debug/app-debug.apk`. The runtime uses
only Android platform APIs; JUnit 4.13.2 is a test dependency. This is a debug
build for local installation, not a Google Play release. Keep the signing key
consistent to install future upgrades without clearing the app's saved pairing.

## Communication and recovery

The foreground service polls the native API about once a second, explicitly
using Wi-Fi even when Android prefers cellular because Wi-Fi has no internet.
It makes no requests to cloud services. A random challenge and HMAC-SHA256
authenticate each request and response with the local pairing key; the key is
never sent in the HTTP request. A new challenge prevents an old signed idle
snapshot from being replayed to clear a newer alarm. Phone configuration is
private app storage, with backup/transfer of that configuration excluded.

Local HTTP is not encrypted: counts are visible to a capable LAN observer.
There are no remote control routes. Only authenticated snapshots change the
phone alarm state; unauthenticated errors count toward the unreachable timeout.

The firmware retains the latest 128 native events in LittleFS during the current
boot, with a 16-event RAM fallback and 16-event response pages. The app stores
its per-boot cursor to avoid duplicate count alerts on reconnect. It separately
reads current alarm state, so an already acknowledged historical completion
does not start a new alarm. Gaps/storage faults are reported. A hardware reboot
starts a new native journal and reports a restart, rather than claiming an
interrupted session completed. The original web journal is untouched.

Optional authenticated computer diagnostic, after native firmware is installed:

```sh
python3 tools/probe_counter.py --host 192.168.0.116
```

## Phone-and-counter acceptance test

1. Install and pair. Confirm the connected notification and live dashboard.
2. Set target two on the physical counter, start, and lock the phone. Generate
   two test detections. Confirm count-1/count-2 notifications, a separate final
   notification, and the looping phone alarm alongside the physical buzzer.
3. Open/close the phone screen and app: neither should acknowledge the alarm.
4. Press a counter button: both alarms should stop within the next successful
   poll (normally around one second).
5. Repeat completion, then press hardware RST: both stop and a restart alert
   appears when the phone receives the new boot identity.
6. Repeat completion, then power the counter off: after about 45 seconds the
   phone completion tone stops and the critical unreachable notification appears.
7. Test Wi-Fi loss while the counter is still alarming. After reconnection the
   phone must ring again until acknowledged/reset on the physical counter.
8. Repeat a locked-screen test with router internet disconnected, while leaving
   the Wi-Fi router powered. This version should work without internet.
9. For version 1.1, leave automatic monitoring enabled, remove the activity from
   Recents, lock the phone, then power the counter on. Confirm connection/count
   alerts arrive without reopening the app. Repeat after a phone reboot and first
   unlock, and after updating the APK over the existing installation.
10. Select a different completion sound while idle, close the app screen, and
    reach the target. Confirm the selected sound loops and physical acknowledge /
    hardware RST still stops it. Reboot the phone and verify the sound persists.

To return to the web/PWA setup, upload the original
`WiFi_Whistle_Counter/firmware/Whistle_counter_esp8266_wifi_multimode` sketch with
the same flash layout and **Only Sketch**. Its private configuration and paired
phone subscriptions remain in the original folder/backend; its HTTPS address
and server must still be available.

Implementation references:
[Android connected-device foreground services](https://developer.android.com/develop/background-work/services/fgs/service-types#connected-device),
[starting a foreground service](https://developer.android.com/develop/background-work/services/fgs/launch),
and [Doze/battery handling](https://developer.android.com/training/monitoring-device-state/doze-standby).
