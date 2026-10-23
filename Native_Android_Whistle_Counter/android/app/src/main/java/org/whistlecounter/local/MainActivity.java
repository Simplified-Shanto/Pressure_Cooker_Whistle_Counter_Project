package org.whistlecounter.local;

import android.Manifest;
import android.app.AlertDialog;
import android.content.*;
import android.content.pm.PackageManager;
import android.os.*;
import android.view.View;
import android.widget.*;

public final class MainActivity extends AppScreen {
    private SharedPreferences prefs;
    private TextView status, count, detail, connection, guidance;
    private ProgressBar progress;
    private Button acknowledge;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final Runnable refresh = new Runnable() {
        @Override public void run() { render(); ui.postDelayed(this, 1000); }
    };

    @Override public void onCreate(Bundle state) {
        super.onCreate(state); prefs = getSharedPreferences(CounterService.PREFS, MODE_PRIVATE);
        LinearLayout body = page("Whistle Counter", "A little peace of mind in the kitchen.", 0);
        LinearLayout session = card(body, GREEN);
        ImageView icon = new ImageView(this); icon.setImageResource(R.drawable.ic_counter);
        session.addView(icon, new LinearLayout.LayoutParams(dp(48), dp(48)));
        text(session, "CURRENT SESSION", 12, ACCENT, true);
        status = text(session, "Waiting for your counter", 23, 0xffffffff, true);
        count = text(session, "—", 66, ACCENT, true);
        detail = text(session, "", 16, 0xffd4e0d8, false);
        progress = new ProgressBar(this, null, android.R.attr.progressBarStyleHorizontal);
        progress.setProgressTintList(android.content.res.ColorStateList.valueOf(ACCENT));
        LinearLayout.LayoutParams bar = new LinearLayout.LayoutParams(-1, dp(8)); bar.topMargin = dp(24); bar.bottomMargin = dp(12);
        session.addView(progress, bar);
        connection = text(session, "", 13, 0xffb7cec1, false);
        acknowledge = button(session, "Acknowledge power-off alert", view -> acknowledgeOutage());
        acknowledge.setVisibility(View.GONE);
        LinearLayout next = card(body, 0xfffffefa);
        text(next, "Ready when you are", 21, INK, true);
        guidance = text(next, "", 15, MUTED, false);
        button(next, "Counter settings", view -> startActivity(new Intent(this, SettingsActivity.class)));
        button(next, "View activity logs", view -> startActivity(new Intent(this, LogsActivity.class)));
    }

    @Override public void onResume() {
        super.onResume(); ui.post(refresh);
        if (prefs.getBoolean("monitoring", false) && (Build.VERSION.SDK_INT < 33 ||
                checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) == PackageManager.PERMISSION_GRANTED)) {
            MonitorRecovery.schedule(this); MonitorRecovery.resume(this, "home opened", true);
        }
    }
    @Override public void onPause() { ui.removeCallbacks(refresh); super.onPause(); }

    private void render() {
        boolean enabled = prefs.getBoolean("monitoring", false), seen = prefs.getBoolean("seen", false),
                offline = prefs.getBoolean("offline", false), alarm = prefs.getBoolean("alarm", false);
        int value = prefs.getInt("count", 0), target = prefs.getInt("target", 1);
        String mode = prefs.getString("mode", "home");
        String outage = prefs.getString(OutageAcknowledgementReceiver.OUTAGE_ID, "");
        boolean acknowledged = offline && !outage.isEmpty() &&
                outage.equals(prefs.getString(OutageAcknowledgementReceiver.ACKNOWLEDGED_ID, ""));
        boolean critical = offline && !acknowledged && MonitorPolicy.unfinishedCounting(seen, mode, value, target);
        long heartbeat = prefs.getLong("service_heartbeat_wall", 0), now = System.currentTimeMillis();
        boolean alive = heartbeat > 0 && now >= heartbeat && now - heartbeat <= RecoveryPolicy.HEALTHY_FOR_MS;
        status.setText(!enabled ? "Ready to connect" : !alive ? "Background access needs attention" : critical ?
                "Connection lost during counting" : offline && acknowledged ? "Power-off alert acknowledged" : offline ? "Counter offline" : alarm ?
                "Your target is complete" : !seen ? "Finding your counter…" : mode.equals("listening") ? "Listening for whistles" : "Counter connected");
        count.setText(seen ? value + " / " + target : "—");
        detail.setText(critical ? "Check the cooker, then acknowledge this outage below." : alarm ?
                "Reset or acknowledge on the physical counter to stop both alarms." : offline ?
                acknowledged ? "This outage is acknowledged. Monitoring continues and reconnects automatically." :
                "Waiting quietly for the next connection." : mode.equals("listening") ?
                "Each completed whistle updates your total." : "Choose a target and start listening on the counter.");
        progress.setMax(Math.max(1, target)); progress.setProgress(seen ? value : 0);
        acknowledge.setVisibility(critical ? View.VISIBLE : View.GONE);
        String address = prefs.getString("host", "");
        long last = prefs.getLong("last_seen_wall", 0);
        String age = last == 0 ? "No contact yet" : getString(R.string.last_contact, Math.max(0, (System.currentTimeMillis() - last) / 1000));
        connection.setText(getString(R.string.connection_summary,
                address.isEmpty() ? "Automatic Wi-Fi discovery" : getString(R.string.wifi_address, address), age));
        String problem = prefs.getString("recovery_error", "");
        guidance.setText(!enabled ? "Open Settings to scan your pairing QR and enable automatic monitoring once." :
                !problem.isEmpty() ? problem : "Monitoring continues with the screen locked. Find notification choices and your alarm sound in Settings; recent events are in Logs.");
    }

    private void acknowledgeOutage() {
        final String id = prefs.getString(OutageAcknowledgementReceiver.OUTAGE_ID, "");
        if (id.isEmpty()) return;
        new AlertDialog.Builder(this).setTitle("Acknowledge this outage")
                .setItems(new String[]{"Intentional power-off", "Checked and resolved"}, (dialog, which) ->
                        sendBroadcast(OutageAcknowledgementReceiver.intent(this, id, which == 0 ? "intentional" : "resolved")))
                .setNegativeButton("Cancel", null).show();
    }
}
