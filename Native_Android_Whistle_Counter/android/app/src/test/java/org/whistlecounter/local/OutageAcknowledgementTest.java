package org.whistlecounter.local;

import org.junit.Test;
import static org.junit.Assert.*;

public class OutageAcknowledgementTest {
    private MonitorPolicy outage() {
        MonitorPolicy policy = new MonitorPolicy();
        policy.accept("device", "boot1", "listening", 1, 3, false, 2, 100);
        assertTrue(policy.timeout(45_100));
        return policy;
    }

    @Test public void acknowledgementQuietsCurrentOutageWithoutStoppingMonitoringState() {
        MonitorPolicy policy = outage(); String id = policy.outageId;
        assertTrue(policy.criticalOffline()); assertTrue(policy.acknowledgeOutage(id));
        assertFalse(policy.criticalOffline()); assertTrue(policy.offline); assertTrue(policy.outageAcknowledged);
        assertEquals("device", policy.device); assertEquals("boot1", policy.boot);
        assertEquals(2, policy.cursor); assertEquals(1, policy.count); assertEquals(3, policy.target);
        assertEquals("listening", policy.mode);
    }

    @Test public void acknowledgedOutageNeverRetriggersWithoutReconnection() {
        MonitorPolicy policy = outage(); policy.acknowledgeOutage(policy.outageId);
        assertFalse(policy.timeout(90_000)); assertFalse(policy.timeout(9_000_000));
        assertFalse(policy.criticalOffline());
    }

    @Test public void repeatedAcknowledgementIsIdempotent() {
        MonitorPolicy policy = outage(); String id = policy.outageId;
        assertTrue(policy.acknowledgeOutage(id)); assertFalse(policy.acknowledgeOutage(id));
        assertTrue(policy.outageAcknowledged);
    }

    @Test public void persistedTokenRestoresQuietAfterProcessRestart() {
        MonitorPolicy before = outage(); String persisted = before.outageId;
        before.acknowledgeOutage(persisted);
        MonitorPolicy restored = new MonitorPolicy();
        restored.offline = true; restored.seen = true; restored.mode = "listening";
        restored.count = 1; restored.target = 3; restored.outageId = persisted;
        restored.ensureOutageId();
        assertEquals(persisted, restored.outageId);
        assertTrue(restored.acknowledgeOutage(persisted));
        assertFalse(restored.criticalOffline()); assertFalse(restored.timeout(500_000));
    }

    @Test public void recoveredThenLostCounterGetsNewAlertAndOldActionCannotDismissIt() {
        MonitorPolicy policy = outage(); String previous = policy.outageId;
        policy.acknowledgeOutage(previous);
        assertTrue(policy.accept("device", "boot1", "listening", 1, 3, false, 2, 46_000).reconnected);
        assertEquals("", policy.outageId); assertFalse(policy.outageAcknowledged);
        assertTrue(policy.timeout(91_000)); assertTrue(policy.criticalOffline());
        assertNotEquals(previous, policy.outageId);
        assertFalse(policy.acknowledgeOutage(previous)); assertTrue(policy.criticalOffline());
    }

    @Test public void oldNotificationCannotAcknowledgeAnOnlineCounter() {
        MonitorPolicy policy = outage(); String previous = policy.outageId;
        policy.accept("device", "boot1", "listening", 1, 3, false, 2, 46_000);
        assertFalse(policy.acknowledgeOutage(previous)); assertFalse(policy.offline);
    }

    @Test public void outageActionCannotSilenceCompletionAlarm() {
        MonitorPolicy policy = outage(); String previous = policy.outageId;
        policy.accept("device", "boot1", "alarm", 3, 3, true, 4, 46_000);
        assertFalse(policy.acknowledgeOutage(previous)); assertTrue(policy.alarm);
        assertFalse(policy.canStopMonitoring());
        // Also reject an inconsistent persisted offline+active-alarm state.
        policy.offline = true; policy.mode = "listening"; policy.count = 1; policy.outageId = previous;
        assertFalse(policy.acknowledgeOutage(previous)); assertTrue(policy.alarm);
    }

    @Test public void startupIdleAndCompletedOutagesHaveNoCriticalAcknowledgement() {
        assertFalse(MonitorPolicy.canAcknowledgeOutage(true, false, "listening", 0, 3, false, "id", "id"));
        assertFalse(MonitorPolicy.canAcknowledgeOutage(true, true, "home", 0, 3, false, "id", "id"));
        assertFalse(MonitorPolicy.canAcknowledgeOutage(true, true, "alarm", 3, 3, false, "id", "id"));
        assertFalse(MonitorPolicy.canAcknowledgeOutage(true, true, "listening", 1, 3, false, "", ""));
        assertFalse(MonitorPolicy.canAcknowledgeOutage(true, true, "listening", 1, 3, false, "id", null));
    }

    @Test public void oldVersionOutageGetsOneStableIdentityForNewAction() {
        MonitorPolicy policy = new MonitorPolicy();
        policy.offline = true; policy.seen = true; policy.mode = "listening";
        policy.count = 1; policy.target = 3;
        policy.ensureOutageId(); String id = policy.outageId;
        assertFalse(id.isEmpty()); policy.ensureOutageId(); assertEquals(id, policy.outageId);
        assertTrue(policy.acknowledgeOutage(id)); assertFalse(policy.criticalOffline());
    }
}
