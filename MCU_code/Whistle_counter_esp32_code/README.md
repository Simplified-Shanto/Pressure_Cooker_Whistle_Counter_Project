# ESP32 pressure-cooker whistle counter

This project counts periodic pressure-cooker whistles with the **analogue (`AO`) output** of the microphone module shown, rather than its `DO` comparator output. It measures the microphone signal's peak-to-peak amplitude in 25 ms windows, smooths that amplitude, then treats it as a whistle only when it remains loud for at least 180 ms. A quiet period is required before another whistle can be counted.

## Wiring

| Part | ESP32 DevKit pin |
| --- | --- |
| Sound module `AO` | GPIO 34 |
| Sound module `GND` | GND |
| Sound module `VCC` | 3V3 |
| LCD `SDA` | GPIO 21 |
| LCD `SCL` | GPIO 22 |
| LCD `GND` | GND |
| LCD `VCC` | 3V3 |
| Target/start button | GPIO 25 |
| Count-decrement button | GPIO 26 |
| Active buzzer `+` | GPIO 27 |
| Both button other terminals, buzzer `−` | GND |

Do not connect the module's `DO` pin. Power the microphone module from **3.3 V**, never 5 V: its analogue output must not exceed the ESP32 ADC's 3.3 V limit. Likewise, use a 3.3 V-compatible LCD backpack, or add an I2C level shifter if the LCD requires 5 V; 5 V I2C pull-ups can damage the ESP32 pins.

The LCD address is set to **`0x3F`**, confirmed with the attached display. The buzzer code assumes an **active buzzer**. If you have a passive piezo buzzer, it needs a tone/PWM signal instead of the continuous `HIGH` output used here.

## Controls

Both buttons use the ESP32's internal pull-up resistors, so each button connects its GPIO pin to GND when pressed.

1. After startup, make short presses of the **target/start** button to increase the target whistle count.
2. Hold that same button for at least one second to reset the current whistle count and engage detection.
3. While the counter is running, make a short press of the **count-decrement** button to remove one count if a false whistle was detected.
4. When the count reaches the target, the active buzzer stays on. Press either button to acknowledge the alarm: it turns the buzzer off, resets the current count to zero, and keeps the configured target ready for the next run. Hold the target/start button again to begin that run.

## Upload and serial logs from VS Code

1. Install the official **PlatformIO IDE** extension in VS Code, then open the `Pressure_Cooker_Whistle_Counter` folder itself (not only the repository root).
2. Connect the ESP32 with a data-capable USB cable. On Linux, ensure your user has permission to access the serial device, commonly `/dev/ttyUSB0` or `/dev/ttyACM0`.
3. Click **PlatformIO: Upload** in the VS Code status bar, or run `PlatformIO: Upload` from the Command Palette. The supplied `platformio.ini` selects the ESP32 DevKit board, Arduino framework, and LCD library.
4. Click **PlatformIO: Serial Monitor** (or run `PlatformIO: Monitor`). Its baud rate is already configured to 115200, matching `Serial.begin(115200)`.

The first build downloads the ESP32 framework and `LiquidCrystal_I2C` automatically. If upload cannot find the board, close the Serial Monitor and choose the correct USB serial port in PlatformIO's device list; hold the ESP32 **BOOT** button during the start of upload only if your particular DevKit needs it.

## Calibrating it

1. Start with the cooker quiet and open Serial Monitor at 115200 baud. Note the `raw`/`filtered` envelope values.
2. Make a real whistle at the intended microphone position and note its filtered values.
3. Set `START_THRESHOLD` in the sketch comfortably above the normal kitchen-noise value and below whistle values. Set `STOP_THRESHOLD` lower than `START_THRESHOLD` so a valid whistle does not flicker between states.
4. Adjust the module's gain potentiometer only enough to make whistles distinct from background noise. It is not used as the digital trigger.

The defaults are an initial starting point, not universal values: distance, cooker type, room acoustics, and module gain all matter. If short sounds are counted, increase `WHISTLE_MIN_MS`; if a single whistle is counted twice, increase `WHISTLE_END_MS` or `MIN_GAP_BETWEEN_WHISTLES_MS`.

The LCD displays the count, detector state, and current smoothed envelope to make setup possible without the serial monitor.
