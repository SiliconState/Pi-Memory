# Prebuilt binaries

Layout: `prebuilds/<platform>-<arch>/pi-memory[.exe]`

Shipped platforms:
- `darwin-x64` — macOS Intel
- `darwin-arm64` — macOS Apple Silicon

Built by CI (added on release):
- `linux-x64`
- `linux-arm64`
- `win32-x64` (`.exe`)

If no prebuilt exists for your platform, `scripts/setup.mjs` compiles from source automatically.
