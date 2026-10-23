package org.whistlecounter.local;

import android.Manifest;
import android.app.*;
import android.app.job.*;
import android.content.*;
import android.content.pm.PackageManager;
import android.os.*;

public final class MonitorRecovery {
    private static final int PERIODIC_JOB = 7401, RECOVERY_JOB = 7402, ATTENTION_ID = 4;
    private MonitorRecovery() {}

    public static boolean configured(SharedPreferences prefs) {
        try {
            Protocol.optionalLocalHost(prefs.getString("host", ""));
            Protocol.key(prefs.getString("pairing_key", ""));
            return true;
        } catch (IllegalArgumentException error) { return false; }
    }

    public static void cancel(Context context) {
        JobScheduler jobs = context.getSystemService(JobScheduler.class);
        jobs.cancel(PERIODIC_JOB); jobs.cancel(RECOVERY_JOB);
        context.getSystemService(NotificationManager.class).cancel(ATTENTION_ID);
        context.getSystemService(NotificationManager.class).cancel(NotificationType.BACKGROUND_HELP.id, ATTENTION_ID);
    }

    public static void schedule(Context context) {
        SharedPreferences prefs = context.getSharedPreferences(CounterService.PREFS, Context.MODE_PRIVATE);
        if (!prefs.getBoolean("monitoring", false) || !configured(prefs)) { cancel(context); return; }
        JobScheduler jobs = context.getSystemService(JobScheduler.class);
        if (jobs.getPendingJob(PERIODIC_JOB) == null) {
            JobInfo job = new JobInfo.Builder(PERIODIC_JOB, new ComponentName(context, RecoveryJob.class))
                    .setPeriodic(15 * 60_000L).setPersisted(true).build();
            jobs.schedule(job);
        }
    }

    public static void scheduleSoon(Context context) {
        schedule(context);
        SharedPreferences prefs = context.getSharedPreferences(CounterService.PREFS, Context.MODE_PRIVATE);
        if (!prefs.getBoolean("monitoring", false) || !configured(prefs)) return;
        // Best-effort job, not an exact alarm. Android may defer it in Doze.
        JobInfo job = new JobInfo.Builder(RECOVERY_JOB, new ComponentName(context, RecoveryJob.class))
                .setMinimumLatency(30_000).setOverrideDeadline(60_000).setPersisted(true).build();
        context.getSystemService(JobScheduler.class).schedule(job);
    }

    public static boolean resume(Context context, String reason, boolean allowedEvent) {
        SharedPreferences prefs = context.getSharedPreferences(CounterService.PREFS, Context.MODE_PRIVATE);
        if (!prefs.getBoolean("monitoring", false) || !configured(prefs)) return false;
        if (Build.VERSION.SDK_INT >= 33 && context.checkSelfPermission(Manifest.permission.POST_NOTIFICATIONS) != PackageManager.PERMISSION_GRANTED) {
            needsAttention(context, "Allow notifications once in the app to enable automatic alerts."); return false;
        }
        boolean batteryExempt = context.getSystemService(PowerManager.class).isIgnoringBatteryOptimizations(context.getPackageName());
        // Boot and package replacement are documented foreground-start exemptions.
        // A recovery job by itself is not; it needs the user's battery exemption.
        if (!RecoveryPolicy.mayStartFromBackground(Build.VERSION.SDK_INT, allowedEvent, batteryExempt)) {
            needsAttention(context, "Set battery use to Unrestricted / Don't optimize so monitoring can recover automatically."); return false;
        }
        try {
            prefs.edit().putString("last_start_reason", reason).apply();
            context.startForegroundService(new Intent(context, CounterService.class).setAction(CounterService.START));
            prefs.edit().putString("recovery_error", "").apply();
            context.getSystemService(NotificationManager.class).cancel(ATTENTION_ID);
            context.getSystemService(NotificationManager.class).cancel(NotificationType.BACKGROUND_HELP.id, ATTENTION_ID);
            return true;
        } catch (RuntimeException error) {
            needsAttention(context, "Android blocked automatic monitoring. Check background access and phone autostart settings.");
            return false;
        }
    }

    private static void needsAttention(Context context, String message) {
        context.getSharedPreferences(CounterService.PREFS, Context.MODE_PRIVATE).edit().putString("recovery_error", message).apply();
        NotificationManager manager = context.getSystemService(NotificationManager.class);
        SharedPreferences prefs = context.getSharedPreferences(CounterService.PREFS, Context.MODE_PRIVATE);
        if (!manager.areNotificationsEnabled() || !NotificationSettings.enabled(prefs, NotificationType.BACKGROUND_HELP)) return;
        NotificationSettings.createChannels(manager);
        PendingIntent open = PendingIntent.getActivity(context, 0, new Intent(context, MainActivity.class), PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        manager.notify(NotificationType.BACKGROUND_HELP.id, ATTENTION_ID, new Notification.Builder(context, NotificationType.BACKGROUND_HELP.channelId()).setSmallIcon(R.drawable.ic_notification)
                .setContentTitle("Enable automatic counter monitoring").setContentText(message)
                .setStyle(new Notification.BigTextStyle().bigText(message)).setContentIntent(open).setAutoCancel(true).setOnlyAlertOnce(true).build());
    }
}
