package org.whistlecounter.local;

import org.junit.Test;
import static org.junit.Assert.*;

public class RecoveryPolicyTest {
    @Test public void explicitlyPausedMonitoringNeverRestarts() {
        assertFalse(RecoveryPolicy.needsRecovery(false, true, 0, 100_000));
    }
    @Test public void missingPairingDoesNotLaunchService() {
        assertFalse(RecoveryPolicy.needsRecovery(true, false, 0, 100_000));
    }
    @Test public void healthyMonitorIsNotRestarted() {
        assertFalse(RecoveryPolicy.needsRecovery(true, true, 95_000, 100_000));
    }
    @Test public void interruptedOrOldMonitorNeedsRecovery() {
        assertTrue(RecoveryPolicy.needsRecovery(true, true, 0, 100_000));
        assertTrue(RecoveryPolicy.needsRecovery(true, true, 70_000, 100_000));
        assertTrue(RecoveryPolicy.needsRecovery(true, true, 110_000, 100_000));
    }
    @Test public void backgroundJobsRespectAndroidStartRestrictions() {
        assertTrue(RecoveryPolicy.mayStartFromBackground(30, false, false));
        assertFalse(RecoveryPolicy.mayStartFromBackground(31, false, false));
        assertFalse(RecoveryPolicy.mayStartFromBackground(35, false, false));
        assertTrue(RecoveryPolicy.mayStartFromBackground(35, true, false));
        assertTrue(RecoveryPolicy.mayStartFromBackground(35, false, true));
    }
}
