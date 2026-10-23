package org.whistlecounter.local;

/** Stable preference/channel identifiers; independent of Android for unit tests. */
public enum NotificationType {
    CONNECTION("connection", "Connection and reconnection", "When the counter connects or communication returns.", true),
    RESTART("restart", "Counter restarted", "When the physical counter reboots.", true),
    WHISTLE("whistle", "Each whistle count", "The total count after each detected whistle.", true),
    COMPLETION("completion", "Target reached notification", "The completion banner. The continuous alarm is controlled separately by the physical counter.", true),
    SESSION_STARTED("session_started", "Listening started", "When a new counting session begins.", true),
    SESSION_CANCELLED("session_cancelled", "Listening cancelled", "When counting is cancelled on the counter.", true),
    ACKNOWLEDGEMENT("acknowledgement", "Physical alarm acknowledged", "When a counter button acknowledges completion.", true),
    TIMER_COMPLETED("timer_completed", "Timer finished", "When the counter's timer reaches zero.", true),
    ACTIVE_DISCONNECT("active_disconnect", "Connection lost during counting", "Critical alert after 45 seconds without contact during an unfinished whistle session.", true),
    IDLE_DISCONNECT("idle_disconnect", "Offline while idle or finished", "Optional ordinary notification after shutdown outside an unfinished counting session. Off by default.", false),
    MISSING_EVENTS("missing_events", "Missing event history", "When some older events could not be recovered.", true),
    BACKGROUND_HELP("background_help", "Monitoring needs attention", "When Android prevents automatic background monitoring.", true);

    public final String id, label, description;
    public final boolean enabledByDefault;

    NotificationType(String id, String label, String description, boolean enabledByDefault) {
        this.id = id; this.label = label; this.description = description;
        this.enabledByDefault = enabledByDefault;
    }

    public String preferenceKey() { return "notify_" + id; }
    public String channelId() { return "counter_" + id; }

    public static NotificationType forEvent(String event) {
        switch (event) {
            case "whistle": return WHISTLE;
            case "completed": return COMPLETION;
            case "session_started": return SESSION_STARTED;
            case "cancelled": return SESSION_CANCELLED;
            case "alarm_acknowledged": return ACKNOWLEDGEMENT;
            case "timer_completed": return TIMER_COMPLETED;
            default: return null;
        }
    }

    public static NotificationType forOutage(boolean unfinishedCounting) {
        return unfinishedCounting ? ACTIVE_DISCONNECT : IDLE_DISCONNECT;
    }
}
