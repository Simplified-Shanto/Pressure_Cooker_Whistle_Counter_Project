/*
  Pressure cooker whistle counter - ESP8266 / NodeMCU

  The microphone module is read through its analogue A0 output.  A cooker
  whistle is a burst, rather than one continuous sound, so the program first
  checks that the sound stays above the threshold long enough to be a real
  whistle.  It then waits for the sound to fall before accepting another one.

  Wiring (NodeMCU board labels):
    A0  -> sound-sensor analogue output (AO)
    D1  -> LCD SCL  (GPIO5)
    D2  -> LCD SDA  (GPIO4)
    D6  -> top button, target + (GPIO12)
    D3  -> middle button, target - / threshold - (GPIO0)
    D4  -> bottom button, start / settings (GPIO2)
    D5  -> buzzer control (GPIO14)

  Buttons use the ESP8266 internal pull-ups.  Connect each button between its
  pin and GND: a pressed button therefore reads LOW.

  IMPORTANT: D3/GPIO0 and D4/GPIO2 are ESP8266 boot pins.  Do not hold the
  middle or bottom button while the board is powered on or reset, otherwise
  the board can fail to boot.  D6 is not a boot-strapping pin.

  LCD address is 0x27.  If the LCD stays blank, scan its I2C address and
  change LCD_ADDRESS (many backpacks use 0x3F instead).
*/

#include <EEPROM.h>
#include <Wire.h>
#include <LiquidCrystal_I2C.h>

// ---------------------------------------------------------------------------
// Pin assignment.  These are physical ESP8266 GPIO numbers, so the code also
// compiles when the Arduino IDE has a generic ESP8266 board selected.
// ---------------------------------------------------------------------------
constexpr uint8_t SOUND_PIN = A0;
constexpr uint8_t LCD_SCL_PIN = 5;       // NodeMCU D1
constexpr uint8_t LCD_SDA_PIN = 4;       // NodeMCU D2
constexpr uint8_t TOP_BUTTON_PIN = 12;   // NodeMCU D6
constexpr uint8_t MIDDLE_BUTTON_PIN = 0; // NodeMCU D3
constexpr uint8_t BOTTOM_BUTTON_PIN = 2; // NodeMCU D4
constexpr uint8_t BUZZER_PIN = 14;       // NodeMCU D5

constexpr uint8_t LCD_ADDRESS = 0x27;
LiquidCrystal_I2C lcd(LCD_ADDRESS, 16, 2);

// ---------------------------------------------------------------------------
// Whistle detector tuning.
// A 25 ms peak-to-peak envelope rejects the microphone's DC level and makes
// the threshold easier to tune.  The displayed "E" value in listening mode is
// this envelope value, while "T" is the start threshold.
// ---------------------------------------------------------------------------
constexpr uint32_t SAMPLE_INTERVAL_US = 1000;  // Read A0 every 1 ms.
constexpr uint16_t ENVELOPE_WINDOW_MS = 25;     // Peak-to-peak measurement.
constexpr float ENVELOPE_SMOOTHING = 0.30f;     // 0..1; higher reacts faster.

constexpr uint16_t DEFAULT_START_THRESHOLD = 65;
constexpr uint16_t MIN_START_THRESHOLD = 25;
constexpr uint16_t MAX_START_THRESHOLD = 900;
constexpr uint8_t THRESHOLD_STEP = 3;
constexpr uint16_t STOP_HYSTERESIS = 22;

constexpr uint16_t WHISTLE_MIN_DURATION_MS = 180;
constexpr uint16_t WHISTLE_END_DURATION_MS = 100;
constexpr uint16_t WHISTLE_MIN_GAP_MS = 600;

// ---------------------------------------------------------------------------
// User-interface timing.
// ---------------------------------------------------------------------------
constexpr uint16_t BUTTON_DEBOUNCE_MS = 30;
constexpr uint16_t LONG_PRESS_MS = 1000;
constexpr uint16_t BUTTON_FEEDBACK_MS = 55;
constexpr uint16_t LCD_REFRESH_MS = 100;
constexpr uint16_t COMPLETION_BLINK_MS = 500;
constexpr uint16_t ALARM_BEEP_INTERVAL_MS = 500;
constexpr uint16_t STARTUP_SCREEN_MS = 900;

// EEPROM is only written when Settings is explicitly exited after a change.
constexpr size_t EEPROM_BYTES = 32;
constexpr uint32_t EEPROM_MAGIC = 0x57434E54UL; // "WCNT"

struct SavedSettings {
  uint32_t magic;
  uint16_t startThreshold;
  uint16_t check; // Simple integrity check for accidental/uninitialised data.
};

enum class UiState : uint8_t {
  HOME,
  LISTENING,
  SETTINGS,
  ALARM
};

enum class DetectorState : uint8_t {
  IDLE,
  CANDIDATE,
  ACTIVE
};

struct Button {
  uint8_t pin;
  bool rawPressed = false;
  bool stablePressed = false;
  bool longPressHandled = false;
  bool pressConsumed = false;
  uint32_t rawChangedAt = 0;
  uint32_t pressedAt = 0;
};

Button topButton{TOP_BUTTON_PIN};
Button middleButton{MIDDLE_BUTTON_PIN};
Button bottomButton{BOTTOM_BUTTON_PIN};

UiState uiState = UiState::HOME;
DetectorState detectorState = DetectorState::IDLE;

uint16_t targetWhistles = 1;
uint16_t countedWhistles = 0;
uint16_t startThreshold = DEFAULT_START_THRESHOLD;
bool thresholdDirty = false;

uint16_t windowMinimum = 1023;
uint16_t windowMaximum = 0;
float filteredEnvelope = 0.0f;
uint32_t lastSampleAtUs = 0;
uint32_t envelopeWindowStartedAt = 0;
uint32_t candidateStartedAt = 0;
uint32_t belowThresholdStartedAt = 0;
uint32_t lastWhistleAt = 0;

uint32_t feedbackBeepStartedAt = 0;
bool feedbackBeepActive = false;
uint32_t lastLcdRefreshAt = 0;
uint32_t startupScreenStartedAt = 0;

// ---------------------------------------------------------------------------
// Forward declarations keep the loop and button state machine easy to read.
// ---------------------------------------------------------------------------
void updateLcd(bool force = false);
void returnHome(bool resetTarget);
void startListening();
void enterSettings();
void leaveSettings();
void resetDetector();
void requestButtonFeedback();

uint16_t stopThreshold() {
  // A lower stop threshold prevents one whistle from being split into several.
  return (startThreshold > STOP_HYSTERESIS)
             ? startThreshold - STOP_HYSTERESIS
             : 1;
}

uint16_t settingsChecksum(uint16_t threshold) {
  return static_cast<uint16_t>(threshold ^ 0xA55A);
}

void printLine(uint8_t row, const char *text) {
  // %-16.16s both pads a short line and prevents stale characters remaining.
  char line[17];
  snprintf(line, sizeof(line), "%-16.16s", text);
  lcd.setCursor(0, row);
  lcd.print(line);
}

void loadThresholdFromEeprom() {
  EEPROM.begin(EEPROM_BYTES);

  SavedSettings saved{};
  EEPROM.get(0, saved);

  const bool isValid =
      saved.magic == EEPROM_MAGIC &&
      saved.check == settingsChecksum(saved.startThreshold) &&
      saved.startThreshold >= MIN_START_THRESHOLD &&
      saved.startThreshold <= MAX_START_THRESHOLD;

  if (isValid) {
    startThreshold = saved.startThreshold;
    Serial.printf("Loaded threshold from EEPROM: %u\n", startThreshold);
  } else {
    // Do not write the default here.  EEPROM is written only after a deliberate
    // threshold-setting action, which avoids needless flash wear on startup.
    startThreshold = DEFAULT_START_THRESHOLD;
    Serial.println("No valid saved threshold; using default 65.");
  }
}

void saveThresholdToEeprom() {
  SavedSettings saved{};
  saved.magic = EEPROM_MAGIC;
  saved.startThreshold = startThreshold;
  saved.check = settingsChecksum(saved.startThreshold);

  EEPROM.put(0, saved);
  if (EEPROM.commit()) {
    thresholdDirty = false;
    Serial.printf("Saved threshold to EEPROM: %u\n", startThreshold);
  } else {
    // Keep the dirty flag set if a commit fails, so the user can retry Settings.
    Serial.println("EEPROM save failed.");
  }
}

void requestButtonFeedback() {
  // This is deliberately non-blocking: sampling continues while it beeps.
  if (uiState != UiState::ALARM) {
    feedbackBeepActive = true;
    feedbackBeepStartedAt = millis();
  }
}

void serviceBuzzer() {
  if (uiState == UiState::ALARM) {
    // Pulse an active buzzer for 500 ms, then silence it for 500 ms.  This is
    // intentionally independent of the LCD blink so either timing can be
    // changed later without affecting the other.  A passive piezo needs a
    // tone/PWM driver circuit instead of this direct digital output.
    const bool alarmBeepOn =
        (millis() / ALARM_BEEP_INTERVAL_MS) % 2 == 0;
    digitalWrite(BUZZER_PIN, alarmBeepOn ? HIGH : LOW);
    return;
  }

  if (feedbackBeepActive && millis() - feedbackBeepStartedAt < BUTTON_FEEDBACK_MS) {
    digitalWrite(BUZZER_PIN, HIGH);
  } else {
    feedbackBeepActive = false;
    digitalWrite(BUZZER_PIN, LOW);
  }
}

void resetDetector() {
  detectorState = DetectorState::IDLE;
  candidateStartedAt = 0;
  belowThresholdStartedAt = 0;
  windowMinimum = 1023;
  windowMaximum = 0;
  filteredEnvelope = 0.0f;
}

void returnHome(bool resetTarget) {
  uiState = UiState::HOME;
  feedbackBeepActive = false;
  countedWhistles = 0;
  resetDetector();

  if (resetTarget) {
    targetWhistles = 1;
  }

  Serial.println("Home screen.");
  updateLcd(true);
}

void startListening() {
  uiState = UiState::LISTENING;
  countedWhistles = 0;
  resetDetector();
  Serial.printf("Listening: target=%u threshold=%u\n", targetWhistles, startThreshold);
  updateLcd(true);
}

void enterSettings() {
  uiState = UiState::SETTINGS;
  thresholdDirty = false;
  feedbackBeepActive = false;
  Serial.println("Threshold settings entered.");
  updateLcd(true);
}

void leaveSettings() {
  // This is the only normal code path that writes EEPROM.  Holding the bottom
  // button in the settings screen causes one save, not repeated loop writes.
  if (thresholdDirty) {
    saveThresholdToEeprom();
  }
  returnHome(false);
}

void handleButtonPress(Button &button) {
  // Every physical button press gets the short acknowledgement beep, even in
  // modes where its short action is intentionally ignored.
  requestButtonFeedback();

  if (uiState == UiState::ALARM) {
    // Any button silences the continuous alarm and starts a fresh session.
    returnHome(true);
    button.pressConsumed = true;
    return;
  }

  if (uiState == UiState::HOME) {
    if (&button == &topButton) {
      if (targetWhistles < 999) {
        ++targetWhistles;
      }
      updateLcd(true);
    } else if (&button == &middleButton) {
      if (targetWhistles > 1) {
        --targetWhistles;
      }
      updateLcd(true);
    }
  } else if (uiState == UiState::SETTINGS) {
    if (&button == &topButton && startThreshold + THRESHOLD_STEP <= MAX_START_THRESHOLD) {
      startThreshold += THRESHOLD_STEP;
      thresholdDirty = true;
      updateLcd(true);
    } else if (&button == &middleButton && startThreshold >= MIN_START_THRESHOLD + THRESHOLD_STEP) {
      startThreshold -= THRESHOLD_STEP;
      thresholdDirty = true;
      updateLcd(true);
    }
  }
}

void serviceButton(Button &button) {
  const uint32_t now = millis();
  const bool rawPressedNow = digitalRead(button.pin) == LOW;

  if (rawPressedNow != button.rawPressed) {
    button.rawPressed = rawPressedNow;
    button.rawChangedAt = now;
  }

  // Change stable state only after the input has remained unchanged long
  // enough.  This handles the electrical bounce of ordinary push-buttons.
  if (button.rawPressed != button.stablePressed &&
      now - button.rawChangedAt >= BUTTON_DEBOUNCE_MS) {
    button.stablePressed = button.rawPressed;

    if (button.stablePressed) {
      button.pressedAt = now;
      button.longPressHandled = false;
      button.pressConsumed = false;
      handleButtonPress(button);
    } else {
      // A short bottom press starts listening, but only from Home.  It happens
      // on release so a hold can be distinguished from the settings command.
      if (&button == &bottomButton && !button.longPressHandled &&
          !button.pressConsumed && uiState == UiState::HOME) {
        startListening();
      }
    }
  }

  // Bottom-button long presses have one action per screen:
  //   Home      -> enter threshold settings
  //   Settings  -> save (if changed) and return Home
  //   Listening -> cancel counting and return Home without changing target
  if (&button == &bottomButton && button.stablePressed &&
      !button.longPressHandled && now - button.pressedAt >= LONG_PRESS_MS) {
    button.longPressHandled = true;
    button.pressConsumed = true;

    if (uiState == UiState::HOME) {
      enterSettings();
    } else if (uiState == UiState::SETTINGS) {
      leaveSettings();
    } else if (uiState == UiState::LISTENING) {
      returnHome(false);
    }
  }
}

void countWhistle() {
  ++countedWhistles;
  Serial.printf("Whistle %u/%u\n", countedWhistles, targetWhistles);
  updateLcd(true);

  if (countedWhistles >= targetWhistles) {
    countedWhistles = targetWhistles;
    uiState = UiState::ALARM;
    feedbackBeepActive = false;
    detectorState = DetectorState::IDLE;
    Serial.println("Target reached: alarm engaged.");
    updateLcd(true);
  }
}

void processEnvelope(uint16_t envelope) {
  if (uiState != UiState::LISTENING) {
    return;
  }

  const uint32_t now = millis();
  const bool aboveStart = envelope >= startThreshold;
  const bool belowStop = envelope <= stopThreshold();

  switch (detectorState) {
    case DetectorState::IDLE:
      if (aboveStart && now - lastWhistleAt >= WHISTLE_MIN_GAP_MS) {
        detectorState = DetectorState::CANDIDATE;
        candidateStartedAt = now;
      }
      break;

    case DetectorState::CANDIDATE:
      if (!aboveStart) {
        // The burst was too short: it was probably a click or brief noise.
        detectorState = DetectorState::IDLE;
      } else if (now - candidateStartedAt >= WHISTLE_MIN_DURATION_MS) {
        // Only a sustained, loud enough burst becomes a valid whistle.
        detectorState = DetectorState::ACTIVE;
        belowThresholdStartedAt = 0;
      }
      break;

    case DetectorState::ACTIVE:
      if (belowStop) {
        if (belowThresholdStartedAt == 0) {
          belowThresholdStartedAt = now;
        } else if (now - belowThresholdStartedAt >= WHISTLE_END_DURATION_MS) {
          // The whistle has ended, so count exactly one event for this burst.
          lastWhistleAt = now;
          detectorState = DetectorState::IDLE;
          belowThresholdStartedAt = 0;
          countWhistle();
        }
      } else {
        belowThresholdStartedAt = 0;
      }
      break;
  }
}

void serviceSoundSampling() {
  const uint32_t nowUs = micros();
  if (nowUs - lastSampleAtUs < SAMPLE_INTERVAL_US) {
    return;
  }
  lastSampleAtUs = nowUs;

  const uint16_t sample = analogRead(SOUND_PIN);
  if (sample < windowMinimum) windowMinimum = sample;
  if (sample > windowMaximum) windowMaximum = sample;

  const uint32_t nowMs = millis();
  if (nowMs - envelopeWindowStartedAt < ENVELOPE_WINDOW_MS) {
    return;
  }

  const uint16_t rawEnvelope = windowMaximum - windowMinimum;
  filteredEnvelope += ENVELOPE_SMOOTHING * (rawEnvelope - filteredEnvelope);
  windowMinimum = 1023;
  windowMaximum = 0;
  envelopeWindowStartedAt = nowMs;
  processEnvelope(static_cast<uint16_t>(filteredEnvelope));
}

void updateLcd(bool force) {
  const uint32_t now = millis();
  if (!force && now - lastLcdRefreshAt < LCD_REFRESH_MS) {
    return;
  }
  lastLcdRefreshAt = now;

  if (now - startupScreenStartedAt < STARTUP_SCREEN_MS) {
    printLine(0, "Whistle Counter");
    printLine(1, "Starting...");
    return;
  }

  char line[24];
  switch (uiState) {
    case UiState::HOME:
      snprintf(line, sizeof(line), "Target: %u", targetWhistles);
      printLine(0, line);
      printLine(1, "Bottom: Start");
      break;

    case UiState::LISTENING:
      snprintf(line, sizeof(line), "Whistles: %u/%u", countedWhistles, targetWhistles);
      printLine(0, line);
      snprintf(line, sizeof(line), "E:%3u T:%3u", static_cast<uint16_t>(filteredEnvelope), startThreshold);
      printLine(1, line);
      break;

    case UiState::SETTINGS:
      printLine(0, "Set threshold");
      snprintf(line, sizeof(line), "T:%u  +/- %u", startThreshold, THRESHOLD_STEP);
      printLine(1, line);
      break;

    case UiState::ALARM:
      // Alternate full text with a blank screen every 500 ms.  The continuous
      // alarm is kept separate in serviceBuzzer(), so it does not blink.
      if ((now / COMPLETION_BLINK_MS) % 2 == 0) {
        snprintf(line, sizeof(line), "%u/%u whistles", countedWhistles, targetWhistles);
        printLine(0, line);
        printLine(1, "COMPLETED");
      } else {
        printLine(0, "");
        printLine(1, "");
      }
      break;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("Pressure cooker whistle counter booting.");

  pinMode(TOP_BUTTON_PIN, INPUT_PULLUP);
  pinMode(MIDDLE_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BOTTOM_BUTTON_PIN, INPUT_PULLUP);
  pinMode(BUZZER_PIN, OUTPUT);
  digitalWrite(BUZZER_PIN, LOW);

  Wire.begin(LCD_SDA_PIN, LCD_SCL_PIN); // ESP8266 order is SDA, then SCL.
  lcd.init();
  lcd.backlight();
  lcd.clear();

  loadThresholdFromEeprom();
  startupScreenStartedAt = millis();
  envelopeWindowStartedAt = millis();
  updateLcd(true);
}

void loop() {
  serviceButton(topButton);
  serviceButton(middleButton);
  serviceButton(bottomButton);

  // Sound sampling remains active in all modes, but processEnvelope() only
  // counts whistle events while the state is LISTENING.
  serviceSoundSampling();
  serviceBuzzer();
  updateLcd(false);
}
