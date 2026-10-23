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
  // The installed LCD previously worked at 0x27. Allow its supply to settle
  // before probing, and always attempt the configured initialization sequence
  // even if the backpack did not ACK during the initial power-up window.
  delay(250);
  Wire.begin(Config::LCD_SDA_PIN, Config::LCD_SCL_PIN);
  Wire.setClock(100000);
  Wire.setClockStretchLimit(1500);
  address_ = 0;
  for (uint8_t attempt = 0; attempt < 5; ++attempt) {
    Wire.beginTransmission(Config::LCD_ADDRESS);
    if (Wire.endTransmission() == 0) { address_ = Config::LCD_ADDRESS; break; }
    if (attempt < 4) delay(100);
  }
  if (!address_) {
    Serial.printf("LCD has not ACKed 0x%02X; attempting fixed-address initialization anyway.\n", Config::LCD_ADDRESS);
  }
  lcd_.reset(new LiquidCrystal_I2C(Config::LCD_ADDRESS, 16, 2));
  delay(100);
  lcd_->init();
  // Some library implementations call Wire.begin() internally.
  Wire.setClock(100000);
  lcd_->backlight();
  lcd_->leftToRight();
  lcd_->noAutoscroll();
  lcd_->clear();
  Wire.beginTransmission(Config::LCD_ADDRESS);
  address_ = Wire.endTransmission() == 0 ? Config::LCD_ADDRESS : 0;
  Serial.printf("LCD configured: 0x%02X; ACK after initialization: %s (SDA GPIO%u, SCL GPIO%u)\n",
                Config::LCD_ADDRESS, address_ ? "yes" : "no", Config::LCD_SDA_PIN, Config::LCD_SCL_PIN);
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
            deliveryFault ? "Storage/key error" : "Bottom: Start");
}

void Display::showListening(uint16_t count, uint16_t target,
                            uint16_t envelope, uint16_t threshold) {
  char first[32];
  char second[32];
  snprintf(first, sizeof(first), "Whistles: %u/%u", count, target);
  snprintf(second, sizeof(second), "E:%3u T:%3u", envelope, threshold);
  showLines(first, second);
}

void Display::showDetectorSettings(uint8_t page, const DetectorSettings &settings,
                                   bool saveFailed) {
  const char *titles[] = {"1/5 Threshold", "2/5 Min sound", "3/5 End quiet", "4/5 Cooldown"};
  if (page > 3) page = 0;
  char line[32];
  if (page == 0) {
    snprintf(line, sizeof(line), "%u  Top+ Mid-", settings.threshold);
  } else {
    const uint16_t value = page == 1 ? settings.minimumDurationMs :
                           page == 2 ? settings.quietDurationMs : settings.cooldownMs;
    snprintf(line, sizeof(line), "%u.%us Top+ Mid-", value / 1000, (value % 1000) / 100);
  }
  showLines(saveFailed ? "Save failed!" : titles[page], line);
}

void Display::showAddress(const char *address) {
  showLines("5/5 WiFi address", address);
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
