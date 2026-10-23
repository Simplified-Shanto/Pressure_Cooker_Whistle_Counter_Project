package org.whistlecounter.local;

import android.app.*;
import android.content.*;
import android.media.*;
import android.net.*;
import android.net.wifi.WifiManager;
import android.os.*;
import org.json.*;
import java.io.*;
import java.net.HttpURLConnection;
import java.nio.charset.StandardCharsets;

public final class CounterService extends Service {
    public static final String PREFS = "counter", START = "monitor", STOP = "stop";
    private static final int MONITOR_ID = 1, ALARM_ID = 2, OFFLINE_ID = 3;
    private static final String MONITOR_CHANNEL = "monitor", EVENT_CHANNEL = "counts", CRITICAL_CHANNEL = "critical";
    private final MonitorPolicy policy = new MonitorPolicy();
    private SharedPreferences prefs;
    private NotificationManager notifications;
    private HandlerThread thread;
    private Handler handler;
    private final Handler watchdog = new Handler(Looper.getMainLooper());
    private volatile boolean running;
    private String host, lastError = "";
    private byte[] pairingKey;
    private PowerManager.WakeLock wakeLock;
    private WifiManager.WifiLock wifiLock;
    private MediaPlayer player;
    private Vibrator vibrator;
    private AudioManager audio;
    private AudioFocusRequest focus;
    private long lastSave, lastWakeRenew, lastServiceHeartbeat;
    private boolean startupFailed;

    @Override public void onCreate() {
        super.onCreate();
        prefs = getSharedPreferences(PREFS, MODE_PRIVATE);
        notifications = getSystemService(NotificationManager.class);
        NotificationChannel monitor = new NotificationChannel(MONITOR_CHANNEL, "Counter monitoring", NotificationManager.IMPORTANCE_LOW);
        monitor.setSound(null, null); monitor.setShowBadge(false);
        notifications.createNotificationChannel(monitor);
        notifications.createNotificationChannel(new NotificationChannel(EVENT_CHANNEL, "Whistle counts and activity", NotificationManager.IMPORTANCE_HIGH));
        NotificationChannel critical = new NotificationChannel(CRITICAL_CHANNEL, "Counter unreachable — critical", NotificationManager.IMPORTANCE_HIGH);
        critical.setDescription("Check the cooker immediately if the loss of connection was unexpected.");
        critical.setSound(RingtoneManager.getDefaultUri(RingtoneManager.TYPE_ALARM),
                new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_ALARM).build());
        critical.enableVibration(true);
        notifications.createNotificationChannel(critical);
        vibrator = getSystemService(Vibrator.class);
        audio = getSystemService(AudioManager.class);
        restore();
    }

    private void restore() {
        policy.boot = prefs.getString("boot", ""); policy.device = prefs.getString("device", "");
        policy.cursor = prefs.getLong("cursor", 0); policy.seen = prefs.getBoolean("seen", false);
        policy.offline = prefs.getBoolean("offline", false); policy.mode = prefs.getString("mode", "home");
        policy.count = prefs.getInt("count", 0); policy.target = prefs.getInt("target", 1);
        long savedAt = prefs.getLong("last_seen_wall", System.currentTimeMillis());
        long age = Math.max(0, System.currentTimeMillis() - savedAt);
        policy.lastSuccess = SystemClock.elapsedRealtime() - Math.min(age, MonitorPolicy.OFFLINE_MS);
        policy.alarm = prefs.getBoolean("alarm", false) && age < MonitorPolicy.OFFLINE_MS;
    }

    @Override public int onStartCommand(Intent intent, int flags, int startId) {
        if (intent != null && STOP.equals(intent.getAction())) {
            synchronized (policy) {
                if (!policy.canStopMonitoring()) {
                    prefs.edit().putString("error", "Use the physical counter to reset or acknowledge the alarm.").apply();
                    return START_STICKY;
                }
                prefs.edit().putBoolean("monitoring", false).apply();
                MonitorRecovery.cancel(this);
                stopSelf(); return START_NOT_STICKY;
            }
        }
        if (running) return START_STICKY;
        if (!prefs.getBoolean("monitoring", false)) { stopSelf(); return START_NOT_STICKY; }
        // Promote immediately, before network, locks, or disk work.
        try {
            startForeground(MONITOR_ID, monitorNotification("Connecting to your counter…"));
            host = Protocol.localHost(prefs.getString("host", ""));
            pairingKey = Protocol.key(prefs.getString("pairing_key", ""));
            wakeLock = getSystemService(PowerManager.class).newWakeLock(PowerManager.PARTIAL_WAKE_LOCK, "WhistleCounter:monitor");
            wakeLock.setReferenceCounted(false);
            wakeLock.acquire(120_000);
            lastWakeRenew = SystemClock.elapsedRealtime();
            WifiManager manager = (WifiManager) getApplicationContext().getSystemService(WIFI_SERVICE);
            wifiLock = manager.createWifiLock(WifiManager.WIFI_MODE_FULL_HIGH_PERF, "WhistleCounter:wifi");
            wifiLock.acquire();
            running = true;
            prefs.edit().putLong("service_heartbeat_wall", System.currentTimeMillis()).putString("recovery_error", "").apply();
            lastServiceHeartbeat = SystemClock.elapsedRealtime();
            MonitorRecovery.schedule(this);
            updateAlarm();
            if (policy.offline) unreachable(false);
            watchdog.post(this::checkHealth);
            thread = new HandlerThread("CounterWifiMonitor"); thread.start();
            handler = new Handler(thread.getLooper()); handler.post(this::poll);
            return START_STICKY;
        } catch (Exception error) {
            // Keep the user's automatic-monitoring preference after a transient
            // start failure. Recovery must not silently become disabled.
            startupFailed = true;
            prefs.edit().putString("error", error.getMessage()).apply();
            MonitorRecovery.schedule(this);
            stopSelf(); return START_NOT_STICKY;
        }
    }

    private Notification.Builder base(String channel, String title, String text) {
        Intent open = new Intent(this, MainActivity.class);
        PendingIntent tap = PendingIntent.getActivity(this, 0, open, PendingIntent.FLAG_IMMUTABLE | PendingIntent.FLAG_UPDATE_CURRENT);
        return new Notification.Builder(this, channel).setSmallIcon(R.drawable.ic_counter)
                .setContentTitle(title).setContentText(text).setStyle(new Notification.BigTextStyle().bigText(text))
                .setContentIntent(tap).setVisibility(Notification.VISIBILITY_PUBLIC);
    }

    private Notification monitorNotification(String text) {
        return base(MONITOR_CHANNEL, "Whistle Counter monitoring", text).setOngoing(true)
                .setOnlyAlertOnce(true).setCategory(Notification.CATEGORY_SERVICE).build();
    }

    private HttpURLConnection connect(String nonce, String boot, long cursor) throws Exception {
        ConnectivityManager manager = getSystemService(ConnectivityManager.class);
        Network wifi = null;
        for (Network network : manager.getAllNetworks()) {
            NetworkCapabilities caps = manager.getNetworkCapabilities(network);
            if (caps != null && caps.hasTransport(NetworkCapabilities.TRANSPORT_WIFI)) { wifi = network; break; }
        }
        if (wifi == null) throw new IOException("Connect this phone to the counter's Wi-Fi network.");
        // Use Wi-Fi even if Android prefers cellular because the router has no internet.
        java.net.URL url = new java.net.URL("http://" + host + "/native/v1/state?nonce=" + nonce + "&boot=" + boot + "&after=" + cursor);
        HttpURLConnection connection = (HttpURLConnection) wifi.openConnection(url);
        connection.setConnectTimeout(2500); connection.setReadTimeout(2500);
        connection.setInstanceFollowRedirects(false); connection.setUseCaches(false);
        connection.setRequestProperty("X-Counter-Request", Protocol.sign(pairingKey, nonce + "\n" + boot + "\n" + cursor));
        return connection;
    }

    private void poll() {
        if (!running) return;
        HttpURLConnection connection = null;
        boolean more = false;
        long requestStarted = SystemClock.elapsedRealtime();
        try {
            if (SystemClock.elapsedRealtime() - lastWakeRenew >= 60_000) {
                wakeLock.acquire(120_000);
                lastWakeRenew = SystemClock.elapsedRealtime();
            }
            String nonce = Protocol.nonce(), requestedBoot;
            long requestedCursor;
            synchronized (policy) { requestedBoot = policy.boot; requestedCursor = policy.cursor; }
            connection = connect(nonce, requestedBoot, requestedCursor);
            int code = connection.getResponseCode();
            if (code != 200) throw new IOException(code == 401 ? "Pairing key does not match this counter." :
                    code == 404 ? "Native firmware is not installed at this IP address." : "Counter HTTP error " + code);
            ByteArrayOutputStream bytes = new ByteArrayOutputStream();
            try (InputStream stream = connection.getInputStream()) {
                byte[] buffer = new byte[2048]; int read;
                while ((read = stream.read(buffer)) != -1) {
                    if (SystemClock.elapsedRealtime() - requestStarted > 8000) throw new IOException("Counter response took too long.");
                    if (bytes.size() + read > 32768) throw new IOException("Counter response exceeded the limit.");
                    bytes.write(buffer, 0, read);
                }
            }
            String body = bytes.toString(StandardCharsets.UTF_8.name());
            if (SystemClock.elapsedRealtime() - requestStarted > 8000) throw new IOException("Counter response is no longer fresh.");
            if (!Protocol.verify(pairingKey, body, connection.getHeaderField("X-Counter-Mac")))
                throw new IOException("Counter response authentication failed; no alarm state was changed.");
            JSONObject snapshot = new JSONObject(body);
            if (snapshot.getInt("protocol") != 1 || !nonce.equals(snapshot.getString("nonce")))
                throw new IOException("Stale or unsupported counter response.");
            String boot = snapshot.getString("boot_id"), device = snapshot.getString("device_id");
            if (!boot.matches("[a-f0-9]{22}") || !device.matches("[a-f0-9]{6}")) throw new IOException("Invalid counter identity.");
            JSONArray events = snapshot.getJSONArray("events");
            long next = snapshot.getLong("next_seq"), latest = snapshot.getLong("latest_seq");
            if (next < 0 || next > latest || events.length() > 16) throw new IOException("Invalid counter event page.");
            String mode = snapshot.getString("mode");
            if (!java.util.Arrays.asList("home", "listening", "alarm", "settings", "stopwatch", "timer").contains(mode))
                throw new IOException("Invalid counter mode.");
            synchronized (policy) {
                if (!running) return;
                boolean sameBoot = policy.boot.equals(boot), oldAlarm = policy.alarm;
                long previous = sameBoot ? policy.cursor : 0;
                // Validate every event before changing the alarm or event cursor.
                long last = previous;
                for (int i = 0; i < events.length(); i++) {
                    JSONObject event = events.getJSONObject(i);
                    long seq = event.getLong("seq");
                    int value = event.getInt("count"), goal = event.getInt("target");
                    if (seq <= last || seq > next || value < 0 || goal < 1 || goal > 999 || value > goal)
                        throw new IOException("Invalid counter event sequence.");
                    last = seq;
                }
                MonitorPolicy.Change change = policy.accept(device, boot, mode, snapshot.getInt("count"),
                        snapshot.getInt("target"), snapshot.getBoolean("alarm_active"), next, SystemClock.elapsedRealtime());
                if (change.connected) activity("Counter got connected", "Local Wi-Fi monitoring is active.", 10);
                if (change.restarted) activity("Counter restarted", "Counter rebooted. Reason: " + snapshot.getString("reset_reason"), 11);
                else if (change.reconnected) activity("Counter reconnected", "Wi-Fi communication has returned.", 12);
                notifications.cancel(OFFLINE_ID);
                if (snapshot.getBoolean("gap")) activity("Some earlier events are unavailable", "The dashboard shows the current counter state.", 13);
                for (int i = 0; i < events.length(); i++) {
                    JSONObject event = events.getJSONObject(i);
                    showEvent(boot, event);
                }
                lastError = snapshot.getBoolean("storage_ok") ? "" : "Counter storage is unavailable; only recent events can be recovered.";
                updateAlarm();
                long now = SystemClock.elapsedRealtime();
                if (events.length() > 0 || change.connected || change.restarted || change.reconnected ||
                        oldAlarm != policy.alarm || now - lastSave >= 5000) save();
                more = snapshot.getBoolean("has_more");
            }
        } catch (Exception error) {
            if (running) synchronized (policy) { lastError = error.getMessage(); }
        } finally {
            if (connection != null) connection.disconnect();
        }
        if (!running) return;
        synchronized (policy) {
            String text = policy.offline ? "CRITICAL: counter unreachable" : policy.alarm ?
                    "ALARM — reset or acknowledge on the physical counter" :
                    policy.seen ? policy.count + " of " + policy.target + " whistles · " + policy.mode : "Connecting to " + host;
            notifications.notify(MONITOR_ID, monitorNotification(text));
            prefs.edit().putString("error", lastError).apply();
        }
        if (running) handler.postDelayed(this::poll, more ? 100 : 1000);
    }

    private void checkHealth() {
        if (!running) return;
        if (SystemClock.elapsedRealtime() - lastServiceHeartbeat >= 5000) {
            prefs.edit().putLong("service_heartbeat_wall", System.currentTimeMillis()).apply();
            lastServiceHeartbeat = SystemClock.elapsedRealtime();
        }
        synchronized (policy) {
            // Independent from HTTP: a slow or stuck response cannot prevent
            // the missing-heartbeat alarm transition from being evaluated.
            if (policy.timeout(SystemClock.elapsedRealtime())) {
                updateAlarm(); unreachable(true); save();
            }
        }
        if (running) watchdog.postDelayed(this::checkHealth, 1000);
    }

    private void showEvent(String boot, JSONObject event) throws JSONException {
        String type = event.getString("type"), title, body;
        int value = event.getInt("count"), goal = event.getInt("target");
        switch (type) {
            case "whistle": title = "Whistle detected"; body = "Total: " + value + " of " + goal + " whistles."; break;
            case "completed": title = "Whistle target reached"; body = "Completed: " + value + " of " + goal + ". Reset or acknowledge on the counter."; break;
            case "session_started": title = "Listening started"; body = "Target: " + goal + " whistles."; break;
            case "cancelled": title = "Listening cancelled"; body = "Session stopped at " + value + " of " + goal + "."; break;
            case "alarm_acknowledged": title = "Alarm acknowledged on counter"; body = "Acknowledgement recorded on the physical counter."; break;
            case "timer_completed": title = "Timer finished"; body = "The counter timer reached zero."; break;
            default: return;
        }
        int id = 1000 + ((boot + ":" + event.getLong("seq")).hashCode() & 0x3fffffff);
        activity(title, body, id);
    }

    private void activity(String title, String body, int id) {
        notifications.notify(id, base(EVENT_CHANNEL, title, body).setAutoCancel(true).setOnlyAlertOnce(true).build());
        history(title + " — " + body);
    }

    private void unreachable(boolean recordHistory) {
        String message = "CRITICAL: Counter powered off or unreachable. Check the cooker immediately if this was not intentional.";
        notifications.notify(OFFLINE_ID, base(CRITICAL_CHANNEL, "Counter powered off or unreachable", message)
                .setOngoing(true).setOnlyAlertOnce(true).setCategory(Notification.CATEGORY_ALARM).build());
        if (recordHistory) history(message);
    }

    private void history(String line) {
        String old = prefs.getString("history", "");
        String value = new java.text.SimpleDateFormat("HH:mm:ss", java.util.Locale.getDefault()).format(new java.util.Date()) + "  " + line + "\n\n" + old;
        if (value.length() > 12000) value = value.substring(0, 12000);
        prefs.edit().putString("history", value).apply();
    }

    @android.annotation.SuppressLint("ApplySharedPref")
    private void save() {
        // Persist the event cursor on this background thread before requesting
        // another page, so a process restart does not replay delivered alerts.
        prefs.edit().putString("boot", policy.boot).putString("device", policy.device).putLong("cursor", policy.cursor)
                .putBoolean("seen", policy.seen).putBoolean("offline", policy.offline).putBoolean("alarm", policy.alarm)
                .putString("mode", policy.mode).putInt("count", policy.count).putInt("target", policy.target)
                .putLong("last_seen_wall", System.currentTimeMillis() - Math.max(0, SystemClock.elapsedRealtime() - policy.lastSuccess))
                .putString("error", lastError).commit();
        lastSave = SystemClock.elapsedRealtime();
    }

    private void updateAlarm() {
        if (!policy.alarm) {
            if (player != null) { player.release(); player = null; }
            if (vibrator != null) vibrator.cancel();
            if (focus != null) { audio.abandonAudioFocusRequest(focus); focus = null; }
            notifications.cancel(ALARM_ID); return;
        }
        notifications.notify(ALARM_ID, base(MONITOR_CHANNEL, "Whistle target reached — alarm active",
                "Reset or acknowledge on the physical counter. There is no phone silence control.")
                .setOngoing(true).setCategory(Notification.CATEGORY_ALARM).build());
        if (player == null) {
            try {
                AudioAttributes attributes = new AudioAttributes.Builder().setUsage(AudioAttributes.USAGE_ALARM)
                        .setContentType(AudioAttributes.CONTENT_TYPE_SONIFICATION).build();
                focus = new AudioFocusRequest.Builder(AudioManager.AUDIOFOCUS_GAIN_TRANSIENT)
                        .setAudioAttributes(attributes).setOnAudioFocusChangeListener(change -> {}).build();
                audio.requestAudioFocus(focus);
                player = AlarmAudio.prepare(this, attributes);
                player.start();
            } catch (Exception error) {
                if (player != null) { player.release(); player = null; }
                lastError = "Alarm audio failed: " + error.getMessage();
            }
            if (vibrator != null && vibrator.hasVibrator()) vibrator.vibrate(VibrationEffect.createWaveform(new long[]{0, 600, 600}, 0));
        }
    }

    @Override public void onDestroy() {
        running = false;
        watchdog.removeCallbacksAndMessages(null);
        if (handler != null) handler.removeCallbacksAndMessages(null);
        if (thread != null) thread.quitSafely();
        // OS termination must release resources. It is not a supported silence action.
        synchronized (policy) {
            if (player != null) { player.release(); player = null; }
            if (vibrator != null) vibrator.cancel();
            if (focus != null) audio.abandonAudioFocusRequest(focus);
            if (wifiLock != null && wifiLock.isHeld()) wifiLock.release();
            if (wakeLock != null && wakeLock.isHeld()) wakeLock.release();
        }
        stopForeground(STOP_FOREGROUND_REMOVE);
        prefs.edit().putLong("service_heartbeat_wall", 0).apply();
        if (prefs.getBoolean("monitoring", false) && !startupFailed) MonitorRecovery.scheduleSoon(this);
        super.onDestroy();
    }

    @Override public void onTaskRemoved(Intent rootIntent) {
        // Removing the screen from Recents is not a request to pause monitoring.
        MonitorRecovery.scheduleSoon(this);
        super.onTaskRemoved(rootIntent);
    }

    @Override public IBinder onBind(Intent intent) { return null; }
}
