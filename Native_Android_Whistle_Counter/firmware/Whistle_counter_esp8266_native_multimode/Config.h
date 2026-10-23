#pragma once

#include <Arduino.h>

// Hardware and timing values shared by the small source files in this sketch.
namespace Config {

// Physical ESP8266 GPIO numbers are used rather than D-label aliases.  This
// lets the sketch compile with both PlatformIO's NodeMCU board and the Arduino
// IDE's generic ESP8266 board definition.
constexpr uint8_t SOUND_PIN = A0;
constexpr uint8_t LCD_SCL_PIN = 5;       // NodeMCU D1
constexpr uint8_t LCD_SDA_PIN = 4;       // NodeMCU D2
constexpr uint8_t TOP_BUTTON_PIN = 12;   // NodeMCU D6
constexpr uint8_t MIDDLE_BUTTON_PIN = 0; // NodeMCU D3
constexpr uint8_t BOTTOM_BUTTON_PIN = 2; // NodeMCU D4
constexpr uint8_t BUZZER_PIN = 14;       // NodeMCU D5
constexpr uint8_t LCD_ADDRESS = 0x27;    // Preferred; also probes 0x27 and 0x3F.

// Button and user-interface timing.
constexpr uint16_t BUTTON_DEBOUNCE_MS = 30;
constexpr uint16_t LONG_PRESS_MS = 1000;
constexpr uint16_t FEEDBACK_BEEP_MS = 55;
constexpr uint16_t LCD_REFRESH_MS = 100;
constexpr uint16_t ALARM_INTERVAL_MS = 500;
constexpr uint16_t STARTUP_SCREEN_MS = 900;

// Analogue whistle detector settings. With Wi-Fi active, ESP8266 ADC values
// may be cached for at least 5 ms. Recalibrate the envelope threshold in situ.
constexpr uint32_t SAMPLE_INTERVAL_US = 5000;
constexpr uint16_t ENVELOPE_WINDOW_MS = 25;
constexpr float ENVELOPE_SMOOTHING = 0.30f;
constexpr uint16_t DEFAULT_START_THRESHOLD = 65;
constexpr uint16_t MIN_START_THRESHOLD = 25;
constexpr uint16_t MAX_START_THRESHOLD = 900;
constexpr uint8_t THRESHOLD_STEP = 3;
constexpr uint16_t STOP_HYSTERESIS = 22;
// Starting values for cooker bursts; runtime settings override these defaults.
// See DETECTION_TUNING.md for sources, tradeoffs, and button controls.
constexpr uint16_t WHISTLE_MIN_DURATION_MS = 500;
constexpr uint16_t WHISTLE_END_DURATION_MS = 1500;
constexpr uint16_t WHISTLE_MIN_GAP_MS = 10000;
constexpr uint16_t MIN_DURATION_LIMIT_MS = 100;
constexpr uint16_t MAX_DURATION_LIMIT_MS = 5000;
constexpr uint16_t MIN_QUIET_LIMIT_MS = 100;
constexpr uint16_t MAX_QUIET_LIMIT_MS = 5000;
constexpr uint16_t MIN_COOLDOWN_LIMIT_MS = 1000;
constexpr uint16_t MAX_COOLDOWN_LIMIT_MS = 60000;
constexpr uint16_t DURATION_STEP_MS = 100;
constexpr uint16_t QUIET_STEP_MS = 100;
constexpr uint16_t COOLDOWN_STEP_MS = 1000;

// EEPROM allocates a small emulated flash sector on the ESP8266.
constexpr size_t EEPROM_BYTES = 32;
constexpr uint32_t EEPROM_MAGIC = 0x57434E54UL; // "WCNT"

// Timer limits. 999 minutes is large enough for this 16x2 interface while
// keeping arithmetic safely inside uint32_t seconds.
constexpr uint16_t DEFAULT_TIMER_MINUTES = 1;
constexpr uint16_t MAX_TIMER_MINUTES = 999;

} // namespace Config
