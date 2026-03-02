#!/usr/bin/env bash
set -euo pipefail

REPO="git:github.com/SiliconState/Pi-Memory"
BIN="${PI_MEMORY_BIN:-$HOME/.pi/memory/pi-memory}"

log() { printf '[pi-memory-install] %s\n' "$*"; }

if ! command -v pi >/dev/null 2>&1; then
  echo "pi CLI is required. Install pi first, then re-run this installer." >&2
  exit 1
fi

log "Installing Pi package: $REPO"
pi install "$REPO"

if [ ! -x "$BIN" ]; then
  log "Binary not found at $BIN. Attempting local compile fallback..."

  SRC_GLOBAL="$HOME/.pi/agent/git/github.com/SiliconState/Pi-Memory/native/pi-memory.c"
  SRC_PROJECT="$HOME/.pi/git/github.com/SiliconState/Pi-Memory/native/pi-memory.c"

  if [ -f "$SRC_GLOBAL" ]; then
    SRC="$SRC_GLOBAL"
  elif [ -f "$SRC_PROJECT" ]; then
    SRC="$SRC_PROJECT"
  else
    echo "Could not locate native/pi-memory.c after install." >&2
    echo "Expected one of:" >&2
    echo "  $SRC_GLOBAL" >&2
    echo "  $SRC_PROJECT" >&2
    exit 1
  fi

  mkdir -p "$(dirname "$BIN")"
  CC_BIN="${CC:-cc}"
  "$CC_BIN" -Wall -Wextra -Wpedantic -O2 -std=c11 -o "$BIN" "$SRC" -lsqlite3
  chmod +x "$BIN"
fi

log "Installed binary: $BIN"
"$BIN" --version

log "Done. Optional health check:"
log "  $BIN help"
