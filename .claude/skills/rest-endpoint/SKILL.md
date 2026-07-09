---
name: rest-endpoint
description: Add or modify a REST API endpoint in MadMapperControl (the C++ WebServer and its Vue client). Use when the task involves a new /api/... route, wiring a web action to app behaviour, or exposing app state to the web UI.
---

# Adding a REST endpoint

The WebServer is table-driven. A new endpoint touches four places; miss one and
it 404s or does nothing.

## 1. Declare the callback — `src/WebServer.h`

Add a public member of the right alias next to the others:

- read-only GET returning JSON → `JsonFetcher myThingFetcher;`
- POST taking a JSON body → `JsonHandler myThingSetter;`

(They are plain public members; there are intentionally no setter methods.)

## 2. Register the route — `src/WebServer.cpp`

Add a row to the matching static table in `APIRequestHandler::handleRequest`:

- GET → `getRoutes[]`: `{"/api/mything", &WebServer::myThingFetcher, "myThingFetcher"}`
- POST → `postRoutes[]`: `{"/api/mything", &WebServer::myThingSetter, "ok"}`

The shared handlers already do JSON dump, body parse, and error wrapping. Only
add a bespoke handler (like `/api/pages`, `/api/config`) if you need custom
validation or a non-standard response.

## 3. Bind it to ofApp — `src/ofApp.cpp` `setupWebServer()`

```cpp
webServer->myThingFetcher = [this]() { return getMyThing(); };
webServer->myThingSetter  = [this](const ofJson& body) { requestMyThing(body); };
```

**Critical:** the lambda runs on the WebServer thread. A *fetcher* may read app
state (take the relevant mutex, e.g. `oscServers.stateMutex()` or `displayMutex`).
A *setter* that changes surface/page/MIDI state must **enqueue** and let
`update()` apply it — never mutate directly. See the `concurrency-check` skill
and copy the `requestEmulatorSurface` + pending-flag-in-`update()` pattern.

## 4. Add the web client — `web/src/api.js`

```js
async fetchMyThing()   { return (await axios.get(`${API_BASE}/mything`)).data },
async setMyThing(body) { return (await axios.post(`${API_BASE}/mything`, body)).data },
```

Then call it from the relevant component (e.g. `components/mock/MockDevicePanel.vue`).

## Verify

- `make -j8 Debug`, then `curl` the route (GET for shape, POST with a body).
- `cd web && npm run build` if you touched `web/`.
- Run the `evaluate` skill; consider adding a probe for the new route to
  `scripts/eval_probe.py` if it's part of the app's contract.
