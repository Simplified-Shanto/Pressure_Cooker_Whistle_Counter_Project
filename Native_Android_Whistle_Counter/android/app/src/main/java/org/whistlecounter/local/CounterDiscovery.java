package org.whistlecounter.local;

import android.content.Context;
import android.net.nsd.*;
import android.net.wifi.WifiManager;
import android.os.*;
import java.net.Inet4Address;
import java.util.ArrayDeque;

/** Bounded Wi-Fi DNS-SD bursts; no subnet scanning or continuous multicast lock. */
public final class CounterDiscovery {
    public interface Listener { void onAddress(String host); void onDone(); }
    private final NsdManager manager;
    private final WifiManager wifi;
    private final Handler main = new Handler(Looper.getMainLooper());
    private final ArrayDeque<NsdServiceInfo> pending = new ArrayDeque<>();
    private NsdManager.DiscoveryListener discovery;
    private WifiManager.MulticastLock lock;
    private boolean resolving;
    private int generation;

    public CounterDiscovery(Context context) {
        manager = context.getSystemService(NsdManager.class);
        wifi = (WifiManager) context.getApplicationContext().getSystemService(Context.WIFI_SERVICE);
    }

    public void start(String expectedDevice, Listener listener) {
        stop(); final int token = generation;
        try {
            lock = wifi.createMulticastLock("WhistleCounter:discovery"); lock.setReferenceCounted(false); lock.acquire();
            discovery = new NsdManager.DiscoveryListener() {
                @Override public void onDiscoveryStarted(String type) {}
                @Override public void onDiscoveryStopped(String type) {}
                @Override public void onServiceLost(NsdServiceInfo info) {}
                @Override public void onServiceFound(NsdServiceInfo info) {
                    main.post(() -> {
                        if (token != generation || !DiscoveryRules.matches(info.getServiceName(), info.getServiceType(), expectedDevice)) return;
                        if (pending.size() < 16) pending.add(info);
                        resolveNext(token, listener);
                    });
                }
                @Override public void onStartDiscoveryFailed(String type, int code) { main.post(() -> finish(token, listener)); }
                @Override public void onStopDiscoveryFailed(String type, int code) {}
            };
            manager.discoverServices("_whistle._tcp.", NsdManager.PROTOCOL_DNS_SD, discovery);
            main.postDelayed(() -> finish(token, listener), 15_000);
        } catch (RuntimeException unavailable) { finish(token, listener); }
    }

    private void resolveNext(int token, Listener listener) {
        if (token != generation || resolving || pending.isEmpty()) return;
        resolving = true;
        try {
            manager.resolveService(pending.removeFirst(), new NsdManager.ResolveListener() {
                @Override public void onResolveFailed(NsdServiceInfo info, int code) { done(null); }
                @Override public void onServiceResolved(NsdServiceInfo info) { done(info); }
                private void done(NsdServiceInfo info) {
                    main.post(() -> {
                        if (token != generation) return;
                        resolving = false;
                        if (info != null && info.getPort() == 80 && info.getHost() instanceof Inet4Address) {
                            try { listener.onAddress(Protocol.localHost(info.getHost().getHostAddress())); }
                            catch (IllegalArgumentException ignored) { /* Reject public/non-IPv4 hints. */ }
                        }
                        resolveNext(token, listener);
                    });
                }
            });
        } catch (RuntimeException unavailable) { resolving = false; resolveNext(token, listener); }
    }

    private void finish(int token, Listener listener) {
        if (token != generation) return;
        stop(); listener.onDone();
    }

    public void stop() {
        ++generation; pending.clear(); resolving = false;
        if (discovery != null) {
            try { manager.stopServiceDiscovery(discovery); } catch (RuntimeException ignored) {}
            discovery = null;
        }
        if (lock != null && lock.isHeld()) lock.release(); lock = null;
    }
}
