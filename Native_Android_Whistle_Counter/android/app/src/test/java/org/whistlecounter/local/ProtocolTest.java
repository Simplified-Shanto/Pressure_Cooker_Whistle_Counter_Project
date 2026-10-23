package org.whistlecounter.local;

import org.junit.Test;
import static org.junit.Assert.*;

public class ProtocolTest {
    @Test public void hmacMatchesRfc4231Vector() throws Exception {
        byte[] key = new byte[20]; java.util.Arrays.fill(key, (byte)0x0b);
        assertEquals("b0344c61d8db38535ca8afceaf0bf12b881dc200c9833da726e9376c2e32cff7", Protocol.sign(key, "Hi There"));
    }
    @Test public void tamperedAlarmStateAndWrongKeyAreRejected() throws Exception {
        byte[] key = Protocol.key("aa".repeat(32));
        String body = "{\"alarm_active\":true}", signature = Protocol.sign(key, body);
        assertTrue(Protocol.verify(key, body, signature));
        assertFalse(Protocol.verify(key, body.replace("true", "false"), signature));
        assertFalse(Protocol.verify(Protocol.key("bb".repeat(32)), body, signature));
        assertFalse(Protocol.verify(key, body, null));
    }
    @Test public void requestAuthenticationBindsCursorAndNonce() throws Exception {
        byte[] key = Protocol.key("aa".repeat(32));
        assertNotEquals(Protocol.sign(key, "nonce1\nboot1\n4"), Protocol.sign(key, "nonce2\nboot1\n4"));
        assertNotEquals(Protocol.sign(key, "nonce1\nboot1\n4"), Protocol.sign(key, "nonce1\nboot1\n5"));
    }
    @Test public void onlyPrivateIpv4HostsAreAccepted() {
        for (String host : new String[]{"192.168.0.116", "10.0.0.2", "172.16.0.1", "172.31.255.254"})
            assertEquals(host, Protocol.localHost(host));
        for (String host : new String[]{"8.8.8.8", "172.32.0.1", "192.168.1.999", "192.168.01.1", "http://192.168.1.1", "192.168.1.1:80", "example.com"}) {
            try { Protocol.localHost(host); fail(host); } catch (IllegalArgumentException expected) { /* expected */ }
        }
    }
}
