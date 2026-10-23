#pragma once

#include <Arduino.h>
#include "DetectorSettings.h"

// Converts fast A0 microphone samples into one event per valid whistle.
// A valid event must stay loud enough for a minimum time and then go quiet.
class WhistleDetector {
 public:
  void begin();
  void reset();

  // Returns true only once for each completed whistle burst.
  bool update(const DetectorSettings &settings);
  uint16_t filteredEnvelope() const;

 private:
  enum class State : uint8_t { IDLE, CANDIDATE, ACTIVE, COOLDOWN };

  State state_ = State::IDLE;
  uint16_t windowMinimum_ = 1023;
  uint16_t windowMaximum_ = 0;
  float filteredEnvelope_ = 0.0f;
  uint32_t lastSampleAtUs_ = 0;
  uint32_t envelopeWindowStartedAt_ = 0;
  uint32_t candidateStartedAt_ = 0;
  uint32_t belowThresholdStartedAt_ = 0;
  uint32_t lastWhistleAt_ = 0;
  bool quietTracking_ = false;
};
