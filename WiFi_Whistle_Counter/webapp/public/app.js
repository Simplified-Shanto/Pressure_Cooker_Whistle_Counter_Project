'use strict';
const $ = id => document.getElementById(id);
let registration, subscription, pollTimer, installPrompt, serverUnavailable = false;
const message = text => { $('message').textContent = text; };

async function api(path, method = 'GET', body) {
  const response = await fetch(path, {method, credentials: 'same-origin', cache: 'no-store',
    headers: body === undefined ? {} : {'Content-Type': 'application/json'},
    body: body === undefined ? undefined : JSON.stringify(body)});
  const value = await response.json();
  if (!response.ok) {
    const error = new Error(value.error || 'Request failed');
    error.status = response.status;
    throw error;
  }
  return value;
}

function paired(value) {
  $('pairing').hidden = value;
  $('dashboard').hidden = !value;
  if (!value) clearInterval(pollTimer);
}

function render(value) {
  const device = value.device;
  const connected = Boolean(device?.online);
  $('connection').textContent = !device ? 'Waiting for counter' : connected ? 'Counter online' : 'Counter offline';
  $('connection').className = `badge ${!device ? '' : connected ? 'online' : 'offline'}`;
  $('count').textContent = device ? device.count : '—';
  $('target').textContent = device ? `of ${device.target} target whistles` : 'No counter connected';
  $('progress').max = device?.target || 1;
  $('progress').value = device?.count || 0;
  const modeTitles = {listening: 'Listening to your cooker.', alarm: 'Your whistle target is reached.',
    home: 'Ready when you are.', settings: 'Adjusting the sound threshold.',
    stopwatch: 'Stopwatch on the counter.', timer: 'Timer on the counter.'};
  $('session-title').textContent = !device ? 'Waiting for your counter.' : !connected ? 'Your counter is out of reach.' : modeTitles[device.mode];
  $('session-description').textContent = !connected && device ? 'Showing the last reported count. Check power, Wi-Fi, and internet.' :
    device?.mode === 'alarm' ? 'The counter has sounded its completion alarm.' :
    device?.mode === 'listening' ? 'Follow the count here. We will alert you when your target is reached.' :
    'Choose a target and start listening on the counter.';
  $('mode').textContent = device ? `Counter mode: ${device.mode}` : 'Awaiting first connection';
  $('last-seen').textContent = device ? `Last heartbeat ${new Date(device.last_seen * 1000).toLocaleTimeString()}` : 'No heartbeat yet';
  const d = value.delivery;
  $('delivery-status').textContent = `${d.pending || 0} alerts queued · ${d.sent || 0} accepted by push service · ${(d.failed || 0) + (d.expired || 0)} failed or expired`;
  const list = $('events');
  list.replaceChildren();
  if (!value.events.length) {
    const empty = document.createElement('li');
    empty.className = 'empty'; empty.textContent = 'Your counter activity will appear here.'; list.append(empty);
  }
  for (const event of value.events) {
    const li = document.createElement('li');
    const icon = document.createElement('span'); icon.className = 'event-icon'; icon.setAttribute('aria-hidden', 'true');
    icon.textContent = ({whistle: '♪', completed: '✓', offline: '!', restarted: '↻', connected: '↗', reconnected: '↗'})[event.type] || '·';
    const content = document.createElement('div'); content.className = 'event-content';
    const title = document.createElement('strong'); title.textContent = event.title;
    const body = document.createElement('p'); body.textContent = event.body;
    content.append(title, body);
    const time = document.createElement('time');
    const when = new Date((event.occurred_at || event.received_at) * 1000);
    time.dateTime = when.toISOString();
    time.textContent = when.toLocaleString([], {month: 'short', day: 'numeric', hour: '2-digit', minute: '2-digit'});
    li.append(icon, content, time); list.append(li);
  }
}

async function refresh() {
  try {
    const state = await api('/api/state');
    if (serverUnavailable) { message('Connection to the app server restored.'); serverUnavailable = false; }
    paired(true); render(state);
  } catch (error) {
    if (error.status === 401) paired(false);
    else {
      paired(true);
      serverUnavailable = true;
      $('connection').textContent = 'App server unreachable';
      $('connection').className = 'badge offline';
      message('Cannot reach the notification server. Displayed counts may be stale.');
    }
  }
}

async function preparePush() {
  if (!window.isSecureContext || !('serviceWorker' in navigator) || !('PushManager' in window) || !('Notification' in window)) {
    $('push-status').textContent = 'Open the HTTPS app in a supported browser. On iPhone, add it to the Home Screen first.';
    $('enable-push').disabled = true;
    return;
  }
  registration = await navigator.serviceWorker.register('/sw.js');
  await navigator.serviceWorker.ready;
  subscription = await registration.pushManager.getSubscription();
  if (subscription) {
    await api('/api/subscriptions', 'POST', subscription.toJSON());
  }
  pushControls();
}

function pushControls() {
  const enabled = Boolean(subscription);
  $('enable-push').hidden = enabled;
  $('disable-push').hidden = !enabled;
  $('test-push').disabled = !enabled;
  $('push-status').textContent = enabled ? 'Notifications are enabled on this phone, including when the screen is locked.' :
    Notification.permission === 'denied' ? 'Notifications are blocked. Allow them in browser or phone settings, then reopen the app.' :
    'Get an alert for every whistle, completion, and connection change.';
}

async function startDashboard() {
  await refresh();
  if (!$('dashboard').hidden) {
    clearInterval(pollTimer); pollTimer = setInterval(refresh, 5000);
    try { await preparePush(); } catch (error) { message(`Notification setup: ${error.message}`); }
  }
}

$('pair-form').addEventListener('submit', async event => {
  event.preventDefault();
  try {
    await api('/api/login', 'POST', {pairing_key: $('pair-key').value});
    $('pair-key').value = ''; message('Phone paired. Enable notifications to receive alerts.');
    await startDashboard();
  } catch (error) { message(error.message); }
});

$('enable-push').addEventListener('click', async () => {
  try {
    // Request permission directly from the user gesture, before any network call.
    const permission = await Notification.requestPermission();
    if (permission !== 'granted') { pushControls(); return; }
    if (!registration) throw new Error('The app is still preparing. Try again in a moment.');
    const {vapid_public_key: key} = await api('/api/config');
    const padding = '='.repeat((4 - key.length % 4) % 4);
    const bytes = Uint8Array.from(atob((key + padding).replace(/-/g, '+').replace(/_/g, '/')), c => c.charCodeAt(0));
    const candidate = await registration.pushManager.subscribe({userVisibleOnly: true, applicationServerKey: bytes});
    await api('/api/subscriptions', 'POST', candidate.toJSON());
    subscription = candidate; pushControls(); message('Notifications enabled. A confirmation alert is on its way.');
  } catch (error) { message(`Could not enable notifications: ${error.message}`); }
});

async function disablePush() {
  if (subscription) {
    await api('/api/subscriptions', 'DELETE', subscription.toJSON());
    await subscription.unsubscribe(); subscription = null;
  }
  if ('Notification' in window) pushControls();
}
$('disable-push').addEventListener('click', () => disablePush().catch(error => message(error.message)));
$('test-push').addEventListener('click', async () => {
  try {
    if (!subscription) throw new Error('Enable notifications first.');
    await api('/api/test', 'POST', subscription.toJSON());
    message('Test scheduled. Lock your phone now; the server will send the alert in 10 seconds.');
  } catch (error) { message(error.message); }
});
$('logout').addEventListener('click', async () => {
  try {
    await disablePush(); await api('/api/logout', 'POST'); paired(false); message('Phone unpaired. Notifications disabled on this phone.');
  } catch (error) { message(error.message); }
});
window.addEventListener('beforeinstallprompt', event => { event.preventDefault(); installPrompt = event; $('install').hidden = false; });
$('install').addEventListener('click', async () => { if (installPrompt) { await installPrompt.prompt(); installPrompt = null; $('install').hidden = true; } });
document.addEventListener('visibilitychange', () => { if (!document.hidden && !$('dashboard').hidden) refresh(); });
if ('serviceWorker' in navigator) navigator.serviceWorker.addEventListener('message', () => { if (!$('dashboard').hidden) refresh(); });
startDashboard();
