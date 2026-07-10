#!/usr/bin/env python3
"""Ground-truth probes against a running MadMapperControl instance (port 8080).

Verifies the REST API contract and the emulator round-trips that are easy to
break: MIDI injection reaching the surface, per-device profile switching, and
the app surviving both. Assertions that need MadMapper connected degrade to
SKIP so the check is meaningful in virtual mode (no hardware, no MadMapper).

Exit code is non-zero if any assertion FAILed. Run via scripts/eval.sh.
"""
import json
import re
import sys
import time
import urllib.error
import urllib.request

BASE = "http://localhost:8080"
if sys.stdout.isatty():
    G, R, Y, X = "\033[32m", "\033[31m", "\033[33m", "\033[0m"
else:
    G = R = Y = X = ""
passed = failed = skipped = 0


def get(path, tries=4):
    # /api/config and friends take the OSC state mutex, which a startup/reconnect
    # thread can hold briefly — retry so a transient block isn't a false failure.
    for i in range(tries):
        try:
            with urllib.request.urlopen(BASE + path, timeout=5) as r:
                return r.status, json.loads(r.read().decode())
        except Exception:
            if i == tries - 1:
                raise
            time.sleep(0.75)


def get_text(path):
    with urllib.request.urlopen(BASE + path, timeout=5) as r:
        return r.status, r.read().decode()


def post(path, body):
    req = urllib.request.Request(
        BASE + path, data=json.dumps(body).encode(),
        headers={"Content-Type": "application/json"})
    with urllib.request.urlopen(req, timeout=4) as r:
        return r.status, r.read().decode()


def ok(m):
    global passed
    passed += 1
    print(f"  {G}PASS{X} {m}")


def bad(m):
    global failed
    failed += 1
    print(f"  {R}FAIL{X} {m}")


def skip(m):
    global skipped
    skipped += 1
    print(f"  {Y}SKIP{X} {m}")


def midi_for(comp, value):
    """Build an /api/midi payload matching a profile component's type."""
    t = comp.get("type", "cc")
    if t in ("note", "note_toggle"):
        return {"channel": comp["channel"], "status": 144, "control": 0,
                "pitch": comp["address"], "value": value}
    if t == "pitch_bend":
        return {"channel": comp["channel"], "status": 224, "control": 0,
                "pitch": 0, "value": value}
    return {"channel": comp["channel"], "status": 176, "control": comp["address"],
            "pitch": 0, "value": value}


def page():
    return get("/api/display")[1].get("page", "")


# ── REST contract ────────────────────────────────────────────────────────────
print("REST API")
try:
    s, cfg = get("/api/config")
    servers = cfg.get("servers")
    if s == 200 and isinstance(servers, list) and servers:
        ok(f"/api/config — {len(servers)} server(s)")
    else:
        bad(f"/api/config — bad shape: {cfg}")
except Exception as e:
    bad(f"/api/config — {e}")

for path, key in [("/api/parameters", None), ("/api/pages", "pages"),
                  ("/api/mappings", "fixed"), ("/api/profiles", None),
                  ("/api/profile", "name"), ("/api/display", "profile")]:
    try:
        s, body = get(path)
        if s != 200:
            bad(f"{path} — HTTP {s}")
        elif key and key not in body:
            bad(f"{path} — missing '{key}'")
        else:
            ok(path)
    except Exception as e:
        bad(f"{path} — {e}")

try:
    s, profiles = get("/api/profiles")
    active = [p["name"] for p in profiles if p.get("active")]
    ok(f"/api/profiles — {[p['name'] for p in profiles]}, active={active}")
except Exception as e:
    bad(f"/api/profiles — {e}")

# ── Web UI bundle contract ───────────────────────────────────────────────────
# Guards the stale-cache invariant: index.html must reference a bundle that
# actually exists (catches "edited web/ but forgot npm run build"), and a
# missing asset must 404 — never SPA-fallback to HTML fed to a <script> tag.
print("\nWeb UI bundle")
try:
    s, html = get_text("/")
    m = re.search(r"assets/index-[a-z0-9]+\.js", html)
    if s != 200 or not m:
        bad(f"GET / — HTTP {s}, bundle ref {'found' if m else 'MISSING'}")
    else:
        bundle = m.group(0)
        try:
            s2, _ = get_text("/" + bundle)
            (ok if s2 == 200 else bad)(f"{bundle} serves (HTTP {s2})")
        except Exception as e:
            bad(f"{bundle} referenced by index.html but not served — {e}")
    try:
        get_text("/assets/index-doesnotexist.js")
        bad("missing asset returned 200 (SPA fallback regression)")
    except urllib.error.HTTPError as e:
        (ok if e.code == 404 else bad)(f"missing asset -> HTTP {e.code}")
except Exception as e:
    bad(f"web bundle — {e}")

# ── WebSocket push (/ws) ─────────────────────────────────────────────────────
# The app must accept the upgrade and push a display-state frame to a fresh
# client (client-generation reset), without needing any state change.
print("\nWebSocket")
try:
    import base64
    import os
    import socket
    key = base64.b64encode(os.urandom(16)).decode()
    sock = socket.create_connection(("localhost", 8080), timeout=4)
    sock.sendall((
        "GET /ws HTTP/1.1\r\nHost: localhost:8080\r\n"
        "Upgrade: websocket\r\nConnection: Upgrade\r\n"
        f"Sec-WebSocket-Key: {key}\r\nSec-WebSocket-Version: 13\r\n\r\n"
    ).encode())
    resp = sock.recv(2048).decode(errors="replace")
    if "101" in resp.split("\r\n", 1)[0]:
        ok("/ws upgrade -> 101 Switching Protocols")
        sock.settimeout(3)
        # Skip any handshake remainder; wait for the first pushed text frame.
        try:
            frame = sock.recv(8192)
            is_text = bool(frame) and (frame[0] & 0x0F) == 0x1
            (ok if is_text else bad)(f"display frame pushed to new client ({len(frame)} bytes)")
        except socket.timeout:
            bad("no frame pushed within 3 s of connecting")
    else:
        bad(f"/ws upgrade failed: {resp.splitlines()[0] if resp else 'no response'}")
    sock.close()
except Exception as e:
    bad(f"/ws — {e}")

# ── Emulator: MIDI injection reaches the surface ─────────────────────────────
print("\nEmulator — MIDI injection")
try:
    _, prof = get("/api/profile")
    comps = prof.get("components", [])
    bank_next = next((c for c in comps if c.get("role") == "nav.bankNext"), None)
    bank_prev = next((c for c in comps if c.get("role") == "nav.bankPrev"), None)
    if not bank_next:
        skip("no nav.bankNext in active profile")
    else:
        before = page()

        def tap(c):
            post("/api/midi", midi_for(c, 127))
            time.sleep(0.15)
            post("/api/midi", midi_for(c, 0))
            time.sleep(0.4)

        tap(bank_next)
        after = page()
        if after != before:
            ok(f"nav.bankNext moved page '{before}' -> '{after}'")
            if bank_prev:
                tap(bank_prev)  # restore
        elif bank_prev:
            tap(bank_prev)
            if page() != before:
                ok("nav.bankPrev moved page (bankNext was at last bank)")
                tap(bank_next)
            else:
                skip("page did not move (single bank / MadMapper not connected)")
        else:
            skip("page did not move (single bank / MadMapper not connected)")
except Exception as e:
    bad(f"MIDI injection — {e}")

# ── Emulator: per-device surface switching (virtual mode only) ───────────────
print("\nEmulator — surface switch")
try:
    _, disp = get("/api/display")
    origin = disp.get("profile", "Push3")
    if not disp.get("virtual"):
        skip("hardware connected — surface is locked to the active device")
    else:
        names = [p["name"] for p in get("/api/profiles")[1]]
        for name in names:
            post("/api/emulator/surface", {"profile": name})
            time.sleep(1.2)
            got = get("/api/display")[1].get("profile")
            (ok if got == name else bad)(f"switch -> {name}" + ("" if got == name else f" (got {got})"))
        post("/api/emulator/surface", {"profile": origin})  # restore
        time.sleep(1.0)
except Exception as e:
    bad(f"surface switch — {e}")

# ── Summary ──────────────────────────────────────────────────────────────────
print(f"\n{passed} passed, {failed} failed, {skipped} skipped")
sys.exit(1 if failed else 0)
