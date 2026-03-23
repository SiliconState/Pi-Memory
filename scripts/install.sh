#!/usr/bin/env bash
set -euo pipefail

REPO="git:github.com/SiliconState/Pi-Memory"
BIN="${PI_MEMORY_BIN:-$HOME/.pi/memory/pi-memory}"

log() { printf '[pi-memory-install] %s\n' "$*"; }

die() {
  printf '%s\n' "$*" >&2
  exit 1
}

if ! command -v pi >/dev/null 2>&1; then
  die "pi CLI is required. Install pi first, then re-run this installer."
fi

log "Installing Pi package: $REPO"
pi install "$REPO"

if [ ! -x "$BIN" ]; then
  log "Binary not found at $BIN. Running package setup fallback..."

  PKG_GLOBAL="$HOME/.pi/agent/git/github.com/SiliconState/Pi-Memory"
  PKG_PROJECT="$HOME/.pi/git/github.com/SiliconState/Pi-Memory"

  if [ -d "$PKG_GLOBAL" ]; then
    PKG="$PKG_GLOBAL"
  elif [ -d "$PKG_PROJECT" ]; then
    PKG="$PKG_PROJECT"
  else
    die "Could not locate the installed Pi-Memory package directory after install."
  fi

  SETUP_SCRIPT="$PKG/scripts/setup.mjs"
  [ -f "$SETUP_SCRIPT" ] || die "Missing setup script: $SETUP_SCRIPT"
  command -v node >/dev/null 2>&1 || die "Node.js is required to run the package setup fallback."

  node "$SETUP_SCRIPT"
fi

log "Installed binary: $BIN"
"$BIN" --version

log "Done. Optional health check:"
log "  $BIN help"
