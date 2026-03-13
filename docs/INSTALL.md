# Install Guide

## Platform support

- **macOS** and **Linux** are supported.
- **Windows** is supported with a native Windows C compiler (`clang`, `gcc`, or `cl`).

## Prerequisites

- Pi installed (`pi` CLI)
- C compiler
  - macOS/Linux: `cc` / `clang` / `gcc`
  - Windows: `clang`, `gcc`, or Visual Studio `cl`

> SQLite is bundled (amalgamation compiled directly into the binary). No system `libsqlite3-dev`, `sqlite3.h`, or external database server is required.

## Option A (recommended): single Pi command

```bash
pi install git:github.com/SiliconState/Pi-Memory
```

This installs package resources declared in `package.json` (`pi.extensions`, `pi.skills`, `pi.prompts`) and runs the native build in `postinstall`.

Verify:

### macOS / Linux
```bash
~/.pi/memory/pi-memory --version
```

### Windows (PowerShell)
```powershell
$env:USERPROFILE\.pi\memory\pi-memory.exe --version
```

## Option B: Unix convenience installer

```bash
curl -fsSL https://raw.githubusercontent.com/SiliconState/Pi-Memory/main/scripts/install.sh | bash
```

The shell installer is **Unix-only**. On Windows, use Option A or the manual compile step below.

## npm/bun status

npm/bun publish is planned but not currently live.

Use Option A (`pi install git:...`) for now.

## Native build output

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

Override binary path with:

### macOS / Linux
```bash
export PI_MEMORY_BIN=/custom/path/pi-memory
```

### Windows (PowerShell)
```powershell
$env:PI_MEMORY_BIN = 'C:\custom\path\pi-memory.exe'
```

## Troubleshooting

### `Failed to compile pi-memory`

Install a basic C compiler and rerun:

```bash
pi install git:github.com/SiliconState/Pi-Memory
```

Or from the package directory:

```bash
npm run setup
```

### Manual compile — macOS / Linux

```bash
PKG="$HOME/.pi/agent/git/github.com/SiliconState/Pi-Memory"
[ -d "$PKG" ] || PKG="$HOME/.pi/git/github.com/SiliconState/Pi-Memory"
mkdir -p "$HOME/.pi/memory"
cc -Wall -Wextra -Wpedantic -O2 -std=c11 -o "$HOME/.pi/memory/pi-memory" \
  "$PKG/native/pi-memory.c" "$PKG/native/sqlite3.c" "$PKG/native/getopt_compat.c" -lpthread -ldl -lm
chmod +x "$HOME/.pi/memory/pi-memory"
```

### Manual compile — Windows (PowerShell + clang/gcc)

```powershell
$pkg = "$env:USERPROFILE\.pi\agent\git\github.com\SiliconState\Pi-Memory"
if (-not (Test-Path $pkg)) { $pkg = (Resolve-Path ".\Pi-Memory").Path }
New-Item -ItemType Directory -Force -Path "$env:USERPROFILE\.pi\memory" | Out-Null
clang -D_CRT_SECURE_NO_WARNINGS -Wall -Wextra -Wpedantic -O2 -std=c11 `
  -o "$env:USERPROFILE\.pi\memory\pi-memory.exe" `
  "$pkg\native\pi-memory.c" "$pkg\native\sqlite3.c" "$pkg\native\getopt_compat.c"
```

### Manual compile — Windows (Developer PowerShell + MSVC `cl`)

```powershell
$pkg = "$env:USERPROFILE\.pi\agent\git\github.com\SiliconState\Pi-Memory"
if (-not (Test-Path $pkg)) { $pkg = (Resolve-Path ".\Pi-Memory").Path }
New-Item -ItemType Directory -Force -Path "$env:USERPROFILE\.pi\memory" | Out-Null
cl /nologo /D_CRT_SECURE_NO_WARNINGS /O2 /W4 /EHsc `
  /Fe:"$env:USERPROFILE\.pi\memory\pi-memory.exe" `
  "$pkg\native\pi-memory.c" "$pkg\native\sqlite3.c" "$pkg\native\getopt_compat.c"
```

### Extension can’t find binary

Set explicit path:

#### macOS / Linux
```bash
export PI_MEMORY_BIN="$HOME/.pi/memory/pi-memory"
```

#### Windows (PowerShell)
```powershell
$env:PI_MEMORY_BIN = "$env:USERPROFILE\.pi\memory\pi-memory.exe"
```

### Check health quickly

#### macOS / Linux
```bash
~/.pi/memory/pi-memory --version
~/.pi/memory/pi-memory help
```

#### Windows (PowerShell)
```powershell
$env:USERPROFILE\.pi\memory\pi-memory.exe --version
$env:USERPROFILE\.pi\memory\pi-memory.exe help
```
