/* GitHub Pages cannot set COOP/COEP. This worker adds them and reloads once
 * so the threaded PlayStation core can use SharedArrayBuffer. */
self.addEventListener("install", () => {
  self.skipWaiting();
});

self.addEventListener("activate", (event) => {
  event.waitUntil(self.clients.claim());
});

self.addEventListener("fetch", (event) => {
  if (event.request.cache === "only-if-cached" && event.request.mode !== "same-origin")
    return;
  const url = new URL(event.request.url);
  if (url.origin !== self.location.origin)
    return;
  event.respondWith(fetch(event.request).then((res) => {
    const headers = new Headers(res.headers);
    /* Safari ignores credentialless, so require-corp is what isolates the page there.
     * The decoded body must not keep the encoded length or Safari drops the response. */
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
