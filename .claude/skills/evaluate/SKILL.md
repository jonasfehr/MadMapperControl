---
name: evaluate
description: Run the ground-truth evaluator for MadMapperControl — build the app, launch it, probe the REST API and emulator round-trips, and confirm no crash. Use after any change to src/, web/, or the addons, and whenever asked to verify the app still works, smoke-test, or check nothing regressed.
---

# Ground-truth evaluation

The real test of this project is not "does it compile" but "does the running app
still serve, inject MIDI, switch surfaces, and not crash". One command does all
of it:

```bash
bash scripts/eval.sh          # build (make Debug) + launch + probe + crash-check
bash scripts/eval.sh --web    # also rebuild the Vue UI first (use after web/ changes)
bash scripts/eval.sh --no-build   # probe the current binary without rebuilding
```

## What it checks

- Every REST endpoint returns a sane shape (`/api/config`, `/api/parameters`,
  `/api/pages`, `/api/mappings`, `/api/profiles`, `/api/profile`, `/api/display`).
- **MIDI injection reaches the surface**: taps `nav.bankNext` via `/api/midi` and
  asserts the mirrored page actually changed.
- **Per-device surface switching**: switches the virtual surface to each profile
  via `/api/emulator/surface` and asserts `/api/display` reflects it.
- The app is still alive at the end (no segfault).

Checks that need MadMapper or real hardware degrade to **SKIP**, so a green run
in virtual mode is still meaningful.

## How to use the result

- Prints `GROUND TRUTH: PASS` / `FAIL` and exits non-zero on any failure or crash.
- A **crash** prints the tail of the app log — treat a segfault as a hard stop and
  read `scripts/eval.sh` output; the usual cause is an invariant in CLAUDE.md
  (off-main-thread mutation, a dangling page pointer). See the `concurrency-check`
  skill.
- The script **stops any running instance of the app** before launching its own,
  and stops its instance on exit. If the user is actively using the app, mention
  that before running.

Report the pass/fail line and any FAILed probes to the user; don't just say
"it builds".
