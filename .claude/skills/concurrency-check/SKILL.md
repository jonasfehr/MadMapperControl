---
name: concurrency-check
description: Review a MadMapperControl change for main-thread safety before trusting it. Use whenever code touches the active surface, pages/subpages, component groups, MIDI delivery, page/bank navigation, or adds work triggered from the WebServer or a MIDI callback. These bugs compile cleanly and crash at runtime.
---

# Main-thread safety review

Almost every runtime crash in this app is the same shape: something on a
non-main thread mutated surface/page/group state (or was raced by a surface
swap). The C++ compiler will not catch it. Check the change against this list.

## The one rule

Building/destroying the surface, changing the active page or bank, and
delivering MIDI **all fire the same `ofParameter` listeners** — `setActivePage`
→ `MadParameterPage::linkDevice`, `backToCurrent`, `pageForward/bankForward`,
`updateParameterDisplay`, etc. Those touch `currentPage`, the page list, and the
select/mute/solo groups. Therefore **all of it must run on the main thread**
(inside `ofApp::update()` or a method it calls synchronously).

The WebServer request handlers and RtMidi's input callback run on **other
threads**. They must not do that work directly — they enqueue it and let
`update()` drain it.

## Red flags to reject

- A WebServer callback (anything wired in `setupWebServer`) that calls
  `newMidiMessage`, `setActivePage`, `activatePageByName`, `reloadFromServer`,
  swaps `surface`, or edits `madOscQuery.pages` **directly**. It must instead set
  a `std::atomic_bool` / push to a queue under a mutex, handled in `update()`.
  Pattern to copy: `injectMidiMessage` (enqueue) + `drainInjectedMidi` (main
  thread), and `requestEmulatorSurface` + the pending-switch block in `update()`.
- Destroying or replacing `surface` without first nulling the device pointer on
  every page/subpage (`setMidiDevice(nullptr)`) and clearing `selectGroup`,
  `muteGroup`, `soloGroup`. A page keeps a raw `ofxMidiDevice*`; dangling it
  crashes in `linkDevice`/`controlLabelForSlot`.
- Reading/writing `oscServerConfigs`, `extraOscQueries`, `endpointReachability`,
  or routing maps without `OscServerManager::stateMutex()` — background health /
  reconnect threads touch these.
- A new background `std::thread` that calls into surface/page state instead of
  just setting a pending flag.

## Verify

After any change in this area, run the `evaluate` skill (`bash scripts/eval.sh`).
Its injection + surface-switch probes are exactly the concurrency-sensitive
paths; a crash there prints the faulting stack. For a targeted stress check,
hammer `POST /api/midi` in a loop while calling `POST /api/emulator/surface` —
this is the race that previously segfaulted and must stay clean.
