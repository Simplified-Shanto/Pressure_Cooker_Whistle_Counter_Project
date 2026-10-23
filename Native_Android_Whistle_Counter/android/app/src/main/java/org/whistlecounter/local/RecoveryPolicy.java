package org.whistlecounter.local;

/** Never override an explicit pause, and never restart a healthy monitor. */
public final class RecoveryPolicy {
    public static final long HEALTHY_FOR_MS = 20_000;
    private RecoveryPolicy() {}

    public static boolean needsRecovery(boolean enabled, boolean configured, long lastHeartbeat, long now) {
        if (!enabled || !configured) return false;
        return lastHeartbeat <= 0 || now < lastHeartbeat || now - lastHeartbeat > HEALTHY_FOR_MS;
    }

    public static boolean mayStartFromBackground(int sdk, boolean exemptEvent, boolean batteryExempt) {
        return sdk < 31 || exemptEvent || batteryExempt;
    }
}
