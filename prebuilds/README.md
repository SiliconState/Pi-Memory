# Prebuilt binaries

Layout:

```text
prebuilds/<platform>-<arch>/pi-memory[.exe]
```

## Currently committed in this branch

- `darwin-arm64/pi-memory`
- `darwin-x64/pi-memory`

## Additional release/CI targets

- `linux-x64/pi-memory`
- `linux-arm64/pi-memory`
- `win32-x64/pi-memory.exe`

If a matching prebuilt is not present, `scripts/setup.mjs` compiles from source automatically.
