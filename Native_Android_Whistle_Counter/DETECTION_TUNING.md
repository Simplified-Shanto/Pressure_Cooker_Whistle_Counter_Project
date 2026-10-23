# Whistle timing and calibration

The native ESP8266 variant measures microphone peak-to-peak amplitude over 25 ms
windows and smooths it with a 0.30 exponential filter. It recognizes a sustained
sound burst followed by quiet; it does not identify whistle frequency. Timing
values apply to that smoothed envelope, not the raw sound waveform.

## Research and selected defaults — 23 October 2026

- [CircuitDigest's own ESP32 cooker-counter repository](https://github.com/Circuit-Digest/Smart-Cooker-Whistle-Counter-using-ESP32)
  uses a **10-second cooldown after a count** to prevent repeat counts. Its
  README lists a 100 ms minimum sound duration. The author's
  [accompanying article](https://circuitdigest.com/microcontroller-projects/smart-cooker-whistle-counter-using-esp32)
  discusses 300 ms instead. These are implementation examples, not measured
  universal cooker timings.
- The manufacturer's [Hawkins Ekobase instruction manual, printed page 10](https://www.hawkinscookers.com/cookbooks/ekobaseim_english.pdf)
  says more than **four whistles per minute** is too frequent and advises
  reducing heat for that cooker. It also describes whistle frequency changing
  with heat adjustments. It does not specify a universal whistle duration or
  guarantee a minimum gap for every cooker.

Based on those examples, the revised **engineering starting defaults** are:

| Setting | Previous | New default | Adjustable range | Step |
| --- | --- | --- | --- | --- |
| Minimum sustained sound | 180 ms | **500 ms** | 100–5,000 ms | 100 ms |
| Continuous quiet to end a whistle | 100 ms | **1,500 ms** | 100–5,000 ms | 100 ms |
| Cooldown after a count | 600 ms | **10,000 ms** | 1,000–60,000 ms | 1,000 ms |

The 500 ms and 1,500 ms choices are engineering judgments to reject brief sounds
and merge short interruptions within one steam release. They are not durations
established by the sources. The 10-second cooldown follows the published counter
example. The actual cooker, heat level, microphone placement, and background
noise still need a real cooking test.

## Adjust on the physical counter

From **Home**, hold **Bottom** for about one second. The LCD starts on
`1/5 Threshold`. Tap **Bottom** to cycle through:

1. **Threshold**: amplitude required to begin a candidate. Top adds 3; Middle
   subtracts 3, bounded at 25–900. The existing saved threshold is retained.
2. **Min sound**: sustained time at or above the start threshold.
3. **End quiet**: continuous time at or below the ending threshold (`T − 22`).
4. **Cooldown**: time after an emitted count before another burst can qualify.
5. **WiFi address**: the current IPv4 address, or `Not connected`. This page is
   read-only and refreshes live; Top/Middle leave the detection settings unchanged.

On every page, **Top increases** and **Middle decreases** the selected value.
Timing values are displayed in seconds with one decimal place, such as `1.5s`.
Hold **Bottom** again, from any page, to **save all changes and return Home**.
If saving fails, `Save failed!` stays on the screen; hold Bottom to retry.
An ordinary reset before saving discards edits. Settings cannot be edited while
listening; cancel the session before adjusting them.

Defaults are in `firmware/Whistle_counter_esp8266_native_multimode/Config.h`.
Saved settings override source defaults. Threshold data keeps its original
EEPROM format, and timing uses a separate versioned record with CRC validation.
On first upgrade, the saved threshold is loaded and new timing defaults are used
without writing at boot. No filesystem initializer or formatting is needed.

## Exact counting behavior and tradeoffs

The first whistle can qualify immediately after starting a session. Once its
envelope stays above the start threshold for the minimum sound time, it is active.
An active whistle is counted only after continuous ending quiet. A shorter quiet
gap resets the quiet timer and keeps the same whistle active.

The cooldown starts **when the count is emitted**, after the ending quiet has
already elapsed. It does not delay that count's notification or completion alarm.
There is nevertheless an intentional approximately 1.5-second ending-quiet delay
plus filter/sampling latency before the default count is emitted. Next-whistle
acceptance resumes only when both the cooldown has expired and the signal has
been quiet for the configured ending-quiet time. A burst beginning during
cooldown and continuing beyond its expiry is ignored as a whole, rather than
counted halfway through. There are no blocking `delay()` calls for these timers;
buttons, Wi-Fi polling, and alarms continue being serviced.

- If one whistle counts multiple times, increase ending quiet or cooldown.
- If separate real whistles are ignored because they arrive too soon, decrease
  cooldown. If a quiet gap between distinct whistles is shorter than ending quiet,
  decrease ending quiet as well.
- If short real whistles are missed, decrease minimum sound time. A longer value
  still requires the smoothed signal to remain above the start threshold
  continuously; it can reject a weak, fluctuating whistle.
- If quiet steam hiss never drops below the ending threshold, the count waits.
  Increasing timing does not fix this; adjust the amplitude threshold/placement.
- Longer timings cannot distinguish an equally loud, sustained non-whistle noise.

Read current settings at `http://<counter-ip>/status`, under `detector`:
`threshold`, `minimum_sound_ms`, `ending_quiet_ms`, and `cooldown_ms`. This endpoint
is read-only. The existing Android app needs no update for these settings.

## Validation

Run `python3 tools/test_firmware.py` with Python 3 and g++ installed. The host
tests compile the actual detector and settings code with simulated 5 ms ADC
samples, clock, and EEPROM. They exercise burst separation, brief lulls, startup,
runtime timing values, cooldown expiry during a burst, resets, clock wraparound,
legacy threshold migration, persistence, invalid settings, CRC fallback, and
reported commit failure. These tests do not replace real acoustic calibration.
