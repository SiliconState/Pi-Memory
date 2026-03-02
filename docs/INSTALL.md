# Install Guide

## Platform support

- **macOS** and **Linux** are supported.
- **Windows** is not currently supported (no native build target).

## Prerequisites

- Pi installed (`pi` CLI)
- C compiler (`cc`/`clang`/`gcc`)

> SQLite is bundled (amalgamation compiled directly into the binary). No system `libsqlite3-dev` or `sqlite3.h` needed.

## Option A (recommended): single Pi command

```bash
pi install git:github.com/SiliconState/Pi-Memory
```

This installs package resources declared in `package.json` (`pi.extensions`, `pi.skills`, `pi.prompts`) and runs native build postinstall.

Verify:

```bash
~/.pi/memory/pi-memory --version
```

## Option B: single curl command

```bash
curl -fsSL https://raw.githubusercontent.com/SiliconState/Pi-Memory/main/scripts/install.sh | bash
```

The installer script runs `pi install ...` and falls back to direct compile if needed.

## npm/bun status

npm/bun publish is planned but not currently live.

Use Option A (`pi install git:...`) or Option B (curl installer) for now.

## Native build output

Default binary location:

```text
~/.pi/memory/pi-memory
```

Database:

```text
~/.pi/memory/memory.db
```

Override binary path with:

```bash
export PI_MEMORY_BIN=/custom/path/pi-memory
```

## Troubleshooting

### `Failed to compile pi-memory`

Install build prerequisites and rerun the installer:

```bash
pi install git:github.com/SiliconState/Pi-Memory
```

If needed, compile manually:

```bash
PKG="$HOME/.pi/agent/git/github.com/SiliconState/Pi-Memory"
[ -d "$PKG" ] || PKG="$HOME/.pi/git/github.com/SiliconState/Pi-Memory"
mkdir -p "$HOME/.pi/memory"
cc -Wall -Wextra -Wpedantic -O2 -std=c11 -o "$HOME/.pi/memory/pi-memory" \
  "$PKG/native/pi-memory.c" "$PKG/native/sqlite3.c" -lpthread -ldl -lm
chmod +x "$HOME/.pi/memory/pi-memory"
```

### Extension can’t find binary

Set explicit path:

```bash
export PI_MEMORY_BIN="$HOME/.pi/memory/pi-memory"
```

### Check health quickly

```bash
~/.pi/memory/pi-memory --version
~/.pi/memory/pi-memory help
```
