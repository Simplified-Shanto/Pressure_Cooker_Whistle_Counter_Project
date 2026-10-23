package org.whistlecounter.local;

import android.app.NotificationManager;
import android.content.*;
import android.net.Uri;
import android.service.notification.StatusBarNotification;
import java.util.Map;

/** Acknowledges one outage locally; never sends a command to the physical counter. */
public final class OutageAcknowledgementReceiver extends BroadcastReceiver {
    public static final String ACTION = "org.whistlecounter.local.ACKNOWLEDGE_OUTAGE";
    public static final String OUTAGE_ID = "outage_id", ACKNOWLEDGED_ID = "acknowledged_outage_id";

    public static Intent intent(Context context, String id, String reason) {
        return new Intent(context, OutageAcknowledgementReceiver.class).setAction(ACTION)
                .setData(Uri.parse("whistlecounter://outage/" + id))
                .putExtra(OUTAGE_ID, id).putExtra("reason", reason);
    }

    @android.annotation.SuppressLint("ApplySharedPref")
    @Override public void onReceive(Context context, Intent intent) {
        if (intent == null || !ACTION.equals(intent.getAction())) return;
        SharedPreferences prefs = context.getSharedPreferences(CounterService.PREFS, Context.MODE_PRIVATE);
        Map<String, Object> state = new java.util.HashMap<>(prefs.getAll());
        String requested = intent.getStringExtra(OUTAGE_ID);
        String current = (String) state.getOrDefault(OUTAGE_ID, "");
        if (!MonitorPolicy.canAcknowledgeOutage(Boolean.TRUE.equals(state.get("offline")),
                Boolean.TRUE.equals(state.get("seen")), (String) state.getOrDefault("mode", "home"),
                (Integer) state.getOrDefault("count", 0), (Integer) state.getOrDefault("target", 1),
                Boolean.TRUE.equals(state.get("alarm")), current, requested) ||
                current.equals(state.get(ACKNOWLEDGED_ID))) return;
        // Write only the acknowledgement token. A concurrent reconnection/new
        // outage uses a different token and cannot be acknowledged by this action.
        if (!prefs.edit().putString(ACKNOWLEDGED_ID, current).putString("error", "").commit()) return;
        NotificationManager manager = context.getSystemService(NotificationManager.class);
        for (StatusBarNotification notice : manager.getActiveNotifications()) {
            String noticeId = notice.getNotification().extras == null ? "" :
                    notice.getNotification().extras.getString(OUTAGE_ID, "");
            if (NotificationType.ACTIVE_DISCONNECT.id.equals(notice.getTag()) &&
                    (current.equals(noticeId) || noticeId.isEmpty())) // Includes pre-1.4 notices.
                manager.cancel(notice.getTag(), notice.getId());
        }
        String reason = intent.getStringExtra("reason");
        String label = "intentional".equals(reason) ? "Intentional power-off" :
                "resolved".equals(reason) ? "Checked and resolved" : "Acknowledged on phone";
        EventLog.append(prefs, "Connection-loss alert acknowledged — " + label + ". " +
                (prefs.getBoolean("monitoring", false) ? "Monitoring continues." : "Automatic monitoring remains paused."));
    }
}
