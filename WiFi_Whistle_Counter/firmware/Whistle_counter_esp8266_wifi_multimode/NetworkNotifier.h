#pragma once
#include <Arduino.h>

#if !defined(ARDUINO_ARCH_ESP8266)
#error "Use Generic ESP8266 Module with esp8266 core 3.1.2 for this sketch."
#endif

// The loop polls a persistent TLS connection. Reopening TLS is deferred while
// listening because ESP8266 TLS handshakes can block its only application core.
namespace NetworkNotifier {
void begin();
void update();
void setState(const char *mode, uint16_t count, uint16_t target);
void startSession();
void setDisplayAddress(uint8_t address);
bool publish(const char *type, uint16_t count, uint16_t target);
bool deliveryFault();
bool wifiConnected();
}
