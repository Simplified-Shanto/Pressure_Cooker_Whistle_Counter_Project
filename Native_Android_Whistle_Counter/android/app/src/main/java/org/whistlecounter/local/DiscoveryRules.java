package org.whistlecounter.local;

/** Discovery is only an address hint; HMAC and device identity still authorize it. */
public final class DiscoveryRules {
    private DiscoveryRules() {}
    public static boolean matches(String serviceName, String serviceType, String expectedDevice) {
        if (serviceName == null || (!"_whistle._tcp".equals(serviceType) && !"_whistle._tcp.".equals(serviceType))) return false;
        String prefix = "WhistleCounter-";
        if (!serviceName.startsWith(prefix)) return false;
        String device = serviceName.substring(prefix.length());
        return device.matches("[a-f0-9]{6}") && (expectedDevice.isEmpty() || expectedDevice.equals(device));
    }
}
