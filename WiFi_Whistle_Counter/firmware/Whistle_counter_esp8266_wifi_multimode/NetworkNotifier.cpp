#include "NetworkNotifier.h"
#include "HttpResponse.h"
#include "LocalStatusPage.h"

#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <WiFiClientSecureBearSSL.h>
#include <LittleFS.h>
#include <time.h>
#include <memory>
extern "C" {
#include <user_interface.h>
}

#if !__has_include("WiFiSecrets.h") || !__has_include("ServerConfig.h")
#error "Create WiFiSecrets.h and run webapp/setup.py --board esp8266; see README.md."
#endif
#include "WiFiSecrets.h"
#include "ServerConfig.h"

namespace {
BearSSL::Session tlsSession;
BearSSL::WiFiClientSecure client;
std::unique_ptr<BearSSL::X509List> trust;
HttpResponse response;
ESP8266WebServer localServer(80);
uint8_t lcdAddress = 0;
uint32_t lastBackendAck = 0, lastWiFiLog = 0;
char bootId[25], sessionId[64] = "none", mode[16] = "home";
const char *resetReason = "unknown";
uint16_t count = 0, target = 1, port = 443;
uint32_t generation = 0, sequence = 0, sessionSequence = 0, stateUptime = 0;
uint32_t lastReconnect = 0, lastHeartbeat = 0, nextAttempt = 0, sentAt = 0, retryDelay = 1000;
bool fsReady = false, storageFault = false, transportFault = false, waiting = false, wasConnected = false;
String host, queuedPath;
Dir scan;
String scanCandidate;
bool scanning = false;
constexpr uint32_t BOOT_MAGIC = 0x57434653;
struct BootRecord { uint32_t magic, generation, checksum; };

String quoted(const String &value) {
  String result = "\"";
  for (size_t i = 0; i < value.length(); ++i) {
    unsigned char c = value[i];
    if (c == '"' || c == '\\') { result += '\\'; result += static_cast<char>(c); }
    else if (c < 0x20) {
      char escape[7];
      snprintf(escape, sizeof(escape), "\\u%04x", c);
      result += escape;
    } else result += static_cast<char>(c);
  }
  result += '"';
  return result;
}

void serveStatus() {
  // Explicit field list: never return Wi-Fi passwords, device tokens, or keys.
  String json;
  json.reserve(650);
  json += F("{\"board\":\"Generic ESP8266 Module\",\"wifi_connected\":");
  json += WiFi.status() == WL_CONNECTED ? F("true") : F("false");
  json += F(",\"ssid\":"); json += quoted(WiFi.SSID());
  json += F(",\"ip\":"); json += quoted(WiFi.localIP().toString());
  json += F(",\"rssi_dbm\":"); json += WiFi.RSSI();
  json += F(",\"mode\":"); json += quoted(String(mode));
  json += F(",\"count\":"); json += count;
  json += F(",\"target\":"); json += target;
  json += F(",\"uptime_ms\":"); json += millis();
  json += F(",\"free_heap_bytes\":"); json += ESP.getFreeHeap();
  json += F(",\"https_address_set\":"); json += !host.isEmpty() ? F("true") : F("false");
  json += F(",\"backend_reachable\":");
  json += lastBackendAck && millis() - lastBackendAck < 45000 ? F("true") : F("false");
  json += F(",\"outbox_ready\":"); json += fsReady ? F("true") : F("false");
  json += F(",\"storage_error\":"); json += storageFault ? F("true") : F("false");
  json += F(",\"lcd_address\":"); json += lcdAddress;
  json += '}';
  localServer.sendHeader(F("Cache-Control"), F("no-store"));
  localServer.send(200, F("application/json; charset=utf-8"), json);
}

const char *reasonText() {
  switch (ESP.getResetInfoPtr()->reason) {
    case REASON_DEFAULT_RST: return "power-on";
    case REASON_WDT_RST: return "hardware watchdog";
    case REASON_EXCEPTION_RST: return "exception";
    case REASON_SOFT_WDT_RST: return "software watchdog";
    case REASON_SOFT_RESTART: return "software reset";
    case REASON_DEEP_SLEEP_AWAKE: return "deep-sleep wake";
    case REASON_EXT_SYS_RST: return "external reset";
    default: return "unknown";
  }
}

bool advanceBoot() {
  BootRecord record{BOOT_MAGIC, 0, BOOT_MAGIC};
  if (LittleFS.exists("/boot.bin")) {
    File file = LittleFS.open("/boot.bin", "r");
    if (!file || file.size() != sizeof(record) ||
        file.read(reinterpret_cast<uint8_t *>(&record), sizeof(record)) != sizeof(record) ||
        record.magic != BOOT_MAGIC || record.checksum != (record.magic ^ record.generation)) return false;
  }
  generation = record.generation + 1;
  if (!generation) return false;
  record.generation = generation;
  record.checksum = record.magic ^ record.generation;
  File file = LittleFS.open("/boot.tmp", "w");
  bool saved = file && file.write(reinterpret_cast<const uint8_t *>(&record), sizeof(record)) == sizeof(record);
  if (file) { file.flush(); file.close(); }
  return saved && LittleFS.rename("/boot.tmp", "/boot.bin");
}

bool findOldestFile(String &oldest) {
  if (!scanning) {
    scan = LittleFS.openDir("/q");
    scanCandidate = String();
    scanning = true;
  }
  uint32_t started = micros();
  // Large offline backlogs must not turn a directory scan into a long pause
  // in microphone sampling. Continue the scan on subsequent loop iterations.
  for (unsigned i = 0; i < 4 && micros() - started < 1000; ++i) {
    if (!scan.next()) {
      oldest = scanCandidate;
      scan = Dir();
      scanning = false;
      return true;
    }
    String name = scan.fileName();
    if (!name.startsWith("/")) name = String("/q/") + name;
    if (name.endsWith(".json") && (scanCandidate.isEmpty() || name < scanCandidate)) scanCandidate = name;
  }
  return false;
}

void failed() {
  waiting = false;
  transportFault = true;
  client.stop();
  nextAttempt = millis() + retryDelay;
  retryDelay = retryDelay < 15000 ? retryDelay * 2 : 30000;
}

bool startRequest(const char *path, const String &body) {
  response.reset();
  String request;
  request.reserve(body.length() + host.length() + 260);
  request += F("POST "); request += path; request += F(" HTTP/1.1\r\nHost: "); request += host;
  if (port != 443) { request += ':'; request += port; }
  request += F("\r\nAuthorization: Bearer "); request += COUNTER_DEVICE_TOKEN;
  request += F("\r\nContent-Type: application/json\r\nConnection: keep-alive\r\nContent-Length: ");
  request += body.length(); request += F("\r\n\r\n"); request += body;
  // A short bounded write on an established connection; never handshakes.
  // If TCP is congested, retain the event rather than waiting seconds here.
  client.setTimeout(20);
  if (client.write(reinterpret_cast<const uint8_t *>(request.c_str()), request.length()) != request.length()) {
    failed(); return false;
  }
  waiting = true;
  sentAt = millis();
  return true;
}

void pollResponse() {
  // Read only available bytes. Yield control back to detector/button processing
  // instead of waiting for response headers or a body on this single-core MCU.
  uint32_t started = micros();
  while (!response.complete() && !response.failed() && client.available() > 0 && micros() - started < 1500) {
    int value = client.read();
    if (value >= 0) response.consume(static_cast<char>(value));
  }
  if (!response.complete() && !response.failed() && !client.connected() && !client.available()) response.disconnected();
  if (response.failed() || millis() - sentAt > 5000) {
    Serial.println(F("Notification response failed or timed out; event retained."));
    failed();
  } else if (response.complete()) {
    waiting = false;
    if (response.statusCode() != 200) {
      Serial.printf("Notification server returned HTTP %d; event retained.\n", response.statusCode());
      failed(); return;
    }
    if (!queuedPath.isEmpty() && !LittleFS.remove(queuedPath)) {
      storageFault = true;
      Serial.println(F("Outbox removal failed; server deduplicates retries."));
    }
    queuedPath = String();
    transportFault = false;
    lastBackendAck = millis();
    retryDelay = 1000;
    nextAttempt = millis() + 20;
    if (response.closeAfter()) client.stop();
  }
}
}

namespace NetworkNotifier {
void begin() {
  snprintf(bootId, sizeof(bootId), "%06lx%08lx%08lx", (unsigned long)ESP.getChipId(),
           (unsigned long)os_random(), (unsigned long)os_random());
  resetReason = reasonText();
  LittleFSConfig fsConfig;
  fsConfig.setAutoFormat(false);
  LittleFS.setConfig(fsConfig);
  fsReady = LittleFS.begin();
  if (fsReady && !LittleFS.exists("/q")) fsReady = LittleFS.mkdir("/q");
  if (fsReady) fsReady = advanceBoot();
  storageFault = !fsReady;
  if (!fsReady) Serial.println(F("Outbox unavailable. Initialize LittleFS with the ESP8266 tool sketch."));

  String origin(COUNTER_SERVER);
  while (origin.endsWith("/")) origin.remove(origin.length() - 1);
  if (origin.startsWith("https://")) {
    host = origin.substring(8);
    int colon = host.lastIndexOf(':');
    if (colon >= 0) {
      long parsed = host.substring(colon + 1).toInt();
      if (parsed < 1 || parsed > 65535) host = String();
      else { port = static_cast<uint16_t>(parsed); host.remove(colon); }
    }
    if (host.indexOf('/') >= 0 || host.indexOf('@') >= 0) host = String();
  }
  if (host.isEmpty()) {
    transportFault = true;
    Serial.println(F("Set COUNTER_SERVER to your HTTPS origin; localhost development is not a device endpoint."));
  }
  client.setTimeout(1500);
  client.setSession(&tlsSession);
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.hostname("whistle-counter");
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  Serial.println(F("Joining Wi-Fi. Local status URL will be printed after connection."));
  localServer.on("/", HTTP_GET, []() {
    localServer.sendHeader(F("Cache-Control"), F("no-store"));
    localServer.send_P(200, PSTR("text/html; charset=utf-8"), LOCAL_STATUS_PAGE);
  });
  localServer.on("/status", HTTP_GET, serveStatus);
  localServer.onNotFound([]() { localServer.send(404, F("text/plain"), F("Not found")); });
  localServer.begin();
  configTime(0, 0, "pool.ntp.org", "time.google.com");
  lastReconnect = millis();
  lastHeartbeat = millis() - 10000;
}

void update() {
  bool connected = WiFi.status() == WL_CONNECTED;
  if (!connected) {
    if (wasConnected || waiting) { client.stop(); waiting = false; }
    wasConnected = false;
    if (millis() - lastWiFiLog >= 5000) {
      Serial.printf("Waiting for Wi-Fi connection (status %d).\n", static_cast<int>(WiFi.status()));
      lastWiFiLog = millis();
    }
    if (millis() - lastReconnect >= 10000) { WiFi.reconnect(); lastReconnect = millis(); }
    return;
  }
  if (!wasConnected) {
    wasConnected = true;
    Serial.print(F("Wi-Fi connected. IP: ")); Serial.println(WiFi.localIP());
    Serial.print(F("Open local Wi-Fi status page: http://")); Serial.print(WiFi.localIP()); Serial.println('/');
    lastHeartbeat = millis() - 10000;
    nextAttempt = millis();
  }
  // Serve local diagnostics even when there is no configured HTTPS backend.
  localServer.handleClient();
  if (waiting) { pollResponse(); return; }
  if (static_cast<int32_t>(millis() - nextAttempt) < 0 || host.isEmpty()) return;
  if (time(nullptr) < 1700000000) { transportFault = true; return; }
  if (!client.connected()) {
    // TLS may consume multiple seconds of CPU. Local sampling takes priority;
    // queued events are transmitted once listening completes or is cancelled.
    if (!strcmp(mode, "listening")) { transportFault = true; return; }
    if (!trust) {
      trust.reset(new BearSSL::X509List(COUNTER_ROOT_CA));
      client.setTrustAnchors(trust.get());
    }
    client.setTimeout(3000);
    if (!client.connect(host.c_str(), port)) {
      Serial.println(F("TLS connection failed. Check HTTPS origin, root CA, time, and free heap."));
      failed(); return;
    }
    client.setTimeout(20);
    Serial.println(F("HTTPS ready; startup notification follows server acknowledgement."));
    lastHeartbeat = millis() - 10000;
  }
  if (millis() - lastHeartbeat >= 10000) {
    char body[320];
    snprintf(body, sizeof(body),
             "{\"boot_id\":\"%s\",\"mode\":\"%s\",\"count\":%u,\"target\":%u,\"reset_reason\":\"%s\",\"uptime_ms\":%lu}",
             bootId, mode, count, target, resetReason, (unsigned long)stateUptime);
    queuedPath = String();
    lastHeartbeat = millis();
    startRequest("/api/device/heartbeat", body);
  } else if (fsReady) {
    if (!findOldestFile(queuedPath)) return;
    if (!queuedPath.isEmpty()) {
      File file = LittleFS.open(queuedPath, "r");
      String body = file ? file.readString() : String();
      if (file) file.close();
      if (body.isEmpty()) { storageFault = true; failed(); return; }
      startRequest("/api/device/events", body);
    } else nextAttempt = millis() + 100;
  }
}

void setState(const char *currentMode, uint16_t currentCount, uint16_t currentTarget) {
  snprintf(mode, sizeof(mode), "%s", currentMode);
  count = currentCount; target = currentTarget; stateUptime = millis();
}

void startSession() {
  snprintf(sessionId, sizeof(sessionId), "%s:s%lu", bootId, (unsigned long)++sessionSequence);
}

void setDisplayAddress(uint8_t address) { lcdAddress = address; }

bool publish(const char *type, uint16_t currentCount, uint16_t currentTarget) {
  if (!fsReady) return false;
  uint32_t seq = ++sequence;
  char path[40], temp[40], body[480], timestamp[24] = "null";
  snprintf(path, sizeof(path), "/q/%08lx%08lx.json", (unsigned long)generation, (unsigned long)seq);
  snprintf(temp, sizeof(temp), "/q/%08lx%08lx.tmp", (unsigned long)generation, (unsigned long)seq);
  time_t occurred = time(nullptr);
  if (occurred >= 1700000000) snprintf(timestamp, sizeof(timestamp), "%lld", (long long)occurred);
  snprintf(body, sizeof(body),
      "{\"event_id\":\"%s:%lu\",\"boot_id\":\"%s\",\"session_id\":\"%s\",\"type\":\"%s\","
      "\"count\":%u,\"target\":%u,\"uptime_ms\":%lu,\"occurred_at\":%s}",
      bootId, (unsigned long)seq, bootId, sessionId, type, currentCount, currentTarget,
      (unsigned long)millis(), timestamp);
  File file = LittleFS.open(temp, "w");
  bool saved = file && file.print(body) == strlen(body);
  if (file) { file.flush(); file.close(); }
  if (saved) saved = LittleFS.rename(temp, path);
  if (!saved) {
    LittleFS.remove(temp); storageFault = true;
    Serial.println(F("Outbox full or write failed. Local counting continues."));
  }
  return saved;
}

bool deliveryFault() { return storageFault || transportFault; }
bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }
}
