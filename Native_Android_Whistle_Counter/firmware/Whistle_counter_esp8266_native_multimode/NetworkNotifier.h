#pragma once
#include <Arduino.h>
#include "DetectorSettings.h"

namespace NetworkNotifier {
void begin();
void update();
void setState(const char *mode, uint16_t count, uint16_t target);
void startSession();
void setDisplayAddress(uint8_t address);
void setDetectorSettings(const DetectorSettings &settings);
bool publish(const char *type, uint16_t count, uint16_t target);
bool acknowledgeAlarm();
bool deliveryFault();
bool wifiConnected();
}
