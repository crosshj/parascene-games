/* GitHub Pages cannot set COOP/COEP. This worker adds them and reloads once
 * so the threaded PlayStation core can use SharedArrayBuffer. */
self.addEventListener("install", () => {
  self.skipWaiting();
});

self.addEventListener("activate", (event) => {
  event.waitUntil(self.clients.claim());
});

self.addEventListener("message", (event) => {
  if (event.data === "claim") event.waitUntil(self.clients.claim());
});

/* 101, 204, 205, and 304 are not allowed to carry a body. Cloudflare's RUM
 * beacon answers 204, and wrapping it rejects the whole FetchEvent. */
const NO_BODY = new Set([101, 204, 205, 304]);

self.addEventListener("fetch", (event) => {
  if (event.request.cache === "only-if-cached" && event.request.mode !== "same-origin")
    return;
  const url = new URL(event.request.url);
  if (url.origin !== self.location.origin || url.pathname.startsWith("/cdn-cgi/"))
    return;
  event.respondWith(fetch(event.request).then((res) => {
    if (res.status === 0 || res.type === "opaque" || res.type === "opaqueredirect" || NO_BODY.has(res.status))
      return res;
    const headers = new Headers(res.headers);
    headers.set("Cross-Origin-Opener-Policy", "same-origin");
    headers.set("Cross-Origin-Embedder-Policy", "require-corp");
    headers.delete("content-length");
    headers.delete("content-encoding");
    return new Response(res.body, {
      status: res.status,
      statusText: res.statusText,
      headers
    });
  }));
});
