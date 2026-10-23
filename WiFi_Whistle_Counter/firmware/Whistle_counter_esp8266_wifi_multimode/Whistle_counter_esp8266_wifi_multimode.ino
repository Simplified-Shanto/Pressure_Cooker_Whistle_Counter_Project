/*
  Generic ESP8266 multi-mode whistle counter with a durable Wi-Fi event outbox.
  AO: A0; LCD SDA/SCL: GPIO4/5; buttons: GPIO12/0/2; buzzer: GPIO14.
  GPIO0 and GPIO2 must remain HIGH during normal boot. Use the original wiring.
  See README.md for board, flash, and HTTPS configuration.
*/

#include "Button.h"
#include "Config.h"
#include "Display.h"
#include "SettingsStore.h"
#include "WhistleDetector.h"
#include "NetworkNotifier.h"

enum class AppMode : uint8_t {
  HOME,
  LISTENING,
  THRESHOLD_SETTINGS,
  WHISTLE_ALARM,
  STOPWATCH,
  TIMER
};

enum ButtonId : uint8_t { TOP = 0, MIDDLE = 1, BOTTOM = 2 };

Button buttons[] = {
    Button(Config::TOP_BUTTON_PIN),
    Button(Config::MIDDLE_BUTTON_PIN),
    Button(Config::BOTTOM_BUTTON_PIN),
};

Display display;
SettingsStore settingsStore;
WhistleDetector whistleDetector;

AppMode mode = AppMode::HOME;
uint16_t targetWhistles = 1;
uint16_t countedWhistles = 0;
uint16_t whistleThreshold = Config::DEFAULT_START_THRESHOLD;
bool thresholdDirty = false;

// Stopwatch stores completed time separately from its current running slice,
// allowing pause/resume without losing any time.
uint32_t stopwatchAccumulatedMs = 0;
uint32_t stopwatchStartedAt = 0;
bool stopwatchRunning = false;

// The timer counts down in seconds.  It starts paused, with one minute ready.
uint32_t timerRemainingSeconds = Config::DEFAULT_TIMER_MINUTES * 60UL;
uint32_t timerLastTickAt = 0;
bool timerRunning = false;
bool timerTimeUp = false;

// A button that has dismissed an alarm ignores its hold/release events.  Thus,
// holding Top to silence an alarm cannot accidentally open Stopwatch mode.
bool ignoreUntilRelease[3] = {false, false, false};

uint32_t feedbackBeepStartedAt = 0;
bool feedbackBeepActive = false;
uint32_t startupScreenStartedAt = 0;
uint32_t lastDisplayUpdateAt = 0;

void render(bool force = false);

void requestFeedbackBeep() {
  feedbackBeepActive = true;
  feedbackBeepStartedAt = millis();
}

void goHome(bool resetTarget) {
  mode = AppMode::HOME;
  countedWhistles = 0;
  whistleDetector.reset();
  if (resetTarget) targetWhistles = 1;
  render(true);
}

void beginListening() {
  mode = AppMode::LISTENING;
  countedWhistles = 0;
  whistleDetector.reset();
  NetworkNotifier::startSession();
  NetworkNotifier::publish("session_started", 0, targetWhistles);
  Serial.printf("Listening: target=%u threshold=%u\n", targetWhistles, whistleThreshold);
  render(true);
}

void enterThresholdSettings() {
  mode = AppMode::THRESHOLD_SETTINGS;
  thresholdDirty = false;
  render(true);
}

void leaveThresholdSettings() {
  // This is the only normal EEPROM-write location in the whole sketch.
  if (thresholdDirty && settingsStore.saveThreshold(whistleThreshold)) {
    thresholdDirty = false;
  }
  goHome(false);
}

uint32_t stopwatchElapsedMs() {
  if (!stopwatchRunning) return stopwatchAccumulatedMs;
  return stopwatchAccumulatedMs + (millis() - stopwatchStartedAt);
}

void toggleStopwatch() {
  if (stopwatchRunning) {
    stopwatchAccumulatedMs = stopwatchElapsedMs();
    stopwatchRunning = false;
  } else {
    stopwatchStartedAt = millis();
    stopwatchRunning = true;
  }
}

void resetStopwatch() {
  stopwatchAccumulatedMs = 0;
  if (stopwatchRunning) stopwatchStartedAt = millis();
}

void adjustTimerMinutes(int8_t direction) {
  // Adjusting while running changes the remaining time immediately, which is
  // useful when the user needs to add or remove one minute mid-countdown.
  if (direction > 0 && timerRemainingSeconds <=
                           (Config::MAX_TIMER_MINUTES - 1UL) * 60UL) {
    timerRemainingSeconds += 60;
  } else if (direction < 0) {
    timerRemainingSeconds = timerRemainingSeconds >= 60
                                ? timerRemainingSeconds - 60
                                : 0;
  }
  timerTimeUp = false;
  if (timerRunning) timerLastTickAt = millis();
}

void toggleTimer() {
  if (timerTimeUp) {
    // The first Bottom press after time-up simply acknowledges the alarm.
    timerTimeUp = false;
    return;
  }
  if (timerRemainingSeconds == 0) return;
  timerRunning = !timerRunning;
  timerLastTickAt = millis();
}

void enterStopwatch() {
  mode = AppMode::STOPWATCH;
  render(true);
}

void enterTimer() {
  mode = AppMode::TIMER;
  render(true);
}

void serviceTimer() {
  if (mode != AppMode::TIMER || !timerRunning) return;

  const uint32_t now = millis();
  const uint32_t elapsedSeconds = (now - timerLastTickAt) / 1000UL;
  if (elapsedSeconds == 0) return;

  timerLastTickAt += elapsedSeconds * 1000UL;
  if (elapsedSeconds >= timerRemainingSeconds) {
    timerRemainingSeconds = 0;
    timerRunning = false;
    timerTimeUp = true;
    Serial.println("Timer reached zero.");
    NetworkNotifier::publish("timer_completed", countedWhistles, targetWhistles);
  } else {
    timerRemainingSeconds -= elapsedSeconds;
  }
}

void serviceWhistleCounter() {
  if (mode != AppMode::LISTENING) return;
  if (!whistleDetector.update(whistleThreshold)) return;

  ++countedWhistles;
  NetworkNotifier::publish("whistle", countedWhistles, targetWhistles);
  Serial.printf("Whistle %u/%u\n", countedWhistles, targetWhistles);
  if (countedWhistles >= targetWhistles) {
    countedWhistles = targetWhistles;
    mode = AppMode::WHISTLE_ALARM;
    feedbackBeepActive = false;
    Serial.println("Whistle target reached.");
    NetworkNotifier::publish("completed", countedWhistles, targetWhistles);
  }
  render(true);
}

void serviceBuzzer() {
  const uint32_t now = millis();
  const bool alertActive = mode == AppMode::WHISTLE_ALARM ||
                           (mode == AppMode::TIMER && timerTimeUp);

  if (alertActive) {
    // 500 ms ON, 500 ms OFF for both the whistle completion and timer alert.
    const bool on = (now / Config::ALARM_INTERVAL_MS) % 2 == 0;
    digitalWrite(Config::BUZZER_PIN, on ? HIGH : LOW);
  } else if (feedbackBeepActive &&
             now - feedbackBeepStartedAt < Config::FEEDBACK_BEEP_MS) {
    digitalWrite(Config::BUZZER_PIN, HIGH);
  } else {
    feedbackBeepActive = false;
    digitalWrite(Config::BUZZER_PIN, LOW);
  }
}

void handleLongPress(ButtonId id) {
  if (mode == AppMode::HOME) {
    if (id == TOP) enterStopwatch();
    if (id == MIDDLE) enterTimer();
    // Bottom keeps its original whistle-counter role: a one-second hold opens
    // the threshold screen.  Its short-release action still starts listening.
    if (id == BOTTOM) enterThresholdSettings();
  } else if (mode == AppMode::LISTENING && id == BOTTOM) {
    // Cancel a listening session but retain the chosen target number.
    NetworkNotifier::publish("cancelled", countedWhistles, targetWhistles);
    goHome(false);
  } else if (mode == AppMode::THRESHOLD_SETTINGS && id == BOTTOM) {
    leaveThresholdSettings();
  } else if ((mode == AppMode::STOPWATCH || mode == AppMode::TIMER) && id == TOP) {
    // Utility modes always return to the regular whistle-counter Home screen.
    goHome(false);
  }
}

void handleShortRelease(ButtonId id) {
  switch (mode) {
    case AppMode::HOME:
      if (id == TOP && targetWhistles < 999) ++targetWhistles;
      if (id == MIDDLE && targetWhistles > 1) --targetWhistles;
      if (id == BOTTOM) beginListening();
      break;

    case AppMode::THRESHOLD_SETTINGS:
      if (id == TOP && whistleThreshold + Config::THRESHOLD_STEP <=
                           Config::MAX_START_THRESHOLD) {
        whistleThreshold += Config::THRESHOLD_STEP;
        thresholdDirty = true;
      }
      if (id == MIDDLE && whistleThreshold >=
                              Config::MIN_START_THRESHOLD + Config::THRESHOLD_STEP) {
        whistleThreshold -= Config::THRESHOLD_STEP;
        thresholdDirty = true;
      }
      break;

    case AppMode::STOPWATCH:
      if (id == TOP) toggleStopwatch();
      if (id == MIDDLE) resetStopwatch();
      break;

    case AppMode::TIMER:
      if (id == TOP) adjustTimerMinutes(+1);
      if (id == MIDDLE) adjustTimerMinutes(-1);
      if (id == BOTTOM) toggleTimer();
      break;

    case AppMode::LISTENING:
    case AppMode::WHISTLE_ALARM:
      // Listening has no short button actions; the bottom long press cancels.
      break;
  }
  render(true);
}

void handleButtonEvent(ButtonId id, ButtonEvent event) {
  if (event == ButtonEvent::NONE) return;

  if (event == ButtonEvent::PRESSED) {
    requestFeedbackBeep();

    if (mode == AppMode::WHISTLE_ALARM) {
      // Any press resets the whistle alarm to a clean target of one.
      goHome(true);
      ignoreUntilRelease[id] = true;
    } else if (mode == AppMode::TIMER && timerTimeUp) {
      // Any press silences a completed timer without changing its time.
      timerTimeUp = false;
      ignoreUntilRelease[id] = true;
      render(true);
    }
    return;
  }

  if (ignoreUntilRelease[id]) {
    if (event == ButtonEvent::RELEASED_SHORT || event == ButtonEvent::RELEASED_LONG) {
      ignoreUntilRelease[id] = false;
    }
    return;
  }

  if (event == ButtonEvent::LONG_PRESSED) {
    handleLongPress(id);
  } else if (event == ButtonEvent::RELEASED_SHORT) {
    handleShortRelease(id);
  }
}

void render(bool force) {
  const uint32_t now = millis();
  if (!force && now - lastDisplayUpdateAt < Config::LCD_REFRESH_MS) return;
  lastDisplayUpdateAt = now;

  if (now - startupScreenStartedAt < Config::STARTUP_SCREEN_MS) {
    display.showSplash();
    return;
  }

  switch (mode) {
    case AppMode::HOME:
      if (now % 5000 < 1000) {
        display.showNetworkStatus(NetworkNotifier::wifiConnected(), NetworkNotifier::deliveryFault());
      } else {
        display.showHome(targetWhistles);
      }
      break;
    case AppMode::LISTENING:
      display.showListening(countedWhistles, targetWhistles,
                            whistleDetector.filteredEnvelope(), whistleThreshold);
      break;
    case AppMode::THRESHOLD_SETTINGS:
      display.showThresholdSettings(whistleThreshold);
      break;
    case AppMode::WHISTLE_ALARM:
      display.showWhistleAlarm(countedWhistles, targetWhistles,
                               (now / Config::ALARM_INTERVAL_MS) % 2 == 0);
      break;
    case AppMode::STOPWATCH:
      display.showStopwatch(stopwatchElapsedMs(), stopwatchRunning);
      break;
    case AppMode::TIMER:
      display.showTimer(timerRemainingSeconds, timerRunning, timerTimeUp);
      break;
  }
}

void setup() {
  Serial.begin(115200);
  Serial.println();
  Serial.println("Multi-mode whistle counter booting.");

  pinMode(Config::BUZZER_PIN, OUTPUT);
  digitalWrite(Config::BUZZER_PIN, LOW);
  for (Button &button : buttons) button.begin();

  display.begin();
  whistleThreshold = settingsStore.loadThreshold();
  pinMode(Config::SOUND_PIN, INPUT);
  whistleDetector.begin();
  NetworkNotifier::begin();
  NetworkNotifier::setDisplayAddress(display.address());
  startupScreenStartedAt = millis();
  render(true);
}

void loop() {
  const uint32_t now = millis();
  for (uint8_t i = 0; i < 3; ++i) {
    handleButtonEvent(static_cast<ButtonId>(i), buttons[i].update(now));
  }

  serviceWhistleCounter();
  serviceTimer();
  serviceBuzzer();
  render(false);
  static const char *modeNames[] = {"home", "listening", "settings", "alarm", "stopwatch", "timer"};
  NetworkNotifier::setState(modeNames[static_cast<uint8_t>(mode)], countedWhistles, targetWhistles);
  NetworkNotifier::update();
  // Yield to the ESP8266 Wi-Fi stack between sampling passes.
  delay(1);
}
