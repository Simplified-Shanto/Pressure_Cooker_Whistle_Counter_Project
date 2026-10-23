package org.whistlecounter.local;

import android.app.job.*;
import android.content.SharedPreferences;

public final class RecoveryJob extends JobService {
    @Override public boolean onStartJob(JobParameters params) {
        SharedPreferences prefs = getSharedPreferences(CounterService.PREFS, MODE_PRIVATE);
        if (RecoveryPolicy.needsRecovery(prefs.getBoolean("monitoring", false), MonitorRecovery.configured(prefs),
                prefs.getLong("service_heartbeat_wall", 0), System.currentTimeMillis()))
            MonitorRecovery.resume(this, "automatic recovery", false);
        return false;
    }
    @Override public boolean onStopJob(JobParameters params) { return true; }
}
