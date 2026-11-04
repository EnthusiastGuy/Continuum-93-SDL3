// Adds the cross-origin isolation headers to this site's responses, for hosts that cannot send them
// (index.html registers it only when the page is not isolated already). Needs HTTPS (or localhost).
self.addEventListener('install', function () { self.skipWaiting(); });
self.addEventListener('activate', function (e) { e.waitUntil(self.clients.claim()); });
self.addEventListener('fetch', function (e) {
  var req = e.request;
  if (req.cache === 'only-if-cached' && req.mode !== 'same-origin') return;   // a Chrome devtools quirk
  e.respondWith(fetch(req).then(function (res) {
    if (res.status === 0) return res;   // opaque: nothing to add
    var h = new Headers(res.headers);
    h.set('Cross-Origin-Opener-Policy', 'same-origin');
    h.set('Cross-Origin-Embedder-Policy', 'require-corp');
    h.set('Cross-Origin-Resource-Policy', 'cross-origin');
    return new Response(res.body, { status: res.status, statusText: res.statusText, headers: h });
  }));
});
