# Local Android installation build

`Whistle-Counter-Local-debug.apk` is generated locally and excluded from Git.
Its SHA-256 is recorded in `SHA256SUMS`. Transfer it to the phone and install
it as **Whistle Counter Local**. This installs separately from the existing PWA.

Current build: **1.1 (version code 2)**. Install over native version 1.0 to keep
pairing/settings. It adds automatic monitoring recovery and selectable alarm
sounds. A verified update copy is also in the computer's Downloads folder.
Open it once to approve background access; no counter firmware update is needed.

Rebuild with `./gradlew assembleDebug` from the `android/` project directory, then
copy `app/build/outputs/apk/debug/app-debug.apk` here. See the parent README for
the firmware, native pairing key, background permission, and alarm tests.

The app has no embedded pairing key or Wi-Fi password. Pair it locally after
the native firmware is installed. This debug APK is for direct installation;
it has not been published to an app store.
