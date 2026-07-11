#!/usr/bin/env bash
#
# Ground-truth evaluator for MadMapperControl.
#
# One command that answers "does the real thing actually work": builds the app,
# launches it, probes the REST API + emulator round-trips, and confirms it did
# not crash. Runs fine without MadMapper or MIDI hardware (virtual-surface mode);
# hardware-only checks degrade to SKIP.
#
#   bash scripts/eval.sh            build, launch, probe, report
#   bash scripts/eval.sh --web      also rebuild the Vue UI first
#   bash scripts/eval.sh --no-build test the current binary as-is
#
# Exit code is non-zero on any build failure, failed probe, or crash.
# NOTE: stops any running instance of this app before launching its own.
set -uo pipefail

ROOT="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
BINNAME="MadMapperControl_MM6_V2_debug"
BIN="$ROOT/bin/$BINNAME.app/Contents/MacOS/$BINNAME"
LOG="$(mktemp -t mmc_eval)"

DO_BUILD=1; DO_WEB=0
for a in "$@"; do
  case "$a" in
    --no-build) DO_BUILD=0 ;;
    --web)      DO_WEB=1 ;;
    *) echo "unknown option: $a"; exit 2 ;;
  esac
done

say(){ printf "\n\033[1m== %s ==\033[0m\n" "$1"; }
die(){ printf "\033[31m%s\033[0m\n" "$1"; exit 1; }

if [ "$DO_WEB" = 1 ]; then
  say "Building web UI (vite -> bin/data/web)"
  ( cd "$ROOT/web" && npm run build ) || die "web build failed"
fi

if [ "$DO_BUILD" = 1 ]; then
  say "Building app (make -j8 Debug)"
  BUILDLOG="$(mktemp -t mmc_eval_build)"
  make -C "$ROOT" -j8 Debug > "$BUILDLOG" 2>&1; brc=$?
  tail -3 "$BUILDLOG"
  [ $brc -eq 0 ] || { tail -40 "$BUILDLOG"; die "BUILD FAILED"; }
fi
[ -x "$BIN" ] || die "binary not found: $BIN"

# Take over port 8080 from any existing instance of this app — including the
# release build, which shares the port and would split requests between two
# binaries (debug and release must never run simultaneously).
STOPPED_EXISTING=0
pkill -x "$BINNAME" 2>/dev/null && STOPPED_EXISTING=1
pkill -x "MadMapperControl_MM6_V2" 2>/dev/null && STOPPED_EXISTING=1
[ "$STOPPED_EXISTING" = 1 ] && sleep 1

say "Launching app"
( cd "$(dirname "$BIN")" && exec "./$BINNAME" ) > "$LOG" 2>&1 &
PID=$!
cleanup(){ kill "$PID" 2>/dev/null; }
trap cleanup EXIT

# Wait for the web server, failing fast if the app exits during startup.
up=0
for _ in $(seq 1 30); do
  if curl -s -m 2 -o /dev/null http://localhost:8080/api/config; then up=1; break; fi
  kill -0 "$PID" 2>/dev/null || { echo "app exited during startup:"; tail -25 "$LOG"; exit 1; }
  sleep 1
done
[ "$up" = 1 ] || { echo "web server never came up:"; tail -25 "$LOG"; exit 1; }

say "Probing (ground truth)"
python3 "$ROOT/scripts/eval_probe.py"; rc=$?

say "Crash check"
if kill -0 "$PID" 2>/dev/null; then
  printf "\033[32mapp still alive\033[0m\n"
else
  printf "\033[31mAPP CRASHED\033[0m\n"; tail -30 "$LOG"; rc=1
fi

[ "$STOPPED_EXISTING" = 1 ] && echo "note: a previously running instance of $BINNAME was stopped; restart it if needed."
[ $rc -eq 0 ] && printf "\n\033[1;32mGROUND TRUTH: PASS\033[0m\n" || printf "\n\033[1;31mGROUND TRUTH: FAIL\033[0m\n"
exit $rc
