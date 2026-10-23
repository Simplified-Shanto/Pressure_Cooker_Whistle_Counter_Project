package org.whistlecounter.local;

import android.content.*;

public final class BootReceiver extends BroadcastReceiver {
    @Override public void onReceive(Context context, Intent intent) {
        String action = intent.getAction();
        if (!Intent.ACTION_BOOT_COMPLETED.equals(action) && !Intent.ACTION_MY_PACKAGE_REPLACED.equals(action)) return;
        MonitorRecovery.schedule(context);
        MonitorRecovery.resume(context, Intent.ACTION_BOOT_COMPLETED.equals(action) ? "phone reboot" : "app updated", true);
    }
}
