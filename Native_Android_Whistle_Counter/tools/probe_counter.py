"""Read an authenticated native counter snapshot without changing the device."""
import argparse
import hashlib
import hmac
import ipaddress
import json
import pathlib
import re
import secrets
import urllib.error
import urllib.request

parser = argparse.ArgumentParser(description=__doc__)
parser.add_argument('--host', required=True, help='Counter local IPv4 address, without http://')
parser.add_argument('--key-file', type=pathlib.Path,
                    default=pathlib.Path(__file__).resolve().parents[1] / '.local/phone-pairing.txt')
args = parser.parse_args()
try:
    host = ipaddress.IPv4Address(args.host)
except ValueError:
    parser.error('Use the counter IPv4 address.')
if not any(host in ipaddress.IPv4Network(net) for net in ['10.0.0.0/8', '172.16.0.0/12', '192.168.0.0/16']):
    parser.error('Use a private home Wi-Fi address.')
match = re.search(r'^[a-fA-F0-9]{64}$', args.key_file.read_text(), re.MULTILINE)
if not match:
    parser.error('The selected key file has no valid native pairing key.')
key = bytes.fromhex(match[0])
nonce = secrets.token_hex(16)
request_mac = hmac.new(key, (nonce + '\n\n0').encode(), hashlib.sha256).hexdigest()
request = urllib.request.Request(f'http://{host}/native/v1/state?nonce={nonce}&boot=&after=0',
                                 headers={'X-Counter-Request': request_mac})
try:
    with urllib.request.urlopen(request, timeout=6) as response:
        body = response.read(32769)
        if len(body) > 32768:
            raise SystemExit('Counter response was too large.')
        expected = hmac.new(key, body, hashlib.sha256).hexdigest()
        if not hmac.compare_digest(expected, response.headers.get('X-Counter-Mac', '')):
            raise SystemExit('Response authentication failed; no state trusted.')
    snapshot = json.loads(body)
    if snapshot.get('nonce') != nonce or snapshot.get('protocol') != 1:
        raise SystemExit('Response is stale or incompatible.')
    print('Authenticated counter response; pairing key was not transmitted.')
    print(json.dumps(snapshot, indent=2))
except urllib.error.HTTPError as error:
    if error.code == 404:
        raise SystemExit('Native firmware is not installed at this IP. The existing web firmware has a different API.')
    if error.code == 401:
        raise SystemExit('Pairing key does not match this counter.')
    raise SystemExit(f'Counter HTTP error: {error.code}')
