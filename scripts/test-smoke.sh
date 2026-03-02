#!/usr/bin/env bash
set -euo pipefail

ROOT="$(cd "$(dirname "$0")/.." && pwd)"
TMP_HOME="$(mktemp -d)"
export HOME="$TMP_HOME"

echo "[smoke] HOME=$HOME"
node "$ROOT/scripts/setup.mjs" --quiet

BIN="$HOME/.pi/memory/pi-memory"
"$BIN" --version
"$BIN" log decision "Smoke" --choice "Use C + SQLite" --project smoke >/dev/null
"$BIN" log finding "Smoke finding" --project smoke >/dev/null
"$BIN" log lesson "Smoke lesson" --fix "Apply fix" --project smoke >/dev/null
"$BIN" log entity "SmokeEntity" --type concept --project smoke >/dev/null

"$BIN" query --project smoke --type entity --limit 1 >/dev/null
"$BIN" search "Smoke" --project smoke >/dev/null
"$BIN" export --project smoke --format json >/dev/null

echo "[smoke] pass"
