'use strict';
const CACHE = 'whistle-shell-v1';
const SHELL = ['/', '/app.js', '/style.css', '/manifest.webmanifest', '/icons/icon-192.png', '/icons/icon-512.png', '/icons/apple-touch-icon.png'];
self.addEventListener('install', event => { event.waitUntil(caches.open(CACHE).then(cache => cache.addAll(SHELL))); self.skipWaiting(); });
self.addEventListener('activate', event => {
  event.waitUntil(Promise.all([caches.keys().then(keys => Promise.all(keys.filter(key => key !== CACHE).map(key => caches.delete(key)))), self.clients.claim()]));
});
self.addEventListener('fetch', event => {
  const url = new URL(event.request.url);
  if (event.request.method !== 'GET' || url.origin !== self.location.origin || url.pathname.startsWith('/api/')) return;
  if (event.request.mode === 'navigate') {
    event.respondWith(fetch(event.request).catch(() => caches.match('/')));
  } else if (SHELL.includes(url.pathname)) {
    event.respondWith(caches.match(event.request).then(cached => cached || fetch(event.request)));
  }
});
self.addEventListener('push', event => {
  let payload;
  try { payload = event.data.json(); } catch { payload = {title: 'Whistle Counter', body: 'Open the app for the latest counter activity.'}; }
  event.waitUntil((async () => {
    await self.registration.showNotification(payload.title || 'Whistle Counter', {
      body: payload.body || 'New counter activity.',
      icon: '/icons/icon-192.png', badge: '/icons/badge.png',
      // An event's retry replaces itself; different whistles have different tags.
      tag: payload.event_id || `counter-${Date.now()}`,
      timestamp: (payload.occurred_at || payload.received_at || Date.now() / 1000) * 1000,
      data: {url: '/', event_id: payload.event_id},
    });
    const windows = await self.clients.matchAll({type: 'window', includeUncontrolled: true});
    for (const client of windows) client.postMessage({type: 'counter-update'});
  })());
});
self.addEventListener('notificationclick', event => {
  event.notification.close();
  event.waitUntil((async () => {
    const windows = await self.clients.matchAll({type: 'window', includeUncontrolled: true});
    const existing = windows.find(client => new URL(client.url).origin === self.location.origin);
    if (existing) { await existing.focus(); return; }
    await self.clients.openWindow('/');
  })());
});
