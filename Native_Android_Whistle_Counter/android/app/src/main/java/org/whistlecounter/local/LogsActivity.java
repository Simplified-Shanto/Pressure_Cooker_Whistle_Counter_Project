package org.whistlecounter.local;

import android.content.SharedPreferences;
import android.os.*;
import android.widget.LinearLayout;

public final class LogsActivity extends AppScreen {
    private SharedPreferences prefs;
    private LinearLayout entries;
    private String shown;
    private final Handler ui = new Handler(Looper.getMainLooper());
    private final Runnable refresh = new Runnable() {
        @Override public void run() { render(); ui.postDelayed(this, 1000); }
    };
    @Override public void onCreate(Bundle state) {
        super.onCreate(state); prefs = getSharedPreferences(CounterService.PREFS, MODE_PRIVATE);
        LinearLayout body = page("Activity logs", "Your counter's recent activity, newest first.", 1);
        entries = new LinearLayout(this); entries.setOrientation(LinearLayout.VERTICAL); body.addView(entries);
    }
    @Override public void onResume() { super.onResume(); ui.post(refresh); }
    @Override public void onPause() { ui.removeCallbacks(refresh); super.onPause(); }
    private void render() {
        String history = prefs.getString("history", ""); if (history.equals(shown)) return; shown = history;
        entries.removeAllViews();
        if (history.trim().isEmpty()) {
            LinearLayout empty = card(entries, 0xfffffefa);
            text(empty, "Nothing here yet", 21, INK, true);
            text(empty, "Connections, counts and completed sessions will appear here. Muted notifications are still recorded.", 15, MUTED, false);
        } else for (String entry : history.split("\n\n")) {
            if (entry.trim().isEmpty()) continue;
            LinearLayout row = card(entries, 0xfffffefa);
            int split = entry.indexOf("  ");
            if (split >= 0) { text(row, entry.substring(0, split), 12, MUTED, false); entry = entry.substring(split + 2); }
            text(row, entry, 15, INK, false);
        }
    }
}
