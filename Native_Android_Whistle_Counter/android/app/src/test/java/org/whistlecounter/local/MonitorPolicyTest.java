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

    @Test public void outageStopsCompletionToneAndRaisesOneCriticalTransition() {
        MonitorPolicy p = new MonitorPolicy();
        p.accept("device", "boot1", "alarm", 2, 2, true, 4, 100);
        assertFalse(p.timeout(45_099)); assertTrue(p.alarm);
        assertTrue(p.timeout(45_100)); assertFalse(p.alarm); assertTrue(p.offline);
        assertFalse(p.timeout(80_000));
        MonitorPolicy.Change change = p.accept("device", "boot1", "alarm", 2, 2, true, 4, 81_000);
        assertTrue(change.reconnected); assertTrue(p.alarm);
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
    }
}
