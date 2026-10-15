/*
  ESP32 Pressure Cooker Whistle Counter

  Hardware
    - ESP32 DevKit
    - LM393-style microphone/sound sensor module: use AO only
    - 16x2 I2C LCD with a PCF8574 backpack

  The microphone signal is sampled from its analogue output.  Each short time
  window is reduced to peak-to-peak amplitude (a sound envelope), rather than
  using the module's noisy digital comparator output.  A sound must stay above
  the threshold for WHISTLE_MIN_MS before it is counted.
*/

#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// Wiring
constexpr uint8_t SOUND_PIN = 34;  // Mic module AO -> GPIO34 (ADC1, input-only)
constexpr uint8_t LCD_SDA_PIN = 21;
constexpr uint8_t LCD_SCL_PIN = 22;
constexpr uint8_t LCD_ADDRESS = 0x27;  // Confirmed with the LCD hardware test.
constexpr uint8_t TARGET_BUTTON_PIN = 25;   // Short press: target +1; hold: start/reset
constexpr uint8_t DECREMENT_BUTTON_PIN = 26; // Short press: whistle count -1
constexpr uint8_t BUZZER_PIN = 27;           // Active buzzer; HIGH = continuous sound

LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);

// Signal processing. Tune these after observing the serial monitor values.
constexpr uint16_t SAMPLE_INTERVAL_US = 1000;  // 1 kHz ADC sampling
constexpr uint16_t ENVELOPE_WINDOW_MS = 25;    // Peak-to-peak measurement window
constexpr float ENVELOPE_SMOOTHING = 0.30F;    // 0..1; larger responds faster

// Whistle validation. A sound must be continuous enough to be a cooker whistle.
constexpr float START_THRESHOLD = 260.0F;  // Start/candidate threshold, ADC counts
constexpr float STOP_THRESHOLD = 170.0F;   // Lower threshold prevents chatter
constexpr uint32_t WHISTLE_MIN_MS = 180;   // Minimum sustained sound to count
constexpr uint32_t WHISTLE_END_MS = 100;   // Required quiet time before re-arming
constexpr uint32_t MIN_GAP_BETWEEN_WHISTLES_MS = 600;
constexpr uint32_t BUTTON_DEBOUNCE_MS = 30;
constexpr uint32_t START_HOLD_MS = 1000;

enum DetectorState { IDLE, CANDIDATE, ACTIVE };
DetectorState detectorState = IDLE;

uint32_t whistleCount = 0;
uint32_t targetWhistles = 0;
bool counterEngaged = false;
bool alarmActive = false;
uint32_t candidateStartedAt = 0;
uint32_t quietStartedAt = 0;
uint32_t lastWhistleStartedAt = 0;

uint32_t lastSampleAtUs = 0;
uint32_t envelopeWindowStartedAt = 0;
uint32_t lastLcdUpdateAt = 0;
uint16_t windowMinimum = 4095;
uint16_t windowMaximum = 0;
float filteredEnvelope = 0.0F;

struct Button {
  uint8_t pin;
  bool lastRawState = HIGH;
  bool stableState = HIGH;
  bool longPressHandled = false;
  uint32_t lastRawChangeAt = 0;
  uint32_t pressedAt = 0;

  Button(uint8_t buttonPin) : pin(buttonPin) {}
};

Button targetButton{TARGET_BUTTON_PIN};
Button decrementButton{DECREMENT_BUTTON_PIN};

const char *stateText() {
  switch (detectorState) {
    case CANDIDATE: return "Checking";
    case ACTIVE:    return "Whistle";
    default:        return "Listening";
  }
}

void updateLcd() {
  const uint32_t now = millis();
  if (now - lastLcdUpdateAt < 200) return;
  lastLcdUpdateAt = now;

  char firstLine[17];
  char secondLine[17];
  snprintf(firstLine, sizeof(firstLine), "C:%3lu/%3lu       ",
           (unsigned long)whistleCount, (unsigned long)targetWhistles);

  if (alarmActive) {
    snprintf(secondLine, sizeof(secondLine), "TARGET REACHED  ");
  } else if (counterEngaged) {
    // Pad the entire 16-character line so a shorter update cannot leave stale text.
    snprintf(secondLine, sizeof(secondLine), "%-9s %4u  ", stateText(), (unsigned int)filteredEnvelope);
  } else {
    snprintf(secondLine, sizeof(secondLine), "Hold + to start ");
  }

  lcd.setCursor(0, 0);
  lcd.print(firstLine);
  lcd.setCursor(0, 1);
  lcd.print(secondLine);
}

void setBuzzer(bool enabled) {
  digitalWrite(BUZZER_PIN, enabled ? HIGH : LOW);
}

void resetDetector() {
  detectorState = IDLE;
  candidateStartedAt = 0;
  quietStartedAt = 0;
  lastWhistleStartedAt = millis() - MIN_GAP_BETWEEN_WHISTLES_MS;
}

void startCounting() {
  if (targetWhistles == 0) {
    Serial.println("Set a target with a short + button press before starting.");
    return;
  }

  whistleCount = 0;
  counterEngaged = true;
  alarmActive = false;
  setBuzzer(false);
  resetDetector();
  Serial.printf("Counter started. Target: %lu whistles\n", (unsigned long)targetWhistles);
}

void resetAfterAlarm() {
  alarmActive = false;
  counterEngaged = false;
  whistleCount = 0;
  setBuzzer(false);
  resetDetector();
  Serial.println("Alarm acknowledged. Count reset; ready to start again.");
}

void resumeIfTargetWasAdjusted() {
  if (alarmActive && whistleCount < targetWhistles) {
    alarmActive = false;
    counterEngaged = true;
    setBuzzer(false);
    resetDetector();
    Serial.println("Alarm cleared; counter resumed.");
  }
}

void increaseTarget() {
  ++targetWhistles;
  Serial.printf("Target: %lu whistles\n", (unsigned long)targetWhistles);
  resumeIfTargetWasAdjusted();
}

void decreaseWhistleCount() {
  if (whistleCount == 0) return;

  --whistleCount;
  Serial.printf("Whistle count corrected to: %lu\n", (unsigned long)whistleCount);
  resumeIfTargetWasAdjusted();
}

void onShortPress(Button &button) {
  if (button.pin == TARGET_BUTTON_PIN) {
    increaseTarget();
  } else {
    decreaseWhistleCount();
  }
}

void serviceButton(Button &button, uint32_t now) {
  const bool rawState = digitalRead(button.pin);
  if (rawState != button.lastRawState) {
    button.lastRawState = rawState;
    button.lastRawChangeAt = now;
  }

  if (now - button.lastRawChangeAt >= BUTTON_DEBOUNCE_MS &&
      button.stableState != button.lastRawState) {
    button.stableState = button.lastRawState;

    if (button.stableState == LOW) {
      button.pressedAt = now;
      button.longPressHandled = false;

      // Either button acknowledges an active alarm. Do not also process this
      // press as a target/count change when the button is released.
      if (alarmActive) {
        resetAfterAlarm();
        button.longPressHandled = true;
      }
    } else if (!button.longPressHandled) {
      onShortPress(button);
    }
  }

  if (button.pin == TARGET_BUTTON_PIN && button.stableState == LOW &&
      !button.longPressHandled && now - button.pressedAt >= START_HOLD_MS) {
    button.longPressHandled = true;
    startCounting();
  }
}

void processEnvelope(float envelope, uint32_t now) {
  if (!counterEngaged || targetWhistles == 0) return;

  const bool loud = envelope >= START_THRESHOLD;
  const bool quiet = envelope <= STOP_THRESHOLD;

  switch (detectorState) {
    case IDLE:
      // The gap prevents one long, uneven whistle from being counted twice.
      if (loud && now - lastWhistleStartedAt >= MIN_GAP_BETWEEN_WHISTLES_MS) {
        detectorState = CANDIDATE;
        candidateStartedAt = now;
      }
      break;

    case CANDIDATE:
      if (quiet) {
        detectorState = IDLE;  // It was only a short noise burst.
      } else if (now - candidateStartedAt >= WHISTLE_MIN_MS) {
        ++whistleCount;
        lastWhistleStartedAt = candidateStartedAt;
        quietStartedAt = 0;
        Serial.printf("Whistle %lu counted (envelope %.0f)\n",
                      (unsigned long)whistleCount, envelope);

        if (whistleCount >= targetWhistles) {
          counterEngaged = false;
          alarmActive = true;
          setBuzzer(true);
          detectorState = IDLE;
          Serial.println("Target reached. Buzzer on.");
        } else {
          detectorState = ACTIVE;
        }
      }
      break;

    case ACTIVE:
      if (quiet) {
        if (quietStartedAt == 0) quietStartedAt = now;
        if (now - quietStartedAt >= WHISTLE_END_MS) {
          detectorState = IDLE;
          quietStartedAt = 0;
        }
      } else {
        quietStartedAt = 0;
      }
      break;
  }
}

void sampleMicrophone() {
  const uint32_t nowUs = micros();
  if (nowUs - lastSampleAtUs < SAMPLE_INTERVAL_US) return;
  lastSampleAtUs = nowUs;

  const uint16_t sample = analogRead(SOUND_PIN);
  if (sample < windowMinimum) windowMinimum = sample;
  if (sample > windowMaximum) windowMaximum = sample;

  const uint32_t nowMs = millis();
  if (nowMs - envelopeWindowStartedAt < ENVELOPE_WINDOW_MS) return;

  const float rawEnvelope = windowMaximum - windowMinimum;
  filteredEnvelope += ENVELOPE_SMOOTHING * (rawEnvelope - filteredEnvelope);

  // Useful while setting the mic gain potentiometer and thresholds.
  Serial.printf("raw=%3.0f filtered=%3.0f state=%s\n",
                rawEnvelope, filteredEnvelope, stateText());
  processEnvelope(filteredEnvelope, nowMs);

  windowMinimum = 4095;
  windowMaximum = 0;
  envelopeWindowStartedAt = nowMs;
}

void setup() {
  Serial.begin(115200);

  // ESP32 ADC: 12-bit reads (0..4095). The mic must be powered from 3.3 V.
  analogReadResolution(12);
  analogSetPinAttenuation(SOUND_PIN, ADC_11db);
  pinMode(SOUND_PIN, INPUT);
  pinMode(TARGET_BUTTON_PIN, INPUT_PULLUP);
  pinMode(DECREMENT_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  setBuzzer(false);

  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN);
  lcd.init();
  lcd.backlight();
  lcd.clear();
  lcd.setCursor(0, 0);
  lcd.print("Whistle Counter");
  lcd.setCursor(0, 1);
  lcd.print("Starting...");

  envelopeWindowStartedAt = millis();
  resetDetector();
  Serial.println("Pressure cooker whistle counter ready.");
  Serial.println("Short + sets target; hold + for 1 second to start; - corrects count.");
  Serial.println("Adjust START_THRESHOLD after checking the raw/filtered values.");
}

void loop() {
  const uint32_t now = millis();
  serviceButton(targetButton, now);
  serviceButton(decrementButton, now);
  sampleMicrophone();
  updateLcd();
}
