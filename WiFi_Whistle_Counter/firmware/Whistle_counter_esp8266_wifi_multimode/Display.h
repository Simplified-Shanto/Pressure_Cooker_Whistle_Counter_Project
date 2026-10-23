#pragma once

#include <Arduino.h>
#include <LiquidCrystal_I2C.h>
#include <memory>

#include "Config.h"

// Keeps all LCD formatting in one place.  It also remembers the previous two
// lines, avoiding unnecessary I2C traffic and visible display flicker.
class Display {
 public:
  void begin();
  uint8_t address() const { return address_; }
  void showSplash();
  void showHome(uint16_t target);
  void showNetworkStatus(bool connected, bool deliveryFault);
  void showListening(uint16_t count, uint16_t target, uint16_t envelope,
                     uint16_t threshold);
  void showThresholdSettings(uint16_t threshold);
  void showWhistleAlarm(uint16_t count, uint16_t target, bool visible);
  void showStopwatch(uint32_t elapsedMs, bool running);
  void showTimer(uint32_t remainingSeconds, bool running, bool timeUp);

 private:
  void showLines(const char *first, const char *second);

  std::unique_ptr<LiquidCrystal_I2C> lcd_;
  uint8_t address_ = 0;
  char previousFirst_[17] = "";
  char previousSecond_[17] = "";
};
