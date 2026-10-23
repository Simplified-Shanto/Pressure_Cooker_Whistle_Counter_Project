#pragma once

#include "Config.h"

struct DetectorSettings {
  uint16_t threshold = Config::DEFAULT_START_THRESHOLD;
  uint16_t minimumDurationMs = Config::WHISTLE_MIN_DURATION_MS;
  uint16_t quietDurationMs = Config::WHISTLE_END_DURATION_MS;
  uint16_t cooldownMs = Config::WHISTLE_MIN_GAP_MS;

  bool validThreshold() const {
    return threshold >= Config::MIN_START_THRESHOLD &&
           threshold <= Config::MAX_START_THRESHOLD;
  }

  bool validTiming() const {
    return minimumDurationMs >= Config::MIN_DURATION_LIMIT_MS &&
           minimumDurationMs <= Config::MAX_DURATION_LIMIT_MS &&
           quietDurationMs >= Config::MIN_QUIET_LIMIT_MS &&
           quietDurationMs <= Config::MAX_QUIET_LIMIT_MS &&
           cooldownMs >= Config::MIN_COOLDOWN_LIMIT_MS &&
           cooldownMs <= Config::MAX_COOLDOWN_LIMIT_MS;
  }
};
