package org.whistlecounter.local;

import android.content.SharedPreferences;

/** Serialize updates from the polling service and notification action receiver. */
public final class EventLog {
    private EventLog() {}
    public static synchronized void append(SharedPreferences prefs, String line) {
        String old = prefs.getString("history", "");
        String value = new java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.getDefault())
                .format(new java.util.Date()) + "  " + line + "\n\n" + old;
        if (value.length() > 12000) value = value.substring(0, 12000);
        prefs.edit().putString("history", value).apply();
    }
}
