/* The configuration tool moved to /bandolibre/tools/. This replaces the service
 * worker that was installed here: it drops its caches and unregisters, so
 * browsers that had the old tool stop serving it from here. */
self.addEventListener("install", () => self.skipWaiting());
self.addEventListener("activate", (event) => {
  event.waitUntil(
    caches.keys()
      .then((keys) => Promise.all(keys.map((k) => caches.delete(k))))
      .then(() => self.registration.unregister())
      .then(() => self.clients.matchAll())
      .then((clients) => clients.forEach((c) => c.navigate(c.url)))
  );
});
