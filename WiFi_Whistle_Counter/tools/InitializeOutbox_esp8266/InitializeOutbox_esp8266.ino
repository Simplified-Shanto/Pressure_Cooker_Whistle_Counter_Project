/* ONE-TIME SETUP ONLY: explicitly erases the ESP8266 notification outbox.
   Select Generic ESP8266 Module and the SAME flash/filesystem layout as the
   main sketch. Upload, wait for SUCCESS at 115200, then upload main firmware. */
#include <LittleFS.h>

void setup() {
  Serial.begin(115200);
  delay(1500);
  LittleFSConfig config;
  config.setAutoFormat(false);
  LittleFS.setConfig(config);
  LittleFS.end();
  if (LittleFS.format() && LittleFS.begin() && LittleFS.mkdir("/q")) {
    Serial.println(F("SUCCESS: ESP8266 outbox initialized. Upload the ESP8266 counter sketch now."));
  } else {
    Serial.println(F("FAILED: check actual flash size and select a layout with filesystem space."));
  }
}
void loop() { delay(1000); }
