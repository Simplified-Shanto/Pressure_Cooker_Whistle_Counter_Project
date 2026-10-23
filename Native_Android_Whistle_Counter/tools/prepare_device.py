"""Create the native alternative's local pairing key without changing the web setup."""
import pathlib
import re
import secrets

ROOT = pathlib.Path(__file__).resolve().parents[1]
header = ROOT / 'firmware/Whistle_counter_esp8266_native_multimode/NativeSecrets.h'
if header.exists():
    match = re.search(r'NATIVE_PAIRING_KEY\[\]\s*=\s*"([a-f0-9]{64})"', header.read_text())
    if not match:
        raise SystemExit('Existing NativeSecrets.h has no valid key; preserve it and correct it manually.')
    key = match[1]
else:
    key = secrets.token_hex(32)
    header.write_text('#pragma once\nconstexpr char NATIVE_PAIRING_KEY[] = "' + key + '";\n')
    header.chmod(0o600)
folder = ROOT / '.local'
folder.mkdir(mode=0o700, exist_ok=True)
pairing = folder / 'phone-pairing.txt'
pairing.write_text('Native Android pairing key (paste into the native app):\n' + key + '\n')
pairing.chmod(0o600)
print('Native pairing key ready in ' + str(pairing) + '; no secrets printed.')
