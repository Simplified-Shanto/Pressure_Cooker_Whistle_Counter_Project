package org.whistlecounter.local;

import java.nio.charset.StandardCharsets;
import java.security.MessageDigest;
import java.security.SecureRandom;
import javax.crypto.Mac;
import javax.crypto.spec.SecretKeySpec;

/** Request-specific authentication; the pairing secret never crosses the LAN. */
public final class Protocol {
    private Protocol() {}

    public static byte[] key(String text) {
        if (!text.matches("[a-fA-F0-9]{64}")) throw new IllegalArgumentException("Pairing key must contain 64 hex characters.");
        byte[] bytes = new byte[32];
        for (int i = 0; i < bytes.length; i++) bytes[i] = (byte) Integer.parseInt(text.substring(i * 2, i * 2 + 2), 16);
        return bytes;
    }

    public static String hex(byte[] bytes) {
        StringBuilder text = new StringBuilder(bytes.length * 2);
        for (byte b : bytes) text.append(String.format(java.util.Locale.ROOT, "%02x", b & 255));
        return text.toString();
    }

    public static String nonce() {
        byte[] bytes = new byte[16]; new SecureRandom().nextBytes(bytes); return hex(bytes);
    }

    public static String sign(byte[] key, String text) throws Exception {
        Mac mac = Mac.getInstance("HmacSHA256");
        mac.init(new SecretKeySpec(key, "HmacSHA256"));
        return hex(mac.doFinal(text.getBytes(StandardCharsets.UTF_8)));
    }

    public static boolean verify(byte[] key, String body, String supplied) throws Exception {
        return supplied != null && MessageDigest.isEqual(sign(key, body).getBytes(StandardCharsets.US_ASCII),
                supplied.getBytes(StandardCharsets.US_ASCII));
    }

    public static String localHost(String text) {
        String value = text.trim();
        if (!value.matches("\\d{1,3}(\\.\\d{1,3}){3}")) throw new IllegalArgumentException("Enter the counter's local IPv4 address, without http:// or a port.");
        String[] parts = value.split("\\.");
        int[] octets = new int[4];
        for (int i = 0; i < 4; i++) {
            octets[i] = Integer.parseInt(parts[i]);
            if (octets[i] > 255 || (parts[i].length() > 1 && parts[i].startsWith("0"))) throw new IllegalArgumentException("Invalid IPv4 address.");
        }
        boolean local = octets[0] == 10 || (octets[0] == 192 && octets[1] == 168) ||
                (octets[0] == 172 && octets[1] >= 16 && octets[1] <= 31);
        if (!local) throw new IllegalArgumentException("Use a private home Wi-Fi address (10.x, 172.16–31.x, or 192.168.x).");
        return value;
    }
}
