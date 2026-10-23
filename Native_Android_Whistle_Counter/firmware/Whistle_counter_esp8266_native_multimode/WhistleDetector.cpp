#include "WhistleDetector.h"

#include "Config.h"

void WhistleDetector::begin() {
  reset();
}

void WhistleDetector::reset() {
  state_ = State::IDLE;
  windowMinimum_ = 1023;
  windowMaximum_ = 0;
  filteredEnvelope_ = 0.0f;
  candidateStartedAt_ = 0;
  belowThresholdStartedAt_ = 0;
  quietTracking_ = false;
  lastSampleAtUs_ = micros();
  envelopeWindowStartedAt_ = millis();
  // A newly started listening session should accept its first whistle without
  // waiting through the inter-whistle gap intended for later whistles.
  lastWhistleAt_ = 0;
}

uint16_t WhistleDetector::filteredEnvelope() const {
  return static_cast<uint16_t>(filteredEnvelope_);
}

bool WhistleDetector::update(const DetectorSettings &settings) {
  const uint16_t startThreshold = settings.threshold;
  const uint32_t nowUs = micros();
  if (nowUs - lastSampleAtUs_ < Config::SAMPLE_INTERVAL_US) {
    return false;
  }
  lastSampleAtUs_ = nowUs;

  const uint16_t sample = analogRead(Config::SOUND_PIN);
  if (sample < windowMinimum_) windowMinimum_ = sample;
  if (sample > windowMaximum_) windowMaximum_ = sample;

  const uint32_t now = millis();
  if (now - envelopeWindowStartedAt_ < Config::ENVELOPE_WINDOW_MS) {
    return false;
  }

  const uint16_t rawEnvelope = windowMaximum_ - windowMinimum_;
  filteredEnvelope_ += Config::ENVELOPE_SMOOTHING *
                       (rawEnvelope - filteredEnvelope_);
  windowMinimum_ = 1023;
  windowMaximum_ = 0;
  envelopeWindowStartedAt_ = now;

  const uint16_t envelope = filteredEnvelope();
  const uint16_t stopThreshold =
      startThreshold > Config::STOP_HYSTERESIS
          ? startThreshold - Config::STOP_HYSTERESIS
          : 1;
  const bool aboveStart = envelope >= startThreshold;
  const bool belowStop = envelope <= stopThreshold;

  switch (state_) {
    case State::IDLE:
      if (aboveStart) {
        state_ = State::CANDIDATE;
        candidateStartedAt_ = now;
      }
      break;

    case State::CANDIDATE:
      if (!aboveStart) {
        // Brief noise did not last long enough to be a cooker whistle.
        state_ = State::IDLE;
      } else if (now - candidateStartedAt_ >= settings.minimumDurationMs) {
        state_ = State::ACTIVE;
        quietTracking_ = false;
      }
      break;

    case State::ACTIVE:
      if (!belowStop) {
        quietTracking_ = false;
        break;
      }

      if (!quietTracking_) {
        quietTracking_ = true;
        belowThresholdStartedAt_ = now;
      } else if (now - belowThresholdStartedAt_ >= settings.quietDurationMs) {
        state_ = State::COOLDOWN;
        lastWhistleAt_ = now;
        return true;
      }
      break;

    case State::COOLDOWN:
      // A burst that starts during cooldown must not become a new whistle
      // merely because the timer expires halfway through that same burst.
      if (!belowStop) {
        quietTracking_ = false;
      } else {
        if (!quietTracking_) {
          quietTracking_ = true;
          belowThresholdStartedAt_ = now;
        }
        if (now - lastWhistleAt_ >= settings.cooldownMs &&
            now - belowThresholdStartedAt_ >= settings.quietDurationMs) {
          state_ = State::IDLE;
        }
      }
      break;
  }

  return false;
}
