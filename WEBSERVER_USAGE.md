# Web UI & API Reference

The app serves a Vue 3 web UI and a REST/WebSocket API on **http://localhost:8080**
(reachable from any device on the network, e.g. an iPad — there is no auth, so
keep the machine on a trusted network).

## Running

```bash
make -j8 Debug && make RunDebug          # build + run
bash scripts/eval.sh                     # ground-truth check: build, launch, probe
cd web && npm run dev                    # UI dev server on :5173 (proxies /api and /ws to :8080)
cd web && npm run build                  # bundle UI into bin/data/web (required after web/ changes)
```

## Web UI tabs

- **Pages** — edit `custom_page.json` (pages/subpages, wildcard OSC paths per
  server); saving triggers a live reload in the app.
- **Endpoints** — OSCQuery server list (`settings.json`), reachability dots,
  Bonjour-discovered servers (Linux).
- **Mapping** — edit the active device profile (`device_profiles.json`):
  components, MIDI-learn, role assignment. The surface graphic is passive until
  **Learn** or **Map** is engaged.
- **Emulator** — substitutes the hardware controller. Shows the mirrored device
  display (page, parameter labels/values, cue grid with running-cue pulse),
  motorized faders that follow parameter values, and a **Mapped Controls** panel
  guaranteeing every role-bound control is reachable. Badges: `LIVE` = WebSocket
  push, `POLL` = HTTP fallback; `VIRTUAL` = no hardware, app runs a virtual
  surface (profile from `settings.emulatorProfile`, switchable per device
  button). When hardware is connected it is authoritative and the device
  selector locks.

## REST API

| Method | Path                    | Purpose |
|--------|-------------------------|---------|
| GET    | `/api/config`           | Servers (+reachability), current page, active profile, virtual flag |
| POST   | `/api/config`           | Replace server list (`{"servers":[…]}`) — applied on the main thread |
| GET    | `/api/pages`            | `custom_page.json` content |
| POST   | `/api/pages`            | Save pages and/or activate one (`{"pages":[…]}`, `{"currentPage":"…"}`) |
| GET    | `/api/parameters`       | OSCQuery parameter list per server |
| GET    | `/api/mappings`         | `mappings.json` (fixed OSC targets, nav shortcuts) + page names |
| POST   | `/api/mappings`         | Save mappings, rewire bindings live |
| GET    | `/api/profiles`         | All device profiles (active one flagged) |
| GET    | `/api/profile`          | Active device profile |
| POST   | `/api/profile`          | Save a profile (matched by `name`), rewire live |
| POST   | `/api/learn/start`      | Start MIDI learn (listens on hardware input) |
| POST   | `/api/learn/stop`       | Stop MIDI learn |
| GET    | `/api/learn/status`     | Learn state + rolling message log |
| POST   | `/api/learn/inject`     | Feed a synthetic message into the learn log |
| POST   | `/api/learn/assign`     | Write component + role into the active profile |
| GET    | `/api/display`          | Mirrored device display: page, labels[8], values[8], cue grid, profile, virtual |
| POST   | `/api/midi`             | Inject MIDI as if from hardware: `{"channel","status","control","pitch","value"}` — queued, applied on the main thread |
| POST   | `/api/emulator/surface` | Switch the virtual surface profile: `{"profile":"Faderport16"}` (virtual mode only) |

MIDI payload notes: `status` 144 = note (use `pitch` + `value` as velocity),
176 = CC (use `control`), 224 = pitch bend (`value` 0–16383). Relative encoders
expect 7-bit two's-complement deltas (+1 = `1`, −1 = `127`).

## WebSocket `/ws`

Push channel, app → browsers (the UI falls back to polling without it):

```jsonc
{ "type": "display", "data": { /* same shape as GET /api/display */ } }
{ "type": "midi", "events": [ { "src": "hw|web", "status": 176, "channel": 1,
                                "control": 71, "pitch": 0, "value": 1, "velocity": 0 } ] }
```

Display frames are sent on change (≤10 Hz) and re-sent in full to newly
connected clients. `midi` mirrors hardware and web-injected traffic — this is
how the web MIDI monitor sees the physical controller.

## Data files (`bin/data/`)

`settings.json` (servers, `emulatorProfile`, persisted `currentPage`),
`device_profiles.json`, `custom_page.json`, `mappings.json`, `subpages.json`.
**The running app writes these** (web saves, MIDI learn, page persistence);
every write keeps the previous version as `<name>.bak`. Review diffs before
committing — some are app-generated.

## Serving details

Poco HTTP server, port 8080, static files from `bin/data/web/`. `index.html`
is served with `Cache-Control: no-cache`; missing assets return 404 (never the
SPA fallback), so a plain browser reload always gets the current UI.

## Troubleshooting

- Nothing on :8080 → is the app running? `lsof -i :8080`; check log for
  "WebServer started".
- UI looks stale/broken → `cd web && npm run build`, then reload.
- Emulator inert → check the active/selected profile has role bindings
  (Mapping tab); the Mapped Controls panel lists everything that is bound.
- Everything else → `bash scripts/eval.sh` tells you which layer is broken.
