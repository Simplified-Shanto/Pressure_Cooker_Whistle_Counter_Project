#pragma once

#include <Arduino.h>

// Stores only the user-tuned whistle threshold.  The caller decides when to
// save, allowing EEPROM writes to occur only on an explicit Settings exit.
class SettingsStore {
 public:
  uint16_t loadThreshold();
  bool saveThreshold(uint16_t threshold);
};
