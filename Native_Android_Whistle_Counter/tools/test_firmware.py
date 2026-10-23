"""Compile and exercise the real detector/settings code with simulated ADC/time/EEPROM."""
import os
from pathlib import Path
import subprocess
import tempfile

root = Path(__file__).resolve().parents[1]
sketch = root / 'firmware/Whistle_counter_esp8266_native_multimode'
tests = root / 'tests/firmware'
with tempfile.TemporaryDirectory(prefix='whistle-firmware-tests-') as output:
    binary = Path(output) / 'detector_test'
    subprocess.run([
        os.environ.get('CXX', 'g++'), '-std=c++17', '-Wall', '-Wextra', '-Werror',
        '-fsanitize=undefined', '-fno-sanitize-recover=all', '-g',
        '-I', str(tests / 'stubs'), '-I', str(sketch),
        str(tests / 'detector_test.cpp'), str(sketch / 'WhistleDetector.cpp'),
        str(sketch / 'SettingsStore.cpp'), '-o', str(binary),
    ], check=True)
    subprocess.run([str(binary)], check=True)
