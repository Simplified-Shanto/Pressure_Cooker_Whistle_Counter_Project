package org.whistlecounter.local;

/** Android-independent rules. Only authenticated snapshots may call accept(). */
public final class MonitorPolicy {
    public static final long OFFLINE_MS = 45_000;
    public String boot = "", device = "", mode = "home";
    public long cursor, lastSuccess;
    public boolean seen, offline, alarm;
    public int count, target = 1;

    public static final class Change {
        public boolean connected, reconnected, restarted;
    }

    public Change accept(String deviceId, String bootId, String newMode, int value, int goal,
                         boolean alarmActive, long nextSequence, long now) {
        if (!device.isEmpty() && !device.equals(deviceId)) throw new IllegalArgumentException("A different counter answered. Stop monitoring while idle and pair again.");
        if (value < 0 || goal < 1 || goal > 999 || value > goal || nextSequence < 0 ||
                alarmActive != newMode.equals("alarm") || (alarmActive && value != goal))
            throw new IllegalArgumentException("Counter sent an inconsistent state.");
        boolean sameBoot = boot.equals(bootId);
        if (sameBoot && nextSequence < cursor) throw new IllegalArgumentException("Counter cursor went backwards.");
        Change change = new Change();
        change.connected = !seen;
        change.reconnected = seen && offline;
        change.restarted = seen && !sameBoot;
        device = deviceId; boot = bootId; cursor = nextSequence; mode = newMode;
        count = value; target = goal; seen = true; offline = false;
        // Physical acknowledgement or hardware reset clears completion sound.
        // Reconnection to a still-active physical alarm starts it again.
        alarm = alarmActive; lastSuccess = now;
        return change;
    }

    public boolean timeout(long now) {
        if (!offline && now - lastSuccess >= OFFLINE_MS) {
            offline = true; alarm = false;
            return true;
        }
        return false;
    }

    public boolean canStopMonitoring() {
        return !alarm && (offline || !mode.equals("listening"));
    }
}
