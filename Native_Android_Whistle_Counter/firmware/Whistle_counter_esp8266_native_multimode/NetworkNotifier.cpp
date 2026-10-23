#include "NetworkNotifier.h"
#include <ESP8266WiFi.h>
#include <ESP8266WebServer.h>
#include <ESP8266mDNS.h>
#include <LittleFS.h>
#include <bearssl/bearssl.h>
extern "C" {
#include <user_interface.h>
}
#if !__has_include("WiFiSecrets.h") || !__has_include("NativeSecrets.h")
#error "Create WiFiSecrets.h and run tools/prepare_device.py for NativeSecrets.h."
#endif
#include "WiFiSecrets.h"
#include "NativeSecrets.h"

namespace {
ESP8266WebServer server(80);
char bootId[25], deviceId[9], currentMode[16] = "home";
uint16_t count = 0, target = 1;
DetectorSettings detectorSettings;
uint8_t lcdAddress = 0, key[32];
uint32_t sequence = 0, alarmId = 0, acknowledgedAlarm = 0, lastReconnect = 0;
bool fsReady = false, storageFault = false, keyReady = false, connectedBefore = false;
bool mdnsReady = false;
String recent[16];
constexpr uint32_t RETAIN = 128, PAGE = 16;

const char *resetReason() {
  switch (ESP.getResetInfoPtr()->reason) {
    case REASON_DEFAULT_RST: return "power-on";
    case REASON_EXT_SYS_RST: return "hardware reset";
    case REASON_SOFT_RESTART: return "software restart";
    case REASON_EXCEPTION_RST: return "exception";
    case REASON_WDT_RST: return "hardware watchdog";
    case REASON_SOFT_WDT_RST: return "software watchdog";
    default: return "other reset";
  }
}

bool hexString(const String &value, size_t length) {
  if (value.length() != length) return false;
  for (size_t i = 0; i < length; ++i)
    if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f'))) return false;
  return true;
}

String mac(const String &message) {
  br_hmac_key_context kc;
  br_hmac_context context;
  uint8_t digest[32];
  br_hmac_key_init(&kc, &br_sha256_vtable, key, sizeof(key));
  br_hmac_init(&context, &kc, sizeof(digest));
  br_hmac_update(&context, message.c_str(), message.length());
  br_hmac_out(&context, digest);
  char hex[65];
  for (unsigned i = 0; i < sizeof(digest); ++i) snprintf(hex + i * 2, 3, "%02x", digest[i]);
  return String(hex);
}

String eventPath(uint32_t seq) {
  char path[32];
  snprintf(path, sizeof(path), "/native-q/%08lx.json", (unsigned long)seq);
  return String(path);
}

String eventAt(uint32_t seq) {
  if (sequence - seq < 16) return recent[seq % 16];
  if (!fsReady) return String();
  File file = LittleFS.open(eventPath(seq), "r");
  return file ? file.readString() : String();
}

void serveNative() {
  String nonce = server.arg("nonce"), knownBoot = server.arg("boot"), afterText = server.arg("after");
  if (!keyReady || !hexString(nonce, 32) || (!knownBoot.isEmpty() && !hexString(knownBoot, 22)) ||
      afterText.isEmpty() || afterText.length() > 10) {
    server.send(400, "text/plain", "Invalid native request"); return;
  }
  for (unsigned i = 0; i < afterText.length(); ++i) if (!isDigit(afterText[i])) {
    server.send(400, "text/plain", "Invalid cursor"); return;
  }
  String expected = mac(nonce + '\n' + knownBoot + '\n' + afterText);
  String supplied = server.header("X-Counter-Request");
  uint8_t different = supplied.length() == expected.length() ? 0 : 1;
  for (unsigned i = 0; i < expected.length(); ++i)
    different |= expected[i] ^ (i < supplied.length() ? supplied[i] : 0);
  if (different) { server.send(401, "text/plain", "Pairing key mismatch"); return; }

  uint32_t after = knownBoot == bootId ? strtoul(afterText.c_str(), nullptr, 10) : 0;
  if (after > sequence) after = 0;
  uint32_t oldest = sequence > RETAIN ? sequence - RETAIN + 1 : 1;
  bool gap = after + 1 < oldest;
  uint32_t start = after + 1 < oldest ? oldest : after + 1;
  uint32_t end = start + PAGE - 1 < sequence ? start + PAGE - 1 : sequence;
  char prefix[700];
  snprintf(prefix, sizeof(prefix),
      "{\"protocol\":1,\"nonce\":\"%s\",\"device_id\":\"%s\",\"boot_id\":\"%s\","
      "\"reset_reason\":\"%s\",\"uptime_ms\":%lu,\"mode\":\"%s\",\"count\":%u,\"target\":%u,"
      "\"alarm_active\":%s,\"alarm_id\":%lu,\"acknowledged_alarm\":%lu,\"storage_ok\":%s,"
      "\"latest_seq\":%lu,\"next_seq\":%lu,\"has_more\":%s,\"events\":[",
      nonce.c_str(), deviceId, bootId, resetReason(), (unsigned long)millis(), currentMode, count, target,
      !strcmp(currentMode, "alarm") ? "true" : "false", (unsigned long)alarmId,
      (unsigned long)acknowledgedAlarm, !storageFault ? "true" : "false", (unsigned long)sequence,
      (unsigned long)end, end < sequence ? "true" : "false");
  String body(prefix);
  body.reserve(4096);
  bool comma = false;
  for (uint32_t seq = start; seq <= end; ++seq) {
    String event = eventAt(seq);
    if (event.isEmpty()) { gap = true; continue; }
    if (comma) body += ',';
    body += event; comma = true;
  }
  body += F("],\"gap\":"); body += gap ? F("true}") : F("false}");
  server.sendHeader("Cache-Control", "no-store");
  server.sendHeader("X-Counter-Mac", mac(body));
  server.send(200, "application/json", body);
}

void serveStatus() {
  char json[650];
  snprintf(json, sizeof(json),
      "{\"board\":\"Generic ESP8266 Module\",\"variant\":\"native-android\",\"wifi_connected\":%s,"
      "\"mode\":\"%s\",\"count\":%u,\"target\":%u,\"alarm_active\":%s,\"outbox_ready\":%s,"
      "\"storage_error\":%s,\"lcd_address\":%u,\"lcd_configured_address\":%u,\"free_heap_bytes\":%u,"
      "\"detector\":{\"threshold\":%u,\"minimum_sound_ms\":%u,\"ending_quiet_ms\":%u,\"cooldown_ms\":%u},"
      "\"discovery_ready\":%s,\"discovery_name\":\"WhistleCounter-%s\"}",
      WiFi.status() == WL_CONNECTED ? "true" : "false", currentMode, count, target,
      !strcmp(currentMode, "alarm") ? "true" : "false", fsReady ? "true" : "false",
      storageFault ? "true" : "false", lcdAddress, Config::LCD_ADDRESS, ESP.getFreeHeap(),
      detectorSettings.threshold, detectorSettings.minimumDurationMs,
      detectorSettings.quietDurationMs, detectorSettings.cooldownMs, mdnsReady ? "true" : "false", deviceId);
  server.sendHeader("Cache-Control", "no-store");
  server.send(200, "application/json", json);
}
}

namespace NetworkNotifier {
void begin() {
  snprintf(deviceId, sizeof(deviceId), "%06lx", (unsigned long)ESP.getChipId());
  snprintf(bootId, sizeof(bootId), "%06lx%08lx%08lx", (unsigned long)ESP.getChipId(),
           (unsigned long)os_random(), (unsigned long)os_random());
  String keyText(NATIVE_PAIRING_KEY);
  keyReady = hexString(keyText, 64);
  if (keyReady) for (unsigned i = 0; i < 32; ++i)
    key[i] = strtoul(keyText.substring(i * 2, i * 2 + 2).c_str(), nullptr, 16);
  else Serial.println(F("Invalid NativeSecrets.h pairing key. Run tools/prepare_device.py."));
  LittleFSConfig config;
  config.setAutoFormat(false);
  LittleFS.setConfig(config);
  fsReady = LittleFS.begin();
  if (fsReady && !LittleFS.exists("/native-q")) fsReady = LittleFS.mkdir("/native-q");
  if (fsReady) {
    // Only the native journal is cleared on a new boot. The cloud variant's
    // /q, /boot.bin and the saved threshold remain untouched for switching back.
    Dir old = LittleFS.openDir("/native-q");
    while (old.next()) {
      String path = old.fileName();
      if (!path.startsWith("/")) path = String("/native-q/") + path;
      LittleFS.remove(path);
    }
  }
  storageFault = !fsReady;
  WiFi.persistent(false);
  WiFi.mode(WIFI_STA);
  WiFi.hostname("whistle-counter-native");
  WiFi.setAutoReconnect(true);
  WiFi.begin(WIFI_SSID, WIFI_PASSWORD);
  server.collectHeaders("X-Counter-Request");
  server.on("/native/v1/state", HTTP_GET, serveNative);
  server.on("/status", HTTP_GET, serveStatus);
  server.on("/", HTTP_GET, []() {
    server.send(200, "text/html", "<!doctype html><meta name=viewport content='width=device-width'>"
      "<h1>Whistle Counter: Native Android</h1><p>Connect the native Android app to this IP address."
      " Pairing uses the private key generated on your computer.</p><p><a href='/status'>Local status</a></p>"
      "<p>Alarms can only be acknowledged on the physical counter. No remote control endpoint exists.</p>");
  });
  server.onNotFound([]() { server.send(404, "text/plain", "Not found"); });
  server.begin();
  Serial.println(F("Native Android firmware. No cloud server or internet required."));
}

void update() {
  if (WiFi.status() != WL_CONNECTED) {
    if (mdnsReady) MDNS.close();
    mdnsReady = false;
    connectedBefore = false;
    if (millis() - lastReconnect >= 10000) { WiFi.reconnect(); lastReconnect = millis(); }
    return;
  }
  if (!connectedBefore) {
    connectedBefore = true;
    MDNS.close();
    String hostname = String("whistle-counter-") + deviceId;
    mdnsReady = MDNS.begin(hostname.c_str());
    if (mdnsReady) {
      auto service = MDNS.addService((String("WhistleCounter-") + deviceId).c_str(), "whistle", "tcp", 80);
      if (service) {
        MDNS.addServiceTxt(service, "device", deviceId);
        MDNS.addServiceTxt(service, "protocol", "1");
      } else {
        mdnsReady = false;
      }
    }
    Serial.print(F("Native app counter IP: ")); Serial.println(WiFi.localIP());
  }
  if (mdnsReady) MDNS.update();
  server.handleClient();
}

void setState(const char *mode, uint16_t value, uint16_t goal) {
  snprintf(currentMode, sizeof(currentMode), "%s", mode); count = value; target = goal;
}
void startSession() {}
void setDisplayAddress(uint8_t address) { lcdAddress = address; }
void setDetectorSettings(const DetectorSettings &settings) { detectorSettings = settings; }
bool publish(const char *type, uint16_t value, uint16_t goal) {
  uint32_t seq = ++sequence;
  if (!strcmp(type, "completed")) alarmId = seq;
  if (!strcmp(type, "alarm_acknowledged")) acknowledgedAlarm = alarmId;
  char json[200];
  snprintf(json, sizeof(json), "{\"seq\":%lu,\"type\":\"%s\",\"count\":%u,\"target\":%u,\"alarm_id\":%lu}",
           (unsigned long)seq, type, value, goal, (unsigned long)alarmId);
  recent[seq % 16] = json;
  if (!fsReady) return false;
  String path = eventPath(seq), temp = path + ".tmp";
  File file = LittleFS.open(temp, "w");
  bool saved = file && file.print(json) == strlen(json);
  if (file) { file.flush(); file.close(); }
  if (saved) saved = LittleFS.rename(temp, path);
  if (!saved) { LittleFS.remove(temp); storageFault = true; }
  if (seq > RETAIN) LittleFS.remove(eventPath(seq - RETAIN));
  return saved;
}
bool acknowledgeAlarm() { publish("alarm_acknowledged", count, target); return true; }
bool deliveryFault() { return storageFault || !keyReady; }
bool wifiConnected() { return WiFi.status() == WL_CONNECTED; }
String localAddress() {
  return WiFi.status() == WL_CONNECTED ? WiFi.localIP().toString() : String("Not connected");
}
}
