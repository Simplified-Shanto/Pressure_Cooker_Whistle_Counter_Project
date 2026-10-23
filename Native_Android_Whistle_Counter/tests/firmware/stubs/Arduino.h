#pragma once
#include <cstddef>
#include <cstdint>

constexpr uint8_t A0 = 0;
uint32_t millis();
uint32_t micros();
int analogRead(uint8_t pin);

struct SerialStub {
  template <typename... Args> void printf(const char *, Args...) {}
  void println(const char *) {}
};
extern SerialStub Serial;
