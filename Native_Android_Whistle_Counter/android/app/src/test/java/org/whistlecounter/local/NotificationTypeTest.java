package org.whistlecounter.local;

import java.util.HashSet;
import java.util.Set;
import org.junit.Test;
import static org.junit.Assert.*;

public class NotificationTypeTest {
    @Test public void normalShutdownQuietButUnfinishedCountingAlertsByDefault() {
        assertFalse(NotificationType.forOutage(false).enabledByDefault);
        assertTrue(NotificationType.forOutage(true).enabledByDefault);
    }

    @Test public void allSupportedEventsHaveIndependentControls() {
        String[] events = {"whistle", "completed", "session_started", "cancelled", "alarm_acknowledged", "timer_completed"};
        Set<NotificationType> controls = new HashSet<>();
        for (String event : events) assertTrue(controls.add(NotificationType.forEvent(event)));
        assertFalse(controls.contains(null));
        assertEquals(NotificationType.COMPLETION, NotificationType.forEvent("completed"));
        assertEquals(NotificationType.ACKNOWLEDGEMENT, NotificationType.forEvent("alarm_acknowledged"));
    }

    @Test public void unknownEventDoesNotBecomeAnUnrelatedAlert() {
        assertNull(NotificationType.forEvent("unknown"));
    }

    @Test public void categoryPreferencesAndChannelsCannotCollide() {
        Set<String> preferences = new HashSet<>(), channels = new HashSet<>();
        for (NotificationType type : NotificationType.values()) {
            assertTrue(preferences.add(type.preferenceKey()));
            assertTrue(channels.add(type.channelId()));
        }
    }
}
