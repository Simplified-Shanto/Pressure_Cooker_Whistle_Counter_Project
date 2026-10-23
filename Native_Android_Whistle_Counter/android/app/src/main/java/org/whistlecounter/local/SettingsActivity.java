package org.whistlecounter.local;

import android.Manifest;
import android.app.*;
import android.content.*;
import android.content.pm.PackageManager;
import android.graphics.Color;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.media.AudioManager;
import android.media.MediaPlayer;
import android.media.RingtoneManager;
import android.net.Uri;
import android.os.*;
import android.provider.Settings;
import android.provider.OpenableColumns;
import android.text.InputType;
import android.view.*;
import android.widget.*;
import java.io.*;

public final class SettingsActivity extends AppScreen {
    private final Handler ui = new Handler(Looper.getMainLooper());
    private SharedPreferences prefs;
    private EditText host, key;
    private TextView error, battery, tone, discovery;
    private Button scanPair, importPair;
    private String expectedDevice = "";
    private Button start, stop, chooseTone, background;
    private final java.util.Map<NotificationType, Switch> notificationSwitches = new java.util.EnumMap<>(NotificationType.class);
    private boolean importingTone;
    private final Runnable refresh = new Runnable() {
        @Override public void run() { render(); ui.postDelayed(this, 1000); }
    };

    @Override public void onCreate(Bundle state) {
        super.onCreate(state);
        prefs = getSharedPreferences(CounterService.PREFS, MODE_PRIVATE);
        NotificationSettings.createChannels(getSystemService(NotificationManager.class));
        LinearLayout page = page("Settings", "Connection, alerts and your alarm sound.", 2);
        LinearLayout setup = card(page, Color.WHITE);
        text(setup, "Your counter", 21, INK, true);
        text(setup, "After setup, close this screen. Monitoring stays enabled and reconnects whenever you power the counter on.", 14, 0xff627268, false);
        scanPair = button(setup, "Scan pairing QR", view -> scanPairing());
        importPair = button(setup, "Import setup file", view -> startActivityForResult(
                new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("text/*").addCategory(Intent.CATEGORY_OPENABLE), 5));
        discovery = text(setup, "", 14, MUTED, false);
        button(setup, "Find counter on Wi-Fi", view -> findCounter());
        LinearLayout advanced = new LinearLayout(this); advanced.setOrientation(LinearLayout.VERTICAL);
        advanced.setVisibility(View.GONE); setup.addView(advanced);
        button(setup, "Manual connection details", view -> advanced.setVisibility(
                advanced.getVisibility() == View.VISIBLE ? View.GONE : View.VISIBLE));
        text(advanced, "IP address (optional with automatic discovery)", 15, INK, false);
        host = new EditText(this); host.setSingleLine(true); host.setHint("192.168.0.116");
        host.setInputType(InputType.TYPE_CLASS_PHONE); host.setText(prefs.getString("host", "")); advanced.addView(host);
        text(advanced, "Pairing key (manual fallback)", 15, INK, false);
        key = new EditText(this); key.setSingleLine(true); key.setHint("Paste the 64-character native key");
        key.setInputType(InputType.TYPE_CLASS_TEXT | InputType.TYPE_TEXT_VARIATION_PASSWORD);
        key.setImportantForAutofill(View.IMPORTANT_FOR_AUTOFILL_NO);
        key.setText(prefs.getString("pairing_key", "")); advanced.addView(key);
        start = button(setup, "Save & enable automatically", view -> start());
        stop = button(setup, "Pause automatic monitoring", view -> startService(new Intent(this, CounterService.class).setAction(CounterService.STOP)));
        text(setup, "The phone has no completion-alarm silence button. A counter button or hardware RST stops both alarms. After 45 seconds without contact, the tone stops. Losing contact during unfinished counting raises a critical alert; shutdown after completion or while idle is quiet by default.", 14, 0xff627268, false);

        LinearLayout permissions = card(page, Color.WHITE);
        text(permissions, "One-time background access", 21, INK, true);
        battery = text(permissions, "", 15, INK, false);
        background = button(permissions, "Allow automatic background use", view -> requestBackgroundAccess());
        button(permissions, "Open phone app settings", view -> startActivity(new Intent(Settings.ACTION_APPLICATION_DETAILS_SETTINGS,
                Uri.parse("package:" + getPackageName()))));
        String manufacturer = Build.MANUFACTURER.toLowerCase(java.util.Locale.ROOT);
        String vendor = manufacturer.contains("xiaomi") || manufacturer.contains("poco") || manufacturer.contains("redmi") ?
                "Xiaomi / Redmi / POCO: enable Settings → Apps → Permissions → Background autostart for this app. Set its battery use to No restrictions and exclude it from the cleaner." :
                "If your phone has an Autostart or protected-app setting, enable it for Whistle Counter Local and allow unrestricted battery use.";
        text(permissions, vendor + "\nKeep Wi-Fi on and lock-screen notifications allowed. Monitoring uses an ongoing notification and consumes battery. Force stop disables Android apps until you open them again.", 14, 0xff627268, false);

        LinearLayout sounds = card(page, Color.WHITE);
        text(sounds, "Completion alarm sound", 21, INK, true);
        tone = text(sounds, "", 15, INK, false);
        chooseTone = button(sounds, "Choose alarm tone", view -> chooseAlarmTone());
        text(sounds, "Choose the built-in sound, an Android alarm tone, or an audio file. Your selection is saved on this phone and loops until the counter resets or is acknowledged. Tone selection is unavailable during listening or an active alarm.", 14, 0xff627268, false);

        LinearLayout alerts = card(page, Color.WHITE);
        text(alerts, "Notifications", 21, INK, true);
        text(alerts, "Choose which alerts this phone receives. Choices are saved immediately. Activity remains in the app even when a notification is off.", 14, 0xff627268, false);
        for (NotificationType type : NotificationType.values()) {
            Switch toggle = new Switch(this);
            toggle.setText(type.label); toggle.setTextSize(16); toggle.setTextColor(INK);
            toggle.setMinHeight(dp(48)); toggle.setSwitchPadding(dp(12));
            toggle.setChecked(NotificationSettings.enabled(prefs, type));
            toggle.setOnCheckedChangeListener((view, checked) -> {
                prefs.edit().putBoolean(type.preferenceKey(), checked).apply();
                if (!checked) NotificationSettings.cancelType(getSystemService(NotificationManager.class), type);
            });
            alerts.addView(toggle, new LinearLayout.LayoutParams(-1, -2));
            text(alerts, type.description, 14, 0xff627268, false);
            notificationSwitches.put(type, toggle);
        }
        text(alerts, "These switches control notification banners and their sounds, not the continuous completion alarm. The monitoring indicator stays on while automatic monitoring is enabled. Android's notification settings can further control each category's sound and vibration.", 14, 0xff627268, false);
        button(alerts, "Open Android notification settings", view -> startActivity(
                new Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS).putExtra(Settings.EXTRA_APP_PACKAGE, getPackageName())));

        error = text(page, "", 15, 0xffa2372b, false);
    }

    private void start() {
        if (Build.VERSION.SDK_INT >= 33 && checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            requestPermissions(new String[]{Manifest.permission.POST_NOTIFICATIONS}, 1); return;
        }
        if (!getSystemService(NotificationManager.class).areNotificationsEnabled()) {
            startActivity(new Intent(Settings.ACTION_APP_NOTIFICATION_SETTINGS).putExtra(Settings.EXTRA_APP_PACKAGE, getPackageName()));
            return;
        }
        try {
            if (!prefs.getBoolean("monitoring", false)) {
                String address = Protocol.optionalLocalHost(host.getText().toString());
                String secret = key.getText().toString().trim().toLowerCase(java.util.Locale.ROOT);
                Protocol.key(secret);
                boolean changed = !secret.equals(prefs.getString("pairing_key", ""));
                SharedPreferences.Editor editor = prefs.edit().putString("host", address).putString("pairing_key", secret)
                        .putBoolean("monitoring", true).putString("error", "");
                if (!expectedDevice.isEmpty()) editor.putString("paired_device", expectedDevice);
                else if (changed) editor.remove("paired_device");
                if (changed) editor.putString("boot", "").putString("device", "").putLong("cursor", 0)
                        .putBoolean("seen", false).putBoolean("alarm", false).putBoolean("offline", false)
                        .remove(OutageAcknowledgementReceiver.OUTAGE_ID).remove(OutageAcknowledgementReceiver.ACKNOWLEDGED_ID)
                        .putString("mode", "home").putInt("count", 0).putInt("target", 1)
                        .putLong("last_seen_wall", System.currentTimeMillis());
                editor.apply();
            }
            MonitorRecovery.schedule(this);
            MonitorRecovery.resume(this, "automatic monitoring enabled", true);
        } catch (Exception problem) { prefs.edit().putString("error", problem.getMessage()).apply(); }
        render();
        promptBackgroundOnce();
    }

    @Override public void onRequestPermissionsResult(int request, String[] permissions, int[] grants) {
        super.onRequestPermissionsResult(request, permissions, grants);
        if (request == 1 && grants.length > 0 && grants[0] == PackageManager.PERMISSION_GRANTED) start();
    }

    @Override public void onResume() {
        super.onResume(); ui.post(refresh);
        // Opening the app explicitly resumes a previously enabled monitor after OS termination.
        if (prefs.getBoolean("monitoring", false) && (Build.VERSION.SDK_INT < 33 ||
                checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED)) {
            MonitorRecovery.schedule(this);
            MonitorRecovery.resume(this, "app opened", true);
            promptBackgroundOnce();
        }
    }

    @Override public void onPause() { ui.removeCallbacks(refresh); super.onPause(); }

    private void render() {
        boolean enabled = prefs.getBoolean("monitoring", false), seen = prefs.getBoolean("seen", false);
        boolean alarm = prefs.getBoolean("alarm", false), offline = prefs.getBoolean("offline", false);
        String mode = prefs.getString("mode", "home");
        host.setEnabled(!enabled); key.setEnabled(!enabled);
        long heartbeat = prefs.getLong("service_heartbeat_wall", 0);
        boolean alive = heartbeat > 0 && System.currentTimeMillis() >= heartbeat &&
                System.currentTimeMillis() - heartbeat <= RecoveryPolicy.HEALTHY_FOR_MS;
        start.setText(enabled ? alive ? "Automatic monitoring enabled" : "Repair automatic monitoring" : "Save & enable automatically");
        start.setEnabled(!enabled || !alive);
        stop.setEnabled(enabled && !alarm && (offline || !mode.equals("listening")));
        stop.setText(alarm ? "Reset or acknowledge on counter" : mode.equals("listening") && !offline ? "Finish or cancel on counter first" : "Pause automatic monitoring");
        scanPair.setEnabled(!enabled); importPair.setEnabled(!enabled);
        String lookup = prefs.getString("discovery_status", "Automatic discovery follows the counter when its IP changes.");
        String address = prefs.getString("host", "");
        discovery.setText(address.isEmpty() ? lookup : getString(R.string.discovery_summary, lookup, address));
        String problem = prefs.getString("error", "");
        String recovery = prefs.getString("recovery_error", "");
        if (!recovery.isEmpty()) problem += "\n" + recovery;
        String toneError = prefs.getString("tone_error", "");
        if (!toneError.isEmpty()) problem += "\n" + toneError;
        if (getSystemService(AudioManager.class).getStreamVolume(AudioManager.STREAM_ALARM) == 0)
            problem = "Android alarm volume is zero. Raise it in Android sound settings.\n" + problem;
        error.setText(problem);
        boolean unrestricted = getSystemService(PowerManager.class).isIgnoringBatteryOptimizations(getPackageName());
        battery.setText(unrestricted ? "Android background access allowed. Automatic monitoring is " + (enabled ? "enabled." : "paused.") :
                "Approval needed once: allow background use so the app can monitor and recover with the phone locked.");
        background.setEnabled(!unrestricted);
        background.setText(unrestricted ? "Background use allowed" : "Allow automatic background use");
        tone.setText(importingTone ? "Saving selected sound…" : prefs.getString("tone_title", "Built-in Whistle Counter alarm"));
        chooseTone.setEnabled(!importingTone && !alarm && (!mode.equals("listening") || offline));
        for (NotificationType type : NotificationType.values()) {
            Switch toggle = notificationSwitches.get(type);
            boolean checked = NotificationSettings.enabled(prefs, type);
            if (toggle.isChecked() != checked) toggle.setChecked(checked);
        }
    }

    @android.annotation.SuppressLint("BatteryLife")
    private void requestBackgroundAccess() {
        // Core function: real-time local peripheral monitoring without internet.
        // Cloud push cannot replace this Wi-Fi link. The user approves Android's
        // explicit exemption prompt; no setting is changed without that approval.
        if (getSystemService(PowerManager.class).isIgnoringBatteryOptimizations(getPackageName())) return;
        try {
            startActivity(new Intent(Settings.ACTION_REQUEST_IGNORE_BATTERY_OPTIMIZATIONS, Uri.parse("package:" + getPackageName())));
        } catch (ActivityNotFoundException unavailable) {
            startActivity(new Intent(Settings.ACTION_IGNORE_BATTERY_OPTIMIZATION_SETTINGS));
        }
    }

    private void promptBackgroundOnce() {
        if (prefs.getBoolean("monitoring", false) &&
                !getSystemService(PowerManager.class).isIgnoringBatteryOptimizations(getPackageName()) &&
                prefs.getInt("background_prompt_version", 0) < 2) {
            prefs.edit().putInt("background_prompt_version", 2).apply();
            requestBackgroundAccess();
        }
    }

    private void chooseAlarmTone() {
        new AlertDialog.Builder(this).setTitle("Completion alarm sound")
                .setItems(new String[]{"Built-in Whistle Counter alarm", "Choose Android alarm tone", "Choose audio file"}, (dialog, which) -> {
                    if (which == 0) {
                        prefs.edit().putBoolean("custom_tone", false).putString("tone_title", "Built-in Whistle Counter alarm").putString("tone_error", "").apply();
                    } else if (which == 1) {
                        Intent picker = new Intent(RingtoneManager.ACTION_RINGTONE_PICKER)
                                .putExtra(RingtoneManager.EXTRA_RINGTONE_TYPE, RingtoneManager.TYPE_ALARM)
                                .putExtra(RingtoneManager.EXTRA_RINGTONE_SHOW_SILENT, false)
                                .putExtra(RingtoneManager.EXTRA_RINGTONE_SHOW_DEFAULT, true)
                                .putExtra(RingtoneManager.EXTRA_RINGTONE_TITLE, "Choose completion alarm");
                        try { startActivityForResult(picker, 2); }
                        catch (ActivityNotFoundException absent) { chooseAudioFile(); }
                    } else chooseAudioFile();
                    render();
                }).show();
    }

    private void chooseAudioFile() {
        startActivityForResult(new Intent(Intent.ACTION_OPEN_DOCUMENT).setType("audio/*").addCategory(Intent.CATEGORY_OPENABLE), 3);
    }

    @Override protected void onActivityResult(int request, int result, Intent data) {
        super.onActivityResult(request, result, data);
        com.google.zxing.integration.android.IntentResult scanned = com.google.zxing.integration.android.IntentIntegrator.parseActivityResult(request, result, data);
        if (scanned != null) {
            if (scanned.getContents() != null) applyPairing(scanned.getContents());
            return;
        }
        if (request == 5) {
            if (result == RESULT_OK && data != null && data.getData() != null) {
                new Thread(() -> {
                    try (InputStream input = getContentResolver().openInputStream(data.getData())) {
                        String payload = PairingSetup.read(input);
                        runOnUiThread(() -> { if (!isDestroyed()) applyPairing(payload); });
                    } catch (Exception problem) {
                        runOnUiThread(() -> Toast.makeText(this, "Could not read that setup file.", Toast.LENGTH_LONG).show());
                    }
                }, "ImportCounterSetup").start();
            }
            return;
        }
        if (result != RESULT_OK || data == null || (request != 2 && request != 3)) return;
        if (prefs.getBoolean("alarm", false) || (prefs.getString("mode", "home").equals("listening") && !prefs.getBoolean("offline", false))) {
            prefs.edit().putString("tone_error", "Choose the next alarm sound when the counter is idle.").apply(); return;
        }
        Uri uri = request == 2 ? data.getParcelableExtra(RingtoneManager.EXTRA_RINGTONE_PICKED_URI) : data.getData();
        if (uri == null) return;
        importTone(uri, request == 2);
    }

    private void importTone(Uri uri, boolean androidTone) {
        importingTone = true; render();
        new Thread(() -> {
            File staged = null;
            try {
                String title = "Selected audio file";
                if (androidTone) {
                    android.media.Ringtone ringtone = RingtoneManager.getRingtone(this, uri);
                    if (ringtone != null) title = ringtone.getTitle(this);
                } else {
                    try (android.database.Cursor cursor = getContentResolver().query(uri, new String[]{OpenableColumns.DISPLAY_NAME}, null, null, null)) {
                        if (cursor != null && cursor.moveToFirst()) title = cursor.getString(0);
                    }
                }
                try (InputStream input = getContentResolver().openInputStream(uri)) {
                    if (input == null) throw new IOException("Could not open the selected audio.");
                    staged = ToneFiles.stage(input, AlarmAudio.folder(this));
                }
                MediaPlayer probe = new MediaPlayer();
                try { probe.setDataSource(staged.getAbsolutePath()); probe.prepare(); }
                finally { probe.release(); }
                File saved = ToneFiles.commit(staged); staged = null;
                if (!prefs.edit().putBoolean("custom_tone", true).putString("tone_file", saved.getName())
                        .putString("tone_title", title).putString("tone_error", "").commit())
                    throw new IOException("Could not save alarm preferences. Choose the sound again.");
                ToneFiles.cleanExcept(AlarmAudio.folder(this), saved.getName());
            } catch (Exception problem) {
                prefs.edit().putString("tone_error", "Could not save that sound: " + problem.getMessage() + " Try Choose audio file.").apply();
            } finally {
                if (staged != null) staged.delete();
                runOnUiThread(() -> { importingTone = false; if (!isDestroyed()) render(); });
            }
        }, "SaveAlarmTone").start();
    }

    private void scanPairing() {
        new com.google.zxing.integration.android.IntentIntegrator(this)
                .setDesiredBarcodeFormats(java.util.Collections.singletonList("QR_CODE"))
                .setCaptureActivity(PairingScanActivity.class).setOrientationLocked(false)
                .setBeepEnabled(false).setPrompt("Scan your counter's private setup QR").initiateScan();
    }

    private void applyPairing(String payload) {
        if (prefs.getBoolean("monitoring", false)) return;
        try {
            PairingSetup setup = PairingSetup.parse(payload);
            new AlertDialog.Builder(this).setTitle("Connect to this counter?")
                    .setMessage("Pair with " + (setup.device.isEmpty() ? "your whistle counter" : "counter " + setup.device) +
                            ". Its address will be found automatically on your Wi-Fi.")
                    .setPositiveButton("Connect", (dialog, which) -> {
                        if (prefs.getBoolean("monitoring", false)) return;
                        host.setText(setup.host); key.setText(setup.key); expectedDevice = setup.device; start();
                    }).setNegativeButton("Cancel", null).show();
        } catch (IllegalArgumentException problem) {
            Toast.makeText(this, "This is not a valid Whistle Counter setup code.", Toast.LENGTH_LONG).show();
        }
    }

    private void findCounter() {
        if (!MonitorRecovery.configured(prefs)) {
            Toast.makeText(this, "Scan or import your setup code first.", Toast.LENGTH_LONG).show(); return;
        }
        if (!prefs.getBoolean("monitoring", false)) { start(); return; }
        startForegroundService(new Intent(this, CounterService.class).setAction(CounterService.DISCOVER));
        Toast.makeText(this, "Looking for your paired counter on Wi-Fi…", Toast.LENGTH_SHORT).show();
    }
}
