/* ONE-TIME SETUP ONLY. Erases queued notifications, not Wi-Fi settings.
   Select ESP32 Dev Module with the same partition scheme as the main sketch.
   Upload, wait for SUCCESS in Serial Monitor, then upload the main sketch.
   Do not use this sketch as part of normal firmware updates. */
#include <LittleFS.h>

void setup() {
  Serial.begin(115200);
  delay(1500);
  // Establish the default "spiffs" partition label even if mount fails.
  LittleFS.begin(false);
  LittleFS.end();
  if (LittleFS.format() && LittleFS.begin(false) && LittleFS.mkdir("/q")) {
    Serial.println("SUCCESS: outbox initialized. Upload the main counter sketch now.");
  } else {
    Serial.println("FAILED: check the selected ESP32 board and flash partition scheme.");
  }
}
void loop() { delay(1000); }
