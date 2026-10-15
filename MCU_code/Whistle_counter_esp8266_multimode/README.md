# ESP8266 multi-mode whistle counter

This separate NodeMCU sketch combines the analogue pressure-cooker whistle counter with a stopwatch and countdown timer. The source is deliberately split by responsibility:

- `Whistle_counter_esp8266_multimode.ino` — application states and button actions
- `WhistleDetector.*` — analogue sound-envelope and valid-whistle detection
- `Button.*` — debouncing and short/long press events
- `Display.*` — LCD formatting
- `SettingsStore.*` — threshold EEPROM persistence
- `Config.h` — pins and adjustable timing/threshold constants

## Wiring

| NodeMCU label | GPIO | Connection |
|---|---:|---|
| A0 | ADC0 | Sound sensor AO |
| D1 | GPIO5 | LCD SCL |
| D2 | GPIO4 | LCD SDA |
| D6 | GPIO12 | Top button to GND |
| D3 | GPIO0 | Middle button to GND |
| D4 | GPIO2 | Bottom button to GND |
| D5 | GPIO14 | Active-buzzer input |

The buttons use internal pull-ups. Do **not** hold D3 or D4 while powering or resetting the ESP8266: GPIO0 and GPIO2 determine its boot mode.

The LCD address is `0x27`; change `Config::LCD_ADDRESS` and `Display.h` if your backpack is `0x3F`.

## Concise operating manual

### Whistle counter — Home screen

- **Top short press:** increase target whistles.
- **Middle short press:** decrease target whistles, minimum 1.
- **Bottom short press:** start listening.
- **Bottom hold 1 second:** open threshold settings.
- **Top hold 1 second:** open Stopwatch.
- **Middle hold 1 second:** open Timer.

Every button press gives a short feedback beep.

### Whistle listening and completion

- The LCD shows `Whistles: counted/target`, sound envelope `E`, and threshold `T`.
- A whistle must remain above `T` long enough, then become quiet, before it counts. This prevents brief clicks from being counted.
- **Bottom hold 1 second:** cancel listening and return Home; the target remains selected.
- On reaching the target, the LCD completion message and buzzer both alternate at 500 ms intervals.
- Press **any** button to silence completion and return Home with target reset to 1.

### Threshold settings

- **Top short press:** threshold +3.
- **Middle short press:** threshold -3.
- **Bottom hold 1 second:** save the changed threshold to EEPROM once, then return Home.

### Stopwatch

- From Home, **hold Top 1 second**.
- **Top short press:** start, pause, or resume.
- **Middle short press:** reset elapsed time to `00:00.0`.
- **Top hold 1 second:** return to whistle-counter Home.

### Timer

- From Home, **hold Middle 1 second**. It opens paused at 1:00.
- **Top short press:** add one minute.
- **Middle short press:** subtract one minute, not below zero.
- **Bottom short press:** start, pause, or resume.
- At zero, the display shows time-up and the buzzer alternates at 500 ms. Press any button to acknowledge it.
- **Top hold 1 second:** return to whistle-counter Home.

## Build

Open this folder as a PlatformIO project and run `pio run`, or select **NodeMCU 1.0 (ESP-12E Module)** in the Arduino IDE and open `Whistle_counter_esp8266_multimode.ino`.
