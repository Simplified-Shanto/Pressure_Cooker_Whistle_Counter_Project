#include "SettingsStore.h"

#include <EEPROM.h>

#include "Config.h"

namespace {
struct SavedSettings {
  uint32_t magic;
  uint16_t threshold;
  uint16_t checksum;
};

struct SavedTiming {
  uint32_t magic;
  uint16_t version;
  uint16_t minimumDurationMs;
  uint16_t quietDurationMs;
  uint16_t cooldownMs;
  uint16_t checksum;
};
constexpr uint32_t TIMING_MAGIC = 0x5743544DUL; // WCTM
constexpr uint16_t TIMING_VERSION = 1;
constexpr size_t TIMING_OFFSET = 8;
static_assert(sizeof(SavedSettings) == TIMING_OFFSET, "Keep legacy threshold layout");
static_assert(TIMING_OFFSET + sizeof(SavedTiming) <= Config::EEPROM_BYTES,
              "Settings must fit existing EEPROM allocation");

uint16_t timingChecksum(const SavedTiming &saved) {
  // CRC-16/CCITT over explicit fields, excluding compiler padding.
  uint16_t crc = 0xFFFF;
  const uint16_t fields[] = {static_cast<uint16_t>(saved.magic),
      static_cast<uint16_t>(saved.magic >> 16), saved.version,
      saved.minimumDurationMs, saved.quietDurationMs, saved.cooldownMs};
  for (uint16_t field : fields) {
    for (uint8_t byte = 0; byte < 2; ++byte) {
      crc ^= static_cast<uint16_t>((field >> (byte * 8)) & 0xFF) << 8;
      for (uint8_t bit = 0; bit < 8; ++bit)
        crc = crc & 0x8000 ? (crc << 1) ^ 0x1021 : crc << 1;
    }
  }
  return crc;
}

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

DetectorSettings SettingsStore::load() {
  DetectorSettings settings;
  EEPROM.begin(Config::EEPROM_BYTES);
  SavedSettings saved{};
  EEPROM.get(0, saved);

  if (isValid(saved)) {
    Serial.printf("Loaded threshold: %u\n", saved.threshold);
    settings.threshold = saved.threshold;
  } else {
    Serial.println("No valid saved threshold; using default 65.");
  }

  SavedTiming timing{};
  EEPROM.get(TIMING_OFFSET, timing);
  DetectorSettings candidate = settings;
  candidate.minimumDurationMs = timing.minimumDurationMs;
  candidate.quietDurationMs = timing.quietDurationMs;
  candidate.cooldownMs = timing.cooldownMs;
  if (timing.magic == TIMING_MAGIC && timing.version == TIMING_VERSION &&
      timing.checksum == timingChecksum(timing) && candidate.validTiming()) {
    settings = candidate;
  } else {
    Serial.println("No valid saved timing; using cooker timing defaults.");
  }
  Serial.printf("Detector timing: minimum=%u quiet=%u cooldown=%u ms\n",
                settings.minimumDurationMs, settings.quietDurationMs, settings.cooldownMs);
  // No boot-time writes or filesystem formatting during migration.
  return settings;
}

bool SettingsStore::save(const DetectorSettings &settings) {
  if (!settings.validThreshold() || !settings.validTiming()) return false;
  SavedSettings saved{};
  saved.magic = Config::EEPROM_MAGIC;
  saved.threshold = settings.threshold;
  saved.checksum = checksumFor(settings.threshold);
  EEPROM.put(0, saved);

  SavedTiming timing{};
  timing.magic = TIMING_MAGIC;
  timing.version = TIMING_VERSION;
  timing.minimumDurationMs = settings.minimumDurationMs;
  timing.quietDurationMs = settings.quietDurationMs;
  timing.cooldownMs = settings.cooldownMs;
  timing.checksum = timingChecksum(timing);
  EEPROM.put(TIMING_OFFSET, timing);

  const bool committed = EEPROM.commit();
  Serial.printf("Detector settings EEPROM save %s.\n", committed ? "succeeded" : "failed");
  return committed;
}
