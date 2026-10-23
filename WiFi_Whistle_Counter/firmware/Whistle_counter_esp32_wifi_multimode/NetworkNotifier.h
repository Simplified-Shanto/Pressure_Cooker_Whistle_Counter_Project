#pragma once
#include <Arduino.h>

#if !defined(ARDUINO_ARCH_ESP32)
#error "This Wi-Fi sketch requires ESP32. Install esp32 by Espressif Systems 2.0.17 in Boards Manager and select ESP32 Dev Module (not NodeMCU/ESP8266)."
#endif

// HTTPS runs in a FreeRTOS task; detector sampling never waits for the network.
// Event files survive reboots. Heartbeats always describe the live boot, never
// an old queued session. This is one device per backend instance.
namespace NetworkNotifier {
void begin();
void setState(const char *mode, uint16_t count, uint16_t target);
void startSession();
bool publish(const char *type, uint16_t count, uint16_t target);
bool deliveryFault();
bool wifiConnected();
}
