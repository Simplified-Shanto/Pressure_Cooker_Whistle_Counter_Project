#pragma once

#include <Arduino.h>

// Hardware and timing values shared by the small source files in this sketch.
namespace Config {

// Original ESP32 DevKit / ESP-WROOM-32. GPIO34 is ADC1, usable with Wi-Fi.
constexpr uint8_t SOUND_PIN = 34;
constexpr uint8_t LCD_SCL_PIN = 22;
constexpr uint8_t LCD_SDA_PIN = 21;
constexpr uint8_t TOP_BUTTON_PIN = 25;
constexpr uint8_t MIDDLE_BUTTON_PIN = 26;
constexpr uint8_t BOTTOM_BUTTON_PIN = 33;
constexpr uint8_t BUZZER_PIN = 27;
constexpr uint8_t LCD_ADDRESS = 0x3F;

// Button and user-interface timing.
constexpr uint16_t BUTTON_DEBOUNCE_MS = 30;
constexpr uint16_t LONG_PRESS_MS = 1000;
constexpr uint16_t FEEDBACK_BEEP_MS = 55;
constexpr uint16_t LCD_REFRESH_MS = 100;
constexpr uint16_t ALARM_INTERVAL_MS = 500;
constexpr uint16_t STARTUP_SCREEN_MS = 900;

// Analogue whistle detector settings.
constexpr uint32_t SAMPLE_INTERVAL_US = 1000;
constexpr uint16_t ENVELOPE_WINDOW_MS = 25;
constexpr float ENVELOPE_SMOOTHING = 0.30f;
constexpr uint16_t DEFAULT_START_THRESHOLD = 65;
constexpr uint16_t MIN_START_THRESHOLD = 25;
constexpr uint16_t MAX_START_THRESHOLD = 900;
constexpr uint8_t THRESHOLD_STEP = 3;
constexpr uint16_t STOP_HYSTERESIS = 22;
constexpr uint16_t WHISTLE_MIN_DURATION_MS = 180;
constexpr uint16_t WHISTLE_END_DURATION_MS = 100;
constexpr uint16_t WHISTLE_MIN_GAP_MS = 600;

// Normalize ESP32 ADC samples to 10 bits to keep the detector's threshold scale.
// Recalibrate the threshold with real whistles after changing boards.
// EEPROM compatibility storage is provided by the ESP32 Arduino core.
constexpr size_t EEPROM_BYTES = 32;
constexpr uint32_t EEPROM_MAGIC = 0x57434E54UL; // "WCNT"

// Timer limits. 999 minutes is large enough for this 16x2 interface while
// keeping arithmetic safely inside uint32_t seconds.
constexpr uint16_t DEFAULT_TIMER_MINUTES = 1;
constexpr uint16_t MAX_TIMER_MINUTES = 999;

} // namespace Config
