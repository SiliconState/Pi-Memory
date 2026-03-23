# Prebuilt binaries

Layout:

```text
prebuilds/<platform>-<arch>/pi-memory[.exe]
```

## Committed platforms

- `darwin-arm64/pi-memory`
- `darwin-x64/pi-memory`
- `linux-x64/pi-memory`
- `linux-arm64/pi-memory`
- `win32-x64/pi-memory.exe`

Prebuilds are automatically updated by CI when native code changes.

If a matching prebuilt is not present, `scripts/setup.mjs` compiles from source automatically.
