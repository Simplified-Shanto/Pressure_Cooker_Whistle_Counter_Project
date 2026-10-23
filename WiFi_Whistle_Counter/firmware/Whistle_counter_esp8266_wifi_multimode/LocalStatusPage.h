#pragma once
#include <Arduino.h>

const char LOCAL_STATUS_PAGE[] PROGMEM = R"HTML(<!doctype html>
<html lang="en"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width,initial-scale=1">
<title>Whistle Counter · Local status</title><style>
body{font:16px system-ui,sans-serif;color:#153b34;background:#f4f5ef;margin:0;padding:24px}
main{max-width:640px;margin:auto}h1{font-size:28px;margin-bottom:8px}p{line-height:1.6}
section{background:#fff;border:1px solid #d5ded4;border-radius:16px;padding:20px;margin:20px 0}
dl{margin:0}dl>div{display:flex;justify-content:space-between;gap:20px;padding:10px 0;border-bottom:1px solid #e8ece4}
dl>div:last-child{border:0}dt{color:#627268}dd{margin:0;text-align:right;overflow-wrap:anywhere;min-width:0}
#count{font-size:64px;font-weight:700}small{color:#627268}.error{color:#9d362a}
</style></head><body><main><small>ESP8266 · HOME WI-FI</small><h1>Whistle Counter</h1>
<p>This page is served directly by your counter. It checks Wi-Fi and shows the local count.</p>
<p id="notice" role="status">Connecting to the counter…</p>
<section><div><span id="count">—</span> <span id="target">whistles</span></div><p id="mode">Waiting for status</p></section>
<section><dl id="details"></dl></section>
<p>This local page does not send lock-screen notifications. Those use the separate HTTPS app and notification server.</p>
<p>If the LCD shows random symbols, check its wiring and power. An I2C address response alone cannot confirm readable text.</p>
<small id="updated"></small>
<script>
const el=id=>document.getElementById(id);
function row(label,value){const r=document.createElement('div'),a=document.createElement('dt'),b=document.createElement('dd');a.textContent=label;b.textContent=value;r.append(a,b);return r;}
async function refresh(){try{const response=await fetch('/status',{cache:'no-store'});if(!response.ok)throw Error('HTTP '+response.status);const s=await response.json();
el('count').textContent=s.count;el('target').textContent='of '+s.target+' target whistles';el('mode').textContent='Counter mode: '+s.mode;
el('notice').textContent='Counter reachable on your local Wi-Fi.';el('notice').className='';
el('details').replaceChildren(...[
['Board',s.board],['Wi-Fi network',s.ssid],['IP address',s.ip],['Wi-Fi signal',s.rssi_dbm+' dBm'],
['HTTPS server address set',s.https_address_set?'Yes':'No — configure ServerConfig.h'],
['Backend acknowledged recently',s.backend_reachable?'Yes':'No'],['Outbox ready',s.outbox_ready?'Yes':'No — initialize LittleFS'],
['Storage error',s.storage_error?'Yes':'No'],['LCD I2C address',s.lcd_address?'0x'+s.lcd_address.toString(16).toUpperCase():'No responder found'],
['Free memory',s.free_heap_bytes+' bytes'],['Uptime',Math.floor(s.uptime_ms/1000)+' seconds']
].map(([k,v])=>row(k,v)));el('updated').textContent='Updated '+new Date().toLocaleTimeString();
}catch(error){el('notice').textContent='Counter unreachable. Displayed values may be stale. Check power and Wi-Fi.';el('notice').className='error';}}
refresh();setInterval(refresh,2000);
</script></main></body></html>)HTML";
