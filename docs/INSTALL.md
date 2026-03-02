# Install Guide

## Prerequisites

- Pi installed (`pi` CLI)
- C compiler (`cc`/`clang`/`gcc`)
- SQLite development headers (`sqlite3` + `libsqlite3-dev` on Linux)

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

## Option C: Pi CLI install from npm (after publish)

```bash
pi install npm:@siliconstate/pi-memory
```

## Option D: npm global install (after npm publish)

```bash
npm i -g @siliconstate/pi-memory
pi-memory-setup
pi-memory-doctor
```

## Option E: bun global install (after npm publish)

```bash
bun add -g @siliconstate/pi-memory
pi-memory-setup
pi-memory-doctor
```

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

Install build prerequisites and rerun:

```bash
pi-memory-setup
```

### Extension can’t find binary

Set explicit path:

```bash
export PI_MEMORY_BIN="$HOME/.pi/memory/pi-memory"
```

### Check health quickly

Always works:

```bash
~/.pi/memory/pi-memory --version
```

If installed via npm/bun global, you can also run:

```bash
pi-memory-doctor
```
