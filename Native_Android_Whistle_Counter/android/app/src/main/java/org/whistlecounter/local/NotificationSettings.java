package org.whistlecounter.local;

import android.app.*;
import android.content.SharedPreferences;
import android.media.AudioAttributes;
import android.media.RingtoneManager;
import android.service.notification.StatusBarNotification;

public final class NotificationSettings {
    private NotificationSettings() {}

    public static boolean enabled(SharedPreferences prefs, NotificationType type) {
        return prefs.getBoolean(type.preferenceKey(), type.enabledByDefault);
    }

    public static void createChannels(NotificationManager manager) {
        manager.createNotificationChannelGroup(new NotificationChannelGroup("counter_alerts", "Counter alerts"));
        for (NotificationType type : NotificationType.values()) {
            // Preserve Android preferences for old shared channels on upgrade.
            NotificationChannel legacy = manager.getNotificationChannel(
                    type == NotificationType.ACTIVE_DISCONNECT ? "critical" :
                    type == NotificationType.BACKGROUND_HELP ? "background_help" : "counts");
            int importance = type == NotificationType.IDLE_DISCONNECT || type == NotificationType.BACKGROUND_HELP ?
                    NotificationManager.IMPORTANCE_DEFAULT : NotificationManager.IMPORTANCE_HIGH;
            if (legacy != null && type != NotificationType.IDLE_DISCONNECT) importance = legacy.getImportance();
            NotificationChannel channel = new NotificationChannel(type.channelId(), type.label, importance);
            channel.setDescription(type.description); channel.setGroup("counter_alerts");
            if (type == NotificationType.ACTIVE_DISCONNECT) {
                channel.setSound(RingtoneManager.getDefaultUri(RingtoneManager.TYPE_ALARM),
                        new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_ALARM).build());
                channel.enableVibration(true);
            }
            if (legacy != null && type != NotificationType.IDLE_DISCONNECT) {
                channel.setSound(legacy.getSound(), legacy.getAudioAttributes());
                if (legacy.getVibrationPattern() != null) channel.setVibrationPattern(legacy.getVibrationPattern());
                channel.enableVibration(legacy.shouldVibrate());
            }
            manager.createNotificationChannel(channel);
        }
    }

    public static void cancelType(NotificationManager manager, NotificationType type) {
        for (StatusBarNotification notice : manager.getActiveNotifications()) {
            if (type.id.equals(notice.getTag())) manager.cancel(notice.getTag(), notice.getId());
        }
    }
}
