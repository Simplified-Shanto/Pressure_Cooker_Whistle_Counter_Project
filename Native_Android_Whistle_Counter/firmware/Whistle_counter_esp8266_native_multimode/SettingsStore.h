#pragma once

#include <Arduino.h>
#include "DetectorSettings.h"

// The first eight bytes retain the original threshold format. Timing settings
// occupy a separate versioned record. Write only on an explicit Settings exit.
class SettingsStore {
 public:
  DetectorSettings load();
  bool save(const DetectorSettings &settings);
};
