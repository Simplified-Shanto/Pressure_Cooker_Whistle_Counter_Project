package org.whistlecounter.local;

import org.junit.Test;
import static org.junit.Assert.*;

public class MonitorPolicyTest {
    @Test public void completionRequiresPhysicalAcknowledgement() {
        MonitorPolicy p = new MonitorPolicy();
        assertTrue(p.accept("device", "boot1", "listening", 0, 2, false, 1, 100).connected);
        assertFalse(p.canStopMonitoring());
        p.accept("device", "boot1", "alarm", 2, 2, true, 4, 200);
        assertTrue(p.alarm); assertFalse(p.canStopMonitoring());
        // Polling and reopening UI cannot clear an active physical alarm.
        p.accept("device", "boot1", "alarm", 2, 2, true, 4, 300);
        assertTrue(p.alarm);
        p.accept("device", "boot1", "home", 0, 1, false, 5, 400);
        assertFalse(p.alarm); assertTrue(p.canStopMonitoring());
    }

    @Test public void hardwareResetClearsAlarmAndReportsRestart() {
        MonitorPolicy p = new MonitorPolicy();
        p.accept("device", "boot1", "alarm", 2, 2, true, 4, 100);
        MonitorPolicy.Change change = p.accept("device", "boot2", "home", 0, 1, false, 0, 200);
        assertTrue(change.restarted); assertFalse(p.alarm); assertEquals(0, p.cursor);
    }

    @Test public void completedSessionShutdownStopsToneWithoutCriticalAlert() {
        MonitorPolicy p = new MonitorPolicy();
        p.accept("device", "boot1", "alarm", 2, 2, true, 4, 100);
        assertFalse(p.timeout(45_099)); assertTrue(p.alarm);
        assertTrue(p.timeout(45_100)); assertFalse(p.alarm); assertTrue(p.offline);
        assertFalse(p.criticalOffline());
        assertEquals(NotificationType.IDLE_DISCONNECT, p.outageType());
        assertFalse(p.timeout(80_000));
        MonitorPolicy.Change change = p.accept("device", "boot1", "alarm", 2, 2, true, 4, 81_000);
        assertTrue(change.reconnected); assertTrue(p.alarm);
    }

    @Test public void unfinishedSessionOutageRaisesOneCriticalTransition() {
        MonitorPolicy p = new MonitorPolicy();
        p.accept("device", "boot1", "listening", 1, 3, false, 2, 100);
        assertFalse(p.timeout(45_099)); assertFalse(p.criticalOffline());
        assertTrue(p.timeout(45_100)); assertTrue(p.criticalOffline());
        assertEquals(NotificationType.ACTIVE_DISCONNECT, p.outageType());
        assertFalse(p.timeout(90_000));
    }

    @Test public void idleShutdownIsNotCritical() {
        MonitorPolicy p = new MonitorPolicy();
        p.accept("device", "boot1", "home", 0, 1, false, 0, 100);
        assertTrue(p.timeout(45_100)); assertFalse(p.criticalOffline());
    }

    @Test public void physicalAcknowledgementThenShutdownIsNotCritical() {
        MonitorPolicy p = new MonitorPolicy();
        p.accept("device", "boot1", "alarm", 2, 2, true, 4, 100);
        p.accept("device", "boot1", "home", 0, 1, false, 5, 200);
        p.timeout(45_200); assertFalse(p.criticalOffline());
    }

    @Test public void resetAfterCompletionThenShutdownIsNotCritical() {
        MonitorPolicy p = new MonitorPolicy();
        p.accept("device", "boot1", "alarm", 2, 2, true, 4, 100);
        p.accept("device", "boot2", "home", 0, 1, false, 0, 200);
        p.timeout(45_200); assertFalse(p.criticalOffline());
    }

    @Test public void cancellingThenShutdownIsNotCritical() {
        MonitorPolicy p = new MonitorPolicy();
        p.accept("device", "boot1", "listening", 1, 3, false, 2, 100);
        p.accept("device", "boot1", "home", 0, 3, false, 3, 200);
        p.timeout(45_200); assertFalse(p.criticalOffline());
    }

    @Test public void completedSessionDoesNotSuppressNextSessionsOutage() {
        MonitorPolicy p = new MonitorPolicy();
        p.accept("device", "boot1", "alarm", 2, 2, true, 4, 100);
        p.accept("device", "boot1", "home", 0, 1, false, 5, 200);
        p.accept("device", "boot1", "listening", 0, 2, false, 6, 300);
        p.timeout(45_300); assertTrue(p.criticalOffline());
    }

    @Test public void restoredCompletedOutageStaysQuietAndDoesNotRepeat() {
        MonitorPolicy p = new MonitorPolicy();
        // These are the persisted fields restored by CounterService, including
        // an outage originally recorded by app version 1.1 as unconditional.
        p.seen = true; p.offline = true; p.mode = "alarm";
        p.count = 2; p.target = 2; p.lastSuccess = 100;
        assertFalse(p.timeout(90_000)); assertFalse(p.criticalOffline());
        assertEquals(NotificationType.IDLE_DISCONNECT, p.outageType());
    }

    @Test public void restoredUnfinishedOutageRemainsCriticalWithoutNewTransition() {
        MonitorPolicy p = new MonitorPolicy();
        p.seen = true; p.offline = true; p.mode = "listening";
        p.count = 1; p.target = 3; p.lastSuccess = 100;
        assertFalse(p.timeout(90_000)); assertTrue(p.criticalOffline());
    }

    @Test public void reconnectClearsCriticalOutageAndNewLossCanAlertOnce() {
        MonitorPolicy p = new MonitorPolicy();
        p.accept("device", "boot1", "listening", 1, 3, false, 2, 100);
        assertTrue(p.timeout(45_100)); assertTrue(p.criticalOffline());
        assertTrue(p.accept("device", "boot1", "listening", 1, 3, false, 2, 46_000).reconnected);
        assertFalse(p.criticalOffline()); assertTrue(p.timeout(91_000));
        assertTrue(p.criticalOffline()); assertFalse(p.timeout(100_000));
    }

    @Test public void eventPageCursorSurvivesIdenticalSnapshots() {
        MonitorPolicy p = new MonitorPolicy();
        p.accept("device", "boot1", "listening", 1, 2, false, 2, 100);
        p.accept("device", "boot1", "listening", 1, 2, false, 2, 200);
        assertEquals(2, p.cursor);
        p.accept("device", "boot1", "alarm", 2, 2, true, 4, 300);
        assertEquals(4, p.cursor);
    }

    @Test public void staleAcknowledgementCannotClearNewerAlarm() {
        MonitorPolicy p = new MonitorPolicy();
        p.accept("device", "boot1", "alarm", 2, 2, true, 9, 100);
        try { p.accept("device", "boot1", "home", 0, 1, false, 5, 200); fail(); }
        catch (IllegalArgumentException expected) { assertTrue(p.alarm); }
    }

    @Test public void differentDeviceCannotClearAlarm() {
        MonitorPolicy p = new MonitorPolicy();
        p.accept("device", "boot1", "alarm", 2, 2, true, 9, 100);
        try { p.accept("imposter", "boot2", "home", 0, 1, false, 0, 200); fail(); }
        catch (IllegalArgumentException expected) { assertTrue(p.alarm); }
    }

    @Test public void inconsistentStateCannotChangeAlarm() {
        MonitorPolicy p = new MonitorPolicy();
        p.accept("device", "boot1", "alarm", 2, 2, true, 9, 100);
        try { p.accept("device", "boot1", "alarm", 0, 2, false, 10, 200); fail(); }
        catch (IllegalArgumentException expected) { assertTrue(p.alarm); }
    }

    @Test public void firstContactAlsoTimesOutWithoutAnyHeartbeat() {
        MonitorPolicy p = new MonitorPolicy(); p.lastSuccess = 10_000;
        assertFalse(p.timeout(54_999)); assertTrue(p.timeout(55_000)); assertFalse(p.seen);
        assertFalse(p.criticalOffline());
    }
}
