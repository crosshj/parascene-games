/* GitHub Pages cannot set COOP/COEP. This worker adds them and reloads once
 * so the threaded PlayStation core can use SharedArrayBuffer. */
self.addEventListener("install", () => {
  self.skipWaiting();
});

self.addEventListener("activate", (event) => {
  event.waitUntil(self.clients.claim());
});

self.addEventListener("fetch", (event) => {
  const url = new URL(event.request.url);
  if (url.origin !== self.location.origin)
    return;
  event.respondWith(fetch(event.request).then((res) => {
    const headers = new Headers(res.headers);
    headers.set("Cross-Origin-Opener-Policy", "same-origin");
    headers.set("Cross-Origin-Embedder-Policy", "credentialless");
    return new Response(res.body, {
      status: res.status,
      statusText: res.statusText,
      headers
    });
  }));
});
