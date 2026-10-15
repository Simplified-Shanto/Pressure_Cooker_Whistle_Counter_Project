#include "SettingsStore.h"

#include <EEPROM.h>

#include "Config.h"

namespace {
struct SavedSettings {
  uint32_t magic;
  uint16_t threshold;
  uint16_t checksum;
};

uint16_t checksumFor(uint16_t threshold) {
  return static_cast<uint16_t>(threshold ^ 0xA55A);
}

bool isValid(const SavedSettings &saved) {
  return saved.magic == Config::EEPROM_MAGIC &&
         saved.checksum == checksumFor(saved.threshold) &&
         saved.threshold >= Config::MIN_START_THRESHOLD &&
         saved.threshold <= Config::MAX_START_THRESHOLD;
}
} // namespace

uint16_t SettingsStore::loadThreshold() {
  EEPROM.begin(Config::EEPROM_BYTES);
  SavedSettings saved{};
  EEPROM.get(0, saved);

  if (isValid(saved)) {
    Serial.printf("Loaded threshold: %u\n", saved.threshold);
    return saved.threshold;
  }

  // Do not write a default during boot: it wastes EEPROM flash endurance.
  Serial.println("No valid saved threshold; using default 65.");
  return Config::DEFAULT_START_THRESHOLD;
}

bool SettingsStore::saveThreshold(uint16_t threshold) {
  SavedSettings saved{};
  saved.magic = Config::EEPROM_MAGIC;
  saved.threshold = threshold;
  saved.checksum = checksumFor(threshold);
  EEPROM.put(0, saved);

  const bool committed = EEPROM.commit();
  Serial.printf("Threshold EEPROM save %s.\n", committed ? "succeeded" : "failed");
  return committed;
}
