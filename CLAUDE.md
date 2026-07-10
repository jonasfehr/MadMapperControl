# MadMapperControl — agent guide

openFrameworks C++ app that bridges **OSCQuery ⇆ MIDI**: it drives MadMapper
(and TouchDesigner) from hardware control surfaces (Ableton Push 3, PreSonus
Faderport 16, Platform M+), and exposes a Vue 3 web UI for configuration,
mapping, and a **device emulator** that can substitute the hardware controller.

## Commands

```bash
make -j8 Debug                 # build the app (binary in bin/…app/Contents/MacOS/)
cd web && npm run build        # build the Vue UI  ->  bin/data/web  (do this after any web/ change)
cd web && npm run dev          # Vite dev server on :5173, proxies /api to :8080
node web/render_surfaces.mjs <outdir>  # SSR the emulator surface SVGs to files for visual layout review
bash scripts/eval.sh           # ground-truth evaluator: build + launch + probe + crash-check
bash scripts/eval.sh --web     # …also rebuild the web UI first
```

`eval.sh` **stops any running instance of the app** to claim port 8080 (and stops
its own on exit) — if the user is mid-session with the app, say so before running.

Runtime: the app serves the web UI + REST API on **:8080**. It runs without
MadMapper or MIDI hardware (see *Virtual surface* below), so most work is
verifiable locally. **After any change, run `bash scripts/eval.sh` — it is the
source of truth for "does it still work".** (Or invoke the `evaluate` skill.)

## Layout

- `src/ofApp.{h,cpp}` — the app. Owns pages, the active surface, page/bank
  navigation, the emulator bridge, and all the pending-work handling in `update()`.
- `src/OscServerManager.{h,cpp}` — every OSCQuery connection: server configs,
  endpoint health, Bonjour, path→server routing, primary reconnect.
- `src/CueGridBuilder.{h,cpp}` — pure JSON→`TimelineGridState` cue-grid parsing.
- `src/OscQueryJson.h` — shared OSCQuery JSON-tree helpers (`namespace oscq`).
- `src/WebServer.{h,cpp}` — REST API + static file serving (table-driven routes).
- `bin/data/*.json` — `device_profiles.json` (surfaces + role bindings),
  `settings.json` (servers, `emulatorProfile`), `custom_page.json`, `mappings.json`,
  `subpages.json`. **The running app writes these** (web UI saves, MIDI learn),
  so `git status` diffs there may be app-generated — review before committing,
  never blind `git add -A`.
- `web/src/` — Vue UI. `components/mock/` holds the per-device emulator surfaces.

Two **custom addons live in separate git repos** and are often edited alongside
the app — `../../../addons/ofxMadOscQuery` and `../../../addons/ofxMidiDevice`.
Changes there need their own commit + push.

## Rules that keep this app alive

These are load-bearing invariants learned from real crashes. Violating them
compiles fine and then segfaults at runtime.

1. **Main-thread only for surface/page/group mutation.** Building or destroying
   the surface, changing the active page/bank, and delivering MIDI all fire the
   same `ofParameter` listeners (`setActivePage` → `linkDevice`, `backToCurrent`,
   …). The WebServer and RtMidi run on other threads, so they must **enqueue**
   and let `update()` apply the work. Never call `newMidiMessage`, swap the
   surface, or change pages directly from a request handler or callback.
   (See the `concurrency-check` skill.)
2. **Re-point pages before destroying a surface.** `MadParameterPage` holds a
   raw `ofxMidiDevice*`. On a surface swap, null it on every page/subpage
   (`setMidiDevice(nullptr)`) and clear the select/mute/solo groups *before*
   `surface.reset()`, then re-point to the new device. A dangling page pointer
   crashes in `linkDevice`/`controlLabelForSlot`.
3. **Re-subscribe websocket parameters on every (re)connect.** A fresh socket
   has no server-side LISTEN subscriptions; `subscribedPaths` must be cleared on
   connect and `subscribeAllParameters()` re-run after page rebuild — otherwise
   MadMapper feedback silently dies after any reconnect.
4. **Rebuild + verify the web bundle.** `index.html` references hashed asset
   files; it is served `no-cache` and missing assets 404 (never SPA-fallback to
   HTML). After editing `web/`, `npm run build` and confirm the served hash
   changed, or the browser runs stale code.

## MIDI facts

- Relative encoders send 7-bit two's-complement deltas: `value ≤ 63` = +n,
  `≥ 64` = −(128−value). Push faders are `encoder_relative`.
- Pitch-bend value range is **0..16383**, not 0..127.
- Component types in profiles: `note`, `note_toggle`, `cc`, `cc_toggle`,
  `encoder`, `encoder_relative`, `pitch_bend`. Roles (e.g. `nav.bankNext`,
  `param.3.fader`) bind a component to app behaviour and **differ per device** —
  read them from that device's entry in `device_profiles.json`.

## Emulator / virtual surface

When no matching MIDI hardware is present the app builds a **virtual surface**
from `settings.emulatorProfile` (default `Push3`) so the web emulator fully
substitutes the controller. Bridge endpoints:

- `GET  /api/display` — mirrored device display (page, 8 labels+values, cue grid).
- `POST /api/midi` — inject one MIDI message (queued, applied on the main thread).
- `POST /api/emulator/surface` `{ "profile": "Faderport16" }` — swap the virtual
  surface (virtual mode only; hardware always wins).
- `WS /ws` — push channel: `{"type":"display",...}` on state change and
  `{"type":"midi","events":[...]}` mirroring hardware + injected MIDI. The web
  UI uses it when available and falls back to polling. Broadcasting happens
  only from `pushWebSocketUpdates()` in `update()` (main thread).

## Git

Branch off `master` before changing anything. Commit/push only when asked, and
for cross-cutting work remember the two addon repos. End commit messages with:

```
Co-Authored-By: Claude <noreply@anthropic.com>
```
