"""Generate a private, offline QR and setup file; never print the pairing key."""
import argparse
import ipaddress
from pathlib import Path
import re
from urllib.parse import urlencode

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--device', required=True, help='Six hexadecimal characters from the counter chip ID')
parser.add_argument('--host', default='', help='Optional current private IPv4 address; discovery follows later changes')
args = parser.parse_args()
device = args.device.lower()
if not re.fullmatch(r'[a-f0-9]{6}', device):
    parser.error('Device ID must be six hexadecimal characters.')
if args.host:
    try:
        address = ipaddress.IPv4Address(args.host)
        if not any(address in ipaddress.IPv4Network(net) for net in ['10.0.0.0/8', '172.16.0.0/12', '192.168.0.0/16']):
            raise ValueError()
    except ValueError:
        parser.error('Use a private home Wi-Fi IPv4 address.')
root = Path(__file__).resolve().parents[1]
header = root / 'firmware/Whistle_counter_esp8266_native_multimode/NativeSecrets.h'
match = re.search(r'NATIVE_PAIRING_KEY\[\]\s*=\s*"([a-f0-9]{64})"', header.read_text())
if not match:
    raise SystemExit('Run prepare_device.py first; existing credentials were not changed.')
payload = 'whistlecounter://pair?' + urlencode({'version': '1', 'key': match[1], 'device': device, 'host': args.host})
folder = root / '.local'
folder.mkdir(mode=0o700, exist_ok=True)
setup = folder / 'Whistle-Counter-Setup.txt'
setup.write_text(payload + '\n'); setup.chmod(0o600)
print('Private setup file ready:', setup)
try:
    import qrcode
    from qrcode.image.pure import PyPNGImage
except ImportError:
    raise SystemExit('For the QR image install tools/pairing-requirements.txt in a virtual environment, then run again.')
image = qrcode.make(payload, image_factory=PyPNGImage, box_size=10, border=4)
destination = folder / 'Whistle-Counter-Pairing.png'
with destination.open('wb') as output:
    image.save(output)
destination.chmod(0o600)
print('Private pairing QR ready:', destination)
