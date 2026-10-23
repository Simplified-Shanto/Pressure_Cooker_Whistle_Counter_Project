#include "Button.h"

#include "Config.h"

Button::Button(uint8_t pin) : pin_(pin) {}

void Button::begin() {
  // Each button is wired from its pin to GND.  The internal pull-up holds the
  // pin HIGH when released, so a pressed button reads LOW.
  pinMode(pin_, INPUT_PULLUP);
  rawPressed_ = digitalRead(pin_) == LOW;
  stablePressed_ = rawPressed_;
  rawChangedAt_ = millis();
}

ButtonEvent Button::update(uint32_t now) {
  const bool rawPressedNow = digitalRead(pin_) == LOW;
  if (rawPressedNow != rawPressed_) {
    rawPressed_ = rawPressedNow;
    rawChangedAt_ = now;
  }

  if (rawPressed_ != stablePressed_ &&
      now - rawChangedAt_ >= Config::BUTTON_DEBOUNCE_MS) {
    stablePressed_ = rawPressed_;
    if (stablePressed_) {
      pressedAt_ = now;
      longPressReported_ = false;
      return ButtonEvent::PRESSED;
    }

    return longPressReported_ ? ButtonEvent::RELEASED_LONG
                              : ButtonEvent::RELEASED_SHORT;
  }

  if (stablePressed_ && !longPressReported_ &&
      now - pressedAt_ >= Config::LONG_PRESS_MS) {
    longPressReported_ = true;
    return ButtonEvent::LONG_PRESSED;
  }

  return ButtonEvent::NONE;
}
