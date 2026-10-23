package org.whistlecounter.local;

import org.junit.Test;
import static org.junit.Assert.*;

public class DiscoveryRulesTest {
    @Test public void onlyPairedCounterIsSelectedWhenIdentityKnown() {
        assertTrue(DiscoveryRules.matches("WhistleCounter-99eb61", "_whistle._tcp.", "99eb61"));
        assertFalse(DiscoveryRules.matches("WhistleCounter-123456", "_whistle._tcp.", "99eb61"));
    }
    @Test public void initialPairingMayFindAnyWellFormedCounterHint() {
        assertTrue(DiscoveryRules.matches("WhistleCounter-99eb61", "_whistle._tcp", ""));
    }
    @Test public void unrelatedAndMalformedServicesAreRejected() {
        assertFalse(DiscoveryRules.matches("Printer", "_whistle._tcp.", ""));
        assertFalse(DiscoveryRules.matches("WhistleCounter-99eb61", "_http._tcp.", ""));
        assertFalse(DiscoveryRules.matches("WhistleCounter-99eb61", "_whistle._tcp.evil", ""));
        assertFalse(DiscoveryRules.matches("WhistleCounter-xxxxxx", "_whistle._tcp.", ""));
        assertFalse(DiscoveryRules.matches(null, "_whistle._tcp.", ""));
    }
    @Test public void emptyManualHostEnablesDiscoveryButPublicAddressDoesNot() {
        assertEquals("", Protocol.optionalLocalHost(" "));
        assertEquals("192.168.0.116", Protocol.optionalLocalHost("192.168.0.116"));
        try { Protocol.optionalLocalHost("8.8.8.8"); fail(); } catch (IllegalArgumentException expected) {}
    }
}
