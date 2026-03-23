# Install Guide

## Platform support

Pi-Memory supports:
- **macOS**
- **Linux**
- **Windows**

If a matching prebuilt binary is present, install is copy-only.
If not, install falls back to compiling from source.

## Prerequisites

### For Pi users
- Pi installed (`pi` CLI)

### For source-compile fallback
- Node.js 18.17+
- a C compiler
  - macOS: `cc` / `clang`
  - Linux: `cc` / `clang` / `gcc`
  - Windows: Visual Studio `cl`, `clang`, or `gcc`

> SQLite is bundled via the amalgamation (`native/sqlite3.c` + `native/sqlite3.h`). No system SQLite package is required.

---

## Option A — recommended for Pi users

```bash
pi install git:github.com/SiliconState/Pi-Memory
```

This path installs:
- `extensions/pi-memory-compact.ts`
- `skills/memory/SKILL.md`
- `prompts/*.md`
- the native binary via `postinstall`

Verify:

### macOS / Linux
```bash
~/.pi/memory/pi-memory --version
```

### Windows (PowerShell)
```powershell
$env:USERPROFILE\.pi\memory\pi-memory.exe --version
```

---

## Option B — npm / bun install from git or tarball

The package layout is ready for npm and bun, even though public npm publishing is not live yet.

Examples:

```bash
npm install github:SiliconState/Pi-Memory
bun add github:SiliconState/Pi-Memory
```

Important:
- npm/bun installs run `postinstall`, which invokes `scripts/setup.mjs`
- this is useful for package QA and direct native installation
- **it does not replace `pi install`** if you want Pi to register the extension/skill/prompt metadata

---

## Option C — Unix helper installer

```bash
curl -fsSL https://raw.githubusercontent.com/SiliconState/Pi-Memory/main/scripts/install.sh | bash
```

Notes:
- Unix only
- calls `pi install ...`
- if the binary is still missing afterward, it runs the package `setup.mjs` fallback

---

## Binary + database locations

### macOS / Linux
```text
~/.pi/memory/pi-memory
~/.pi/memory/memory.db
```

### Windows
```text
%USERPROFILE%\.pi\memory\pi-memory.exe
%USERPROFILE%\.pi\memory\memory.db
```

Override binary location with `PI_MEMORY_BIN`.

### macOS / Linux
```bash
export PI_MEMORY_BIN=/custom/path/pi-memory
```

### Windows (PowerShell)
```powershell
$env:PI_MEMORY_BIN = 'C:\custom\path\pi-memory.exe'
```

---

## Current prebuilt reality

On this branch, macOS prebuilds are committed in `prebuilds/`.
Linux and Windows are supported through:
- release/CI-built prebuilds when available
- source compilation fallback otherwise

You can inspect the expected layout in `prebuilds/README.md`.

---

## Troubleshooting

### `Failed to compile pi-memory`

Run the package setup explicitly:

```bash
npm run setup
```

Or with bun:

```bash
bun run setup
```

If that still fails, verify that:
- Node.js is installed
- a supported C compiler is available
- your shell can execute the compiler directly

### Check installation health

```bash
npm run doctor
npm run test:smoke
```

### Extension can’t find the binary

Set `PI_MEMORY_BIN` explicitly.

### Manual source compile — macOS

```bash
PKG="$HOME/.pi/agent/git/github.com/SiliconState/Pi-Memory"
[ -d "$PKG" ] || PKG="$HOME/.pi/git/github.com/SiliconState/Pi-Memory"
mkdir -p "$HOME/.pi/memory"
cc -Wall -Wextra -Wpedantic -O2 -std=c11 \
  -o "$HOME/.pi/memory/pi-memory" \
  "$PKG/native/pi-memory.c" "$PKG/native/sqlite3.c" -lm
chmod +x "$HOME/.pi/memory/pi-memory"
```

### Manual source compile — Linux

```bash
PKG="$HOME/.pi/agent/git/github.com/SiliconState/Pi-Memory"
[ -d "$PKG" ] || PKG="$HOME/.pi/git/github.com/SiliconState/Pi-Memory"
mkdir -p "$HOME/.pi/memory"
cc -Wall -Wextra -Wpedantic -O2 -std=c11 \
  -o "$HOME/.pi/memory/pi-memory" \
  "$PKG/native/pi-memory.c" "$PKG/native/sqlite3.c" -lpthread -ldl -lm
chmod +x "$HOME/.pi/memory/pi-memory"
```

### Manual source compile — Windows (MSVC)

```powershell
$pkg = "$env:USERPROFILE\.pi\agent\git\github.com\SiliconState\Pi-Memory"
if (-not (Test-Path $pkg)) { $pkg = "$env:USERPROFILE\.pi\git\github.com\SiliconState\Pi-Memory" }
New-Item -ItemType Directory -Force -Path "$env:USERPROFILE\.pi\memory" | Out-Null
cl /nologo /O2 /W3 /D_CRT_SECURE_NO_WARNINGS /D_CRT_NONSTDC_NO_DEPRECATE `
  /Fe:"$env:USERPROFILE\.pi\memory\pi-memory.exe" `
  "$pkg\native\pi-memory.c" "$pkg\native\sqlite3.c"
```

### Manual source compile — Windows (clang / gcc)

```powershell
$pkg = "$env:USERPROFILE\.pi\agent\git\github.com\SiliconState\Pi-Memory"
if (-not (Test-Path $pkg)) { $pkg = "$env:USERPROFILE\.pi\git\github.com\SiliconState\Pi-Memory" }
New-Item -ItemType Directory -Force -Path "$env:USERPROFILE\.pi\memory" | Out-Null
clang -D_CRT_SECURE_NO_WARNINGS -D_CRT_NONSTDC_NO_DEPRECATE -Wall -Wextra -Wpedantic -O2 -std=c11 `
  -o "$env:USERPROFILE\.pi\memory\pi-memory.exe" `
  "$pkg\native\pi-memory.c" "$pkg\native\sqlite3.c" -lm
```
