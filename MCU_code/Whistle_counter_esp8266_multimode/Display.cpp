#include "Display.h"

#include <Wire.h>

#include "Config.h"

namespace {
void formatStopwatch(uint32_t elapsedMs, char *out, size_t length) {
  const uint16_t tenths = (elapsedMs / 100) % 10;
  const uint32_t totalSeconds = elapsedMs / 1000;
  const uint16_t minutes = totalSeconds / 60;
  const uint16_t seconds = totalSeconds % 60;
  snprintf(out, length, "%02u:%02u.%u", minutes, seconds, tenths);
}

void formatTimer(uint32_t remainingSeconds, char *out, size_t length) {
  const uint16_t minutes = remainingSeconds / 60;
  const uint16_t seconds = remainingSeconds % 60;
  snprintf(out, length, "%03u:%02u", minutes, seconds);
}
} // namespace

void Display::begin() {
  Wire.begin(Config::LCD_SDA_PIN, Config::LCD_SCL_PIN); // SDA first on ESP8266.
  lcd_.init();
  lcd_.backlight();
  lcd_.clear();
}

void Display::showLines(const char *first, const char *second) {
  char firstLine[17];
  char secondLine[17];
  snprintf(firstLine, sizeof(firstLine), "%-16.16s", first);
  snprintf(secondLine, sizeof(secondLine), "%-16.16s", second);

  if (strcmp(firstLine, previousFirst_) != 0) {
    lcd_.setCursor(0, 0);
    lcd_.print(firstLine);
    strcpy(previousFirst_, firstLine);
  }
  if (strcmp(secondLine, previousSecond_) != 0) {
    lcd_.setCursor(0, 1);
    lcd_.print(secondLine);
    strcpy(previousSecond_, secondLine);
  }
}

void Display::showSplash() {
  showLines("Whistle Counter", "Starting...");
}

void Display::showHome(uint16_t target) {
  char line[20];
  snprintf(line, sizeof(line), "Target: %u", target);
  showLines(line, "Bottom: Start");
}

void Display::showListening(uint16_t count, uint16_t target,
                            uint16_t envelope, uint16_t threshold) {
  char first[32];
  char second[32];
  snprintf(first, sizeof(first), "Whistles: %u/%u", count, target);
  snprintf(second, sizeof(second), "E:%3u T:%3u", envelope, threshold);
  showLines(first, second);
}

void Display::showThresholdSettings(uint16_t threshold) {
  char line[32];
  snprintf(line, sizeof(line), "T:%u  +/- 3", threshold);
  showLines("Set threshold", line);
}

void Display::showWhistleAlarm(uint16_t count, uint16_t target, bool visible) {
  if (!visible) {
    showLines("", "");
    return;
  }
  char line[32];
  snprintf(line, sizeof(line), "%u/%u whistles", count, target);
  showLines(line, "COMPLETED");
}

void Display::showStopwatch(uint32_t elapsedMs, bool running) {
  char line[20];
  formatStopwatch(elapsedMs, line, sizeof(line));
  showLines(running ? "STOPWATCH RUN" : "STOPWATCH PAUSE", line);
}

void Display::showTimer(uint32_t remainingSeconds, bool running, bool timeUp) {
  char line[20];
  formatTimer(remainingSeconds, line, sizeof(line));
  if (timeUp) {
    showLines("TIMER: TIME UP", line);
  } else {
    showLines(running ? "TIMER RUN" : "TIMER PAUSE", line);
  }
}
