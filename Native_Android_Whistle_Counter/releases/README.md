# Local Android installation build

`Whistle-Counter-Local-debug.apk` is generated locally and excluded from Git.
Its SHA-256 is recorded in `SHA256SUMS`. Transfer it to the phone and install
it as **Whistle Counter Local**. This installs separately from the existing PWA.

Current build: **1.4 (version code 5)**. Install over native version 1.0–1.3 to
keep pairing/settings. Connection-loss notifications now have an **Acknowledge**
action; Home also offers intentional power-off / checked-and-resolved choices.
The current outage stays quiet across restarts while monitoring continues, and
a new outage after reconnection can still alert. This does not silence the
physical-counter-controlled completion alarm.

Separate Home/Settings/Logs screens, QR/file pairing, Wi-Fi discovery, notification
choices, alarm sounds, and graceful completed-session shutdown are retained.

A verified update copy is in the computer's Downloads folder, alongside the
private pairing QR and setup file. The separate firmware update is required for
the LCD's IP page and mDNS discovery; the APK still supports the existing manual
IP firmware. Do not put the private setup artifacts or compiled firmware in Git.

Rebuild with `./gradlew assembleDebug` from the `android/` project directory, then
copy `app/build/outputs/apk/debug/app-debug.apk` here. See the parent README for
the firmware, native pairing key, background permission, and alarm tests.

The app has no embedded pairing key or Wi-Fi password. Pair it locally after
the native firmware is installed. This debug APK is for direct installation;
it has not been published to an app store.
