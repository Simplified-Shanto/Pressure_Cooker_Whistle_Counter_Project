package org.whistlecounter.local;

import android.app.Activity;
import android.content.Intent;
import android.graphics.Typeface;
import android.graphics.drawable.GradientDrawable;
import android.os.Build;
import android.view.*;
import android.widget.*;

/** Shared shell: scrolling content and a persistent three-screen navigation bar. */
public abstract class AppScreen extends Activity {
    protected static final int GREEN = 0xff102e28, INK = 0xff183e32, MUTED = 0xff69776f,
            PAPER = 0xfff6f5ef, ACCENT = 0xffbcdaa7;

    protected LinearLayout page(String title, String subtitle, int selected) {
        LinearLayout root = new LinearLayout(this); root.setOrientation(LinearLayout.VERTICAL);
        root.setBackgroundColor(PAPER); setContentView(root);
        getWindow().getDecorView().setSystemUiVisibility(View.SYSTEM_UI_FLAG_LIGHT_STATUS_BAR | View.SYSTEM_UI_FLAG_LIGHT_NAVIGATION_BAR);
        if (Build.VERSION.SDK_INT >= 35) root.setOnApplyWindowInsetsListener((view, insets) -> {
            android.graphics.Insets bars = insets.getInsets(WindowInsets.Type.systemBars() | WindowInsets.Type.displayCutout());
            view.setPadding(bars.left, bars.top, bars.right, bars.bottom); return insets;
        });
        ScrollView scroll = new ScrollView(this); scroll.setFillViewport(true); scroll.setClipToPadding(false);
        root.addView(scroll, new LinearLayout.LayoutParams(-1, 0, 1));
        LinearLayout body = new LinearLayout(this); body.setOrientation(LinearLayout.VERTICAL);
        body.setPadding(dp(24), dp(24), dp(24), dp(24)); scroll.addView(body);
        text(body, title, 28, INK, true); text(body, subtitle, 14, MUTED, false);
        View divider = new View(this); divider.setBackgroundColor(0xffe3e7df); root.addView(divider, new LinearLayout.LayoutParams(-1, dp(1)));
        LinearLayout nav = new LinearLayout(this); nav.setPadding(dp(12), dp(8), dp(12), dp(8));
        nav.setBackgroundColor(0xfffffefa); root.addView(nav);
        Class<?>[] destinations = {MainActivity.class, LogsActivity.class, SettingsActivity.class};
        String[] labels = {"Home", "Logs", "Settings"};
        int[] icons = {R.drawable.ic_home, R.drawable.ic_logs, R.drawable.ic_settings};
        for (int i = 0; i < labels.length; i++) {
            final int index = i;
            TextView tab = new TextView(this); tab.setText(labels[i]); tab.setTextSize(12);
            tab.setGravity(Gravity.CENTER); tab.setTextColor(i == selected ? GREEN : MUTED);
            tab.setTypeface(null, i == selected ? Typeface.BOLD : Typeface.NORMAL);
            android.graphics.drawable.Drawable icon = getDrawable(icons[i]);
            icon.setTint(i == selected ? GREEN : MUTED); icon.setBounds(0, 0, dp(23), dp(23));
            tab.setCompoundDrawables(null, icon, null, null); tab.setCompoundDrawablePadding(dp(3));
            if (i == selected) tab.setBackground(rounded(0xffe9efdf, 16));
            tab.setOnClickListener(view -> {
                if (index == selected) return;
                startActivity(new Intent(this, destinations[index]).addFlags(Intent.FLAG_ACTIVITY_REORDER_TO_FRONT));
                if (!(this instanceof MainActivity)) finish();
            });
            nav.addView(tab, new LinearLayout.LayoutParams(0, dp(56), 1));
        }
        return body;
    }

    protected int dp(int value) { return Math.round(value * getResources().getDisplayMetrics().density); }
    protected GradientDrawable rounded(int color, int radius) {
        GradientDrawable drawable = new GradientDrawable(); drawable.setColor(color); drawable.setCornerRadius(dp(radius)); return drawable;
    }
    protected LinearLayout card(LinearLayout parent, int color) {
        LinearLayout card = new LinearLayout(this); card.setOrientation(LinearLayout.VERTICAL);
        card.setPadding(dp(20), dp(19), dp(20), dp(20)); card.setBackground(rounded(color, 24));
        LinearLayout.LayoutParams params = new LinearLayout.LayoutParams(-1, -2); params.topMargin = dp(18);
        parent.addView(card, params); return card;
    }
    protected TextView text(LinearLayout parent, String value, int size, int color, boolean bold) {
        TextView view = new TextView(this); view.setText(value); view.setTextSize(size); view.setTextColor(color);
        view.setPadding(0, dp(5), 0, dp(7)); view.setLineSpacing(dp(3), 1);
        if (bold) view.setTypeface(null, Typeface.BOLD); parent.addView(view); return view;
    }
    protected Button button(LinearLayout parent, String title, View.OnClickListener click) {
        Button button = new Button(this); button.setText(title); button.setAllCaps(false); button.setTextSize(15);
        button.setTextColor(GREEN); button.setBackgroundTintList(android.content.res.ColorStateList.valueOf(0xffe8eedf));
        button.setOnClickListener(click); parent.addView(button, new LinearLayout.LayoutParams(-1, dp(56))); return button;
    }
}
