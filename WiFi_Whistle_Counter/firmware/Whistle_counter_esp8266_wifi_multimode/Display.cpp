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
  Wire.begin(Config::LCD_SDA_PIN, Config::LCD_SCL_PIN);
  Wire.setClock(100000);
  Wire.setClockStretchLimit(1500);
  // Common PCF8574/PCF8574A backpacks use these two addresses. An ACK only
  // confirms a bus responder; it does not verify the LCD's text or pin map.
  const uint8_t candidates[] = {Config::LCD_ADDRESS, 0x27, 0x3F};
  for (uint8_t candidate : candidates) {
    Wire.beginTransmission(candidate);
    if (Wire.endTransmission() == 0) { address_ = candidate; break; }
  }
  if (!address_) {
    Serial.println(F("LCD not detected at configured address, 0x27, or 0x3F. Check GPIO4 SDA / GPIO5 SCL and power."));
    return;
  }
  Serial.printf("LCD I2C responder: 0x%02X (SDA GPIO%u, SCL GPIO%u)\n",
                address_, Config::LCD_SDA_PIN, Config::LCD_SCL_PIN);
  lcd_.reset(new LiquidCrystal_I2C(address_, 16, 2));
  delay(100);
  lcd_->init();
  // Some library implementations call Wire.begin() internally.
  Wire.setClock(100000);
  lcd_->backlight();
  lcd_->leftToRight();
  lcd_->noAutoscroll();
  lcd_->clear();
  previousFirst_[0] = previousSecond_[0] = 0;
}

void Display::showLines(const char *first, const char *second) {
  if (!lcd_) return;
  char firstLine[17];
  char secondLine[17];
  snprintf(firstLine, sizeof(firstLine), "%-16.16s", first);
  snprintf(secondLine, sizeof(secondLine), "%-16.16s", second);

  if (strcmp(firstLine, previousFirst_) != 0) {
    lcd_->setCursor(0, 0);
    lcd_->print(firstLine);
    strcpy(previousFirst_, firstLine);
  }
  if (strcmp(secondLine, previousSecond_) != 0) {
    lcd_->setCursor(0, 1);
    lcd_->print(secondLine);
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

void Display::showNetworkStatus(bool connected, bool deliveryFault) {
  showLines(connected ? "WiFi connected" : "WiFi connecting",
            deliveryFault ? "Push config/error" : "Bottom: Start");
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
