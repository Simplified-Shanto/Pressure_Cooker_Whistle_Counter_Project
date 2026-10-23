package org.whistlecounter.local;

import java.io.*;
import java.net.URI;
import java.net.URLDecoder;
import java.nio.charset.StandardCharsets;
import java.util.*;

/** Strict local QR/file format. Never includes untrusted payloads in errors/logs. */
public final class PairingSetup {
    public final String host, key, device;
    private PairingSetup(String host, String key, String device) { this.host = host; this.key = key; this.device = device; }

    public static String read(InputStream input) throws IOException {
        if (input == null) throw new IOException("No setup file selected.");
        ByteArrayOutputStream bytes = new ByteArrayOutputStream(); byte[] buffer = new byte[256]; int size;
        while ((size = input.read(buffer)) != -1) {
            if (bytes.size() + size > 1024) throw new IOException("Setup file is too large.");
            bytes.write(buffer, 0, size);
        }
        return new String(bytes.toByteArray(), StandardCharsets.UTF_8).trim();
    }

    public static PairingSetup parse(String text) {
        try {
            if (text == null || text.length() > 1024) throw new IllegalArgumentException();
            URI uri = new URI(text.trim());
            if (!"whistlecounter".equals(uri.getScheme()) || !"pair".equals(uri.getHost()) ||
                    uri.getPort() != -1 || uri.getUserInfo() != null || uri.getFragment() != null ||
                    (uri.getPath() != null && !uri.getPath().isEmpty())) throw new IllegalArgumentException();
            Map<String, String> fields = new HashMap<>();
            for (String part : uri.getRawQuery().split("&")) {
                String[] pair = part.split("=", -1);
                if (pair.length != 2) throw new IllegalArgumentException();
                String name = URLDecoder.decode(pair[0], "UTF-8"), value = URLDecoder.decode(pair[1], "UTF-8");
                if (!Arrays.asList("version", "key", "device", "host").contains(name) || fields.put(name, value) != null)
                    throw new IllegalArgumentException();
            }
            if (!"1".equals(fields.get("version"))) throw new IllegalArgumentException();
            String key = fields.getOrDefault("key", "").toLowerCase(Locale.ROOT); Protocol.key(key);
            String host = Protocol.optionalLocalHost(fields.getOrDefault("host", ""));
            String device = fields.getOrDefault("device", "").toLowerCase(Locale.ROOT);
            if (!device.isEmpty() && !device.matches("[a-f0-9]{6}")) throw new IllegalArgumentException();
            return new PairingSetup(host, key, device);
        } catch (Exception invalid) { throw new IllegalArgumentException("Invalid Whistle Counter setup code."); }
    }
}
