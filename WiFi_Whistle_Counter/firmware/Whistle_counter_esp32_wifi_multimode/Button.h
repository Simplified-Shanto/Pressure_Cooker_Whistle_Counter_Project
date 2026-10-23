#pragma once

#include <Arduino.h>

// A released-short event means the button was released before LONG_PRESS_MS.
// Separating it from RELEASED_LONG prevents a long press from also performing
// the short-press action when the user finally releases the button.
enum class ButtonEvent : uint8_t {
  NONE,
  PRESSED,
  LONG_PRESSED,
  RELEASED_SHORT,
  RELEASED_LONG
};

class Button {
 public:
  explicit Button(uint8_t pin);

  void begin();
  ButtonEvent update(uint32_t now);

 private:
  uint8_t pin_;
  bool rawPressed_ = false;
  bool stablePressed_ = false;
  bool longPressReported_ = false;
  uint32_t rawChangedAt_ = 0;
  uint32_t pressedAt_ = 0;
};
