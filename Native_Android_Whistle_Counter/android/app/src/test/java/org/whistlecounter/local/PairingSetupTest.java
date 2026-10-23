package org.whistlecounter.local;

import java.io.*;
import java.nio.charset.StandardCharsets;
import org.junit.Test;
import static org.junit.Assert.*;

public class PairingSetupTest {
    private final String key = "0123456789abcdef0123456789abcdef0123456789abcdef0123456789abcdef";
    private String payload() { return "whistlecounter://pair?version=1&key=" + key; }
    private void reject(String payload) {
        try { PairingSetup.parse(payload); fail("Invalid setup accepted"); }
        catch (IllegalArgumentException expected) { assertFalse(expected.getMessage().contains(key)); }
    }
    @Test public void qrCanPairWithoutTypingAnIp() {
        PairingSetup setup = PairingSetup.parse(payload() + "&device=99eb61");
        assertEquals(key, setup.key); assertEquals("", setup.host); assertEquals("99eb61", setup.device);
    }
    @Test public void optionalPrivateIpHintIsAccepted() {
        assertEquals("192.168.0.116", PairingSetup.parse(payload() + "&host=192.168.0.116").host);
    }
    @Test public void unsafeHostsAndWrongQrFormatsRejected() {
        reject(payload() + "&host=8.8.8.8"); reject(payload() + "&host=localhost");
        reject("https://example.com/?version=1&key=" + key);
        reject("whistlecounter://pair@evil?version=1&key=" + key);
        reject(payload() + "#extra"); reject(payload() + "&device=wrong");
    }
    @Test public void ambiguousAndUnsupportedPayloadsRejected() {
        reject(payload() + "&key=" + key); reject(payload() + "&unknown=1");
        reject(payload().replace("version=1", "version=2")); reject("whistlecounter://pair?key=" + key);
        reject("whistlecounter://pair?version=1&key=short"); reject(null); reject("whistlecounter://pair");
    }
    @Test public void fileReaderHandlesWhitespaceWithoutLeakingKey() throws Exception {
        ByteArrayInputStream input = new ByteArrayInputStream((payload() + "\n").getBytes(StandardCharsets.UTF_8));
        assertEquals(key, PairingSetup.parse(PairingSetup.read(input)).key);
    }
    @Test public void oversizedFileAndQrRejected() throws Exception {
        byte[] large = new byte[1025];
        try { PairingSetup.read(new ByteArrayInputStream(large)); fail(); }
        catch (IOException expected) { assertEquals("Setup file is too large.", expected.getMessage()); }
        reject(payload() + new String(new char[1025]).replace('\0', 'x'));
    }
}
