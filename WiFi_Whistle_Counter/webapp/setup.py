"""Generate private setup files once; do not overwrite keys on subsequent runs."""
import argparse
import base64
import json
import secrets
from pathlib import Path
from urllib.parse import urlparse

from cryptography.hazmat.primitives import serialization
from cryptography.hazmat.primitives.asymmetric import ec

ROOT = Path(__file__).resolve().parent


def private_write(path, content):
    path.write_text(content)
    path.chmod(0o600)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--origin', required=True, help='Public HTTPS origin, e.g. https://counter.example.com')
    parser.add_argument('--contact', required=True, help='VAPID mailto: address or HTTPS contact URL')
    parser.add_argument('--ca-file', type=Path, help='PEM root CA trusted by the counter for your HTTPS origin')
    parser.add_argument('--board', choices=['esp8266', 'esp32'], default='esp8266',
                        help='Firmware target; defaults to the installed Generic ESP8266 Module')
    args = parser.parse_args()
    origin = args.origin.rstrip('/')
    url = urlparse(origin)
    local = url.scheme == 'http' and url.hostname in {'localhost', '127.0.0.1'}
    if not url.netloc or url.path or url.query or url.fragment or url.username is not None or (url.scheme != 'https' and not local):
        parser.error('Use an HTTPS origin with no path, or HTTP localhost for development')
    if not (args.contact.startswith('mailto:') or args.contact.startswith('https://')):
        parser.error('Contact must begin with mailto: or https://')
    ca = args.ca_file.read_text() if args.ca_file else 'REPLACE_WITH_YOUR_SERVER_ROOT_CA_PEM'
    if args.ca_file:
        from cryptography import x509
        try:
            cert = x509.load_pem_x509_certificate(ca.encode())
            if not cert.extensions.get_extension_for_class(x509.BasicConstraints).value.ca:
                parser.error('--ca-file must contain a CA certificate, not a server leaf certificate')
        except (ValueError, x509.ExtensionNotFound):
            parser.error('--ca-file must contain a valid PEM CA certificate')
    folder = ROOT / '.local'
    folder.mkdir(mode=0o700, exist_ok=True)
    config_path = folder / 'config.json'
    if config_path.exists():
        config = json.loads(config_path.read_text())
        if not (folder / 'vapid.pem').exists():
            parser.error('Existing setup is missing vapid.pem; restore it from backup')
    else:
        key = ec.generate_private_key(ec.SECP256R1())
        pem = key.private_bytes(serialization.Encoding.PEM, serialization.PrivateFormat.PKCS8,
                                serialization.NoEncryption()).decode()
        public = key.public_key().public_bytes(serialization.Encoding.X962, serialization.PublicFormat.UncompressedPoint)
        config = {
            'session_secret': secrets.token_urlsafe(48), 'device_token': secrets.token_urlsafe(32),
            'pairing_key': secrets.token_urlsafe(24),
            'vapid_public_key': base64.urlsafe_b64encode(public).rstrip(b'=').decode()}
        private_write(folder / 'vapid.pem', pem)
    config.update(public_origin=origin, vapid_subject=args.contact)
    private_write(config_path, json.dumps(config, indent=2) + '\n')
    private_write(folder / 'phone-pairing.txt', 'Enter this key in the phone app:\n' + config['pairing_key'] + '\n')
    # JSON quoting safely produces these ordinary C++ string literals.
    header = ('#pragma once\n#include <Arduino.h>\n'
              f'constexpr char COUNTER_SERVER[] = {json.dumps(origin)};\n'
              f'constexpr char COUNTER_DEVICE_TOKEN[] = {json.dumps(config["device_token"])};\n'
              f'const char COUNTER_ROOT_CA[] PROGMEM = R"PEM({ca})PEM";\n')
    firmware = ROOT.parent / f'firmware/Whistle_counter_{args.board}_wifi_multimode'
    firmware.mkdir(parents=True, exist_ok=True)
    private_write(firmware / 'ServerConfig.h', header)
    private_write(ROOT / '.env', f'COUNTER_DOMAIN={url.netloc}\n')
    print('Setup files generated. Secrets were not printed.')
    print('Phone pairing key: webapp/.local/phone-pairing.txt')
    print(f'Firmware target: {args.board}')
    if not args.ca_file:
        print('Before flashing: rerun with --ca-file containing your HTTPS server root CA.')


if __name__ == '__main__':
    main()
