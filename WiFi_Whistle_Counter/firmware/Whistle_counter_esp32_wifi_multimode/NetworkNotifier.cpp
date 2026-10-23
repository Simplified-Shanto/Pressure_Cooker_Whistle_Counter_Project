#include "NetworkNotifier.h"

#include <HTTPClient.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <WiFi.h>
#include <WiFiClientSecure.h>
#include <esp_system.h>
#include <time.h>
#include <atomic>

#if !__has_include("WiFiSecrets.h") || !__has_include("ServerConfig.h")
#error "Run webapp/setup.py and create WiFiSecrets.h first; see the Wi-Fi variant README."
#endif
#include "ServerConfig.h"
#include "WiFiSecrets.h"

namespace {
SemaphoreHandle_t fsMutex;
portMUX_TYPE stateMux = portMUX_INITIALIZER_UNLOCKED;
struct Snapshot {
  char mode[16] = "home";
  uint16_t count = 0;
  uint16_t target = 1;
  uint32_t uptimeMs = 0;
};
Snapshot state;
char bootId[25];
char sessionId[64] = "none";
const char *resetReason = "unknown";
uint32_t generation = 0, sequence = 0, sessionSequence = 0;
bool fsReady = false;
std::atomic<bool> storageFault{false}, transportFault{false};

const char *reasonText() {
  switch (esp_reset_reason()) {
    case ESP_RST_POWERON: return "power-on";
    case ESP_RST_EXT: return "external reset";
    case ESP_RST_SW: return "software reset";
    case ESP_RST_PANIC: return "software panic";
    case ESP_RST_INT_WDT: return "interrupt watchdog";
    case ESP_RST_TASK_WDT: return "task watchdog";
    case ESP_RST_WDT: return "watchdog";
    case ESP_RST_DEEPSLEEP: return "deep-sleep wake";
    case ESP_RST_BROWNOUT: return "brownout";
    default: return "unknown";
  }
}

Snapshot snapshot() {
  portENTER_CRITICAL(&stateMux);
  Snapshot copy = state;
  portEXIT_CRITICAL(&stateMux);
  return copy;
}

bool post(const char *path, const String &body) {
  WiFiClientSecure client;
  client.setCACert(COUNTER_ROOT_CA);
  client.setHandshakeTimeout(5);
  HTTPClient http;
  http.setConnectTimeout(3000);
  http.setTimeout(3000);
  if (!http.begin(client, String(COUNTER_SERVER) + path)) return false;
  http.addHeader("Content-Type", "application/json");
  http.addHeader("Authorization", String("Bearer ") + COUNTER_DEVICE_TOKEN);
  int code = http.POST(body);
  http.end();
  if (code != 200) Serial.printf("Notification server request failed: HTTP %d\n", code);
  return code == 200;
}

String oldestFile() {
  String oldest;
  xSemaphoreTake(fsMutex, portMAX_DELAY);
  File dir = LittleFS.open("/q");
  if (dir) {
    for (File file = dir.openNextFile(); file; file = dir.openNextFile()) {
      String name = file.name();
      if (!name.startsWith("/")) name = String("/q/") + name;
      if (name.endsWith(".json") && (oldest.isEmpty() || name < oldest)) oldest = name;
      file.close();
    }
    dir.close();
  }
  xSemaphoreGive(fsMutex);
  return oldest;
}

void networkTask(void *) {
  WiFi.mode(WIFI_STA);
  WiFi.setAutoReconnect(true);
  WiFi.persistent(false);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  uint32_t lastReconnect = millis(), lastHeartbeat = 0, nextRetry = 0;
  uint32_t retryDelay = 1000;
  bool wasConnected = false;
  for (;;) {
    bool connected = WiFi.status() == WL_CONNECTED;
    if (!connected) {
      wasConnected = false;
      if (millis() - lastReconnect >= 10000) {
        WiFi.reconnect();
        lastReconnect = millis();
      }
      vTaskDelay(pdMS_TO_TICKS(100));
      continue;
    }
    if (!wasConnected) {
      wasConnected = true;
      Serial.print("Wi-Fi connected. IP: ");
      Serial.println(WiFi.localIP());
      lastHeartbeat = millis() - 10000;
      nextRetry = millis();
    }
    // Certificate validity checks require network time. Never disable TLS checks.
    if (time(nullptr) < 1700000000) {
      transportFault = true;
      vTaskDelay(pdMS_TO_TICKS(250));
      continue;
    }
    if (millis() - lastHeartbeat >= 10000) {
      Snapshot s = snapshot();
      char heartbeat[320];
      snprintf(heartbeat, sizeof(heartbeat),
               "{\"boot_id\":\"%s\",\"mode\":\"%s\",\"count\":%u,\"target\":%u,\"reset_reason\":\"%s\",\"uptime_ms\":%lu}",
               bootId, s.mode, s.count, s.target, resetReason, (unsigned long)s.uptimeMs);
      transportFault = !post("/api/device/heartbeat", heartbeat);
      lastHeartbeat = millis();
    }
    if (fsReady && static_cast<int32_t>(millis() - nextRetry) >= 0) {
      String path = oldestFile();
      if (!path.isEmpty()) {
        xSemaphoreTake(fsMutex, portMAX_DELAY);
        File file = LittleFS.open(path, "r");
        String body = file ? file.readString() : String();
        if (file) file.close();
        xSemaphoreGive(fsMutex);
        bool success = !body.isEmpty() && post("/api/device/events", body);
        if (success) {
          xSemaphoreTake(fsMutex, portMAX_DELAY);
          bool removed = LittleFS.remove(path);
          xSemaphoreGive(fsMutex);
          if (!removed) Serial.println("Outbox removal failed; server will deduplicate retries.");
          retryDelay = 1000;
        } else {
          retryDelay = retryDelay < 15000 ? retryDelay * 2 : 30000;
        }
        transportFault = !success;
        nextRetry = millis() + (success ? 20 : retryDelay);
      }
    }
    vTaskDelay(pdMS_TO_TICKS(20));
  }
}
}

namespace NetworkNotifier {
void begin() {
  fsMutex = xSemaphoreCreateMutex();
  snprintf(bootId, sizeof(bootId), "%08lx%08lx", (unsigned long)esp_random(), (unsigned long)esp_random());
  resetReason = reasonText();
  Preferences prefs;
  if (prefs.begin("counter-net", false)) {
    generation = prefs.getUInt("boot", 0) + 1;
    if (!prefs.putUInt("boot", generation)) storageFault = true;
    prefs.end();
  } else {
    storageFault = true;
  }
  // Do not silently format a corrupt outbox. Initial setup uses the explicit
  // FormatOutbox sketch described in README; upload firmware without erasing FS.
  fsReady = fsMutex && !storageFault && LittleFS.begin(false);
  if (fsReady) {
    if (!LittleFS.exists("/q")) fsReady = LittleFS.mkdir("/q");
  }
  storageFault = !fsReady;
  if (storageFault) Serial.println("Outbox unavailable. See README: initialize LittleFS before use.");
  if (xTaskCreate(networkTask, "counter-network", 12288, nullptr, 1, nullptr) != pdPASS) {
    transportFault = true;
    Serial.println("Could not start network task.");
  }
}

void setState(const char *mode, uint16_t count, uint16_t target) {
  portENTER_CRITICAL(&stateMux);
  snprintf(state.mode, sizeof(state.mode), "%s", mode);
  state.count = count;
  state.target = target;
  state.uptimeMs = millis();
  portEXIT_CRITICAL(&stateMux);
}

void startSession() {
  snprintf(sessionId, sizeof(sessionId), "%s:s%lu", bootId, (unsigned long)++sessionSequence);
}

bool publish(const char *type, uint16_t count, uint16_t target) {
  if (!fsReady) return false;
  uint32_t seq = ++sequence;
  char path[40], temp[40], body[480];
  snprintf(path, sizeof(path), "/q/%08lx%08lx.json", (unsigned long)generation, (unsigned long)seq);
  snprintf(temp, sizeof(temp), "/q/%08lx%08lx.tmp", (unsigned long)generation, (unsigned long)seq);
  time_t occurred = time(nullptr);
  // Timestamps remain null until NTP is available; uptime still identifies order.
  char timestamp[24] = "null";
  if (occurred >= 1700000000) snprintf(timestamp, sizeof(timestamp), "%lld", (long long)occurred);
  snprintf(body, sizeof(body),
      "{\"event_id\":\"%s:%lu\",\"boot_id\":\"%s\",\"session_id\":\"%s\",\"type\":\"%s\","
      "\"count\":%u,\"target\":%u,\"uptime_ms\":%lu,\"occurred_at\":%s}",
      bootId, (unsigned long)seq, bootId, sessionId, type, count, target, (unsigned long)millis(), timestamp);
  xSemaphoreTake(fsMutex, portMAX_DELAY);
  File file = LittleFS.open(temp, "w");
  bool saved = file && file.print(body) == strlen(body);
  if (file) { file.flush(); file.close(); }
  if (saved) saved = LittleFS.rename(temp, path);
  if (!saved) LittleFS.remove(temp);
  xSemaphoreGive(fsMutex);
  if (!saved) {
    storageFault = true;
    Serial.println("Event could not be saved: outbox full or storage error. Local counting continues.");
  }
  return saved;
}

bool deliveryFault() { return storageFault || transportFault; }
bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }
}
