# Changelog

## 2.2.0

### Cross-platform native package hardening

- merged the verified Windows support work into the 2.2 branch
- Windows native support now includes:
  - path/home-directory handling for Windows environments
  - bundled Windows `getopt_long` compatibility
  - MSVC / clang / gcc source-build support
  - `Makefile.win` for native MSVC builds
- session ingest extraction buffers moved off the stack for Windows-safe reliability
- extension project-key resolution now honors:
  1. `PI_MEMORY_PROJECT`
  2. `MEMORY.md` header
  3. working directory basename

### Packaging / install

- package layout is ready for `pi install`, npm, and bun installs
- npm package fileset now excludes locally built `native/pi-memory` artifacts
- `scripts/setup.mjs` now:
  - prefers prebuilt binaries when present
  - falls back to source compilation automatically
  - uses broader Windows compiler discovery
- `scripts/postinstall.mjs` and `scripts/doctor.mjs` now mention bun-friendly setup guidance
- `scripts/install.sh` now delegates to the package setup flow instead of using a stale direct compile path
- package description updated to describe prebuilds accurately

### Prebuilds / release readiness

- committed macOS prebuilds for:
  - `darwin-arm64`
  - `darwin-x64`
- release workflow builds additional artifacts for:
  - `linux-x64`
  - `linux-arm64`
  - `win32-x64`
- documented current prebuilt reality instead of over-promising branch contents

### CI / validation

- smoke tests run through the cross-platform `scripts/test-smoke.mjs`
- CI validates macOS, Linux, and Windows
- `npm pack --dry-run` remains clean with the packaged fileset

### Documentation cleanup

- reconciled README, install docs, architecture docs, extension docs, and skill docs
- removed stale “Windows unsupported” language
- clarified that npm/bun install flow is structurally ready even though public npm publish is not live yet

### Branch cleanup

- removed tracked autoresearch artifacts and the legacy shell smoke script from the product branch

## 2.1.1

- packaged Pi-Memory v2 for public git install via Pi (`pi install git:...`)
- added setup/doctor/smoke scripts
- bundled extension, skill, and prompts
- documented install, architecture, extension lifecycle, and agent usage
- aligned binary version output with package version (`2.1.1`)
- fixed Linux build portability (`optreset`/`getopt` reset handling + POSIX declarations)
- improved packaged extension binary resolution order:
  1. `PI_MEMORY_BIN`
  2. installed binary in the user `.pi/memory` directory
  3. `pi-memory` in PATH
