# Changelog

## 2.2.0

### Self-contained package — zero compiler needed

- **prebuilt binaries** for macOS (arm64, x64), Linux (x64, arm64), and Windows (x64)
  - `npm install` / `bun install` / `pi install git:...` just works — no C compiler required
  - falls back to source compilation on unsupported platforms
- **Windows support** — full cross-platform C port
  - compat.h abstraction layer for POSIX → Windows shims
  - bundled getopt_long implementation for MSVC
  - Makefile.win for native MSVC builds
  - handles USERPROFILE, _mkdir, _popen, path separators
- **cross-platform scripts** — all JS, no bash dependency
  - test-smoke.mjs replaces test-smoke.sh (works on Windows)
  - doctor.mjs updated for Windows paths and .exe extension
  - setup.mjs tries prebuilt first, compiles as fallback
  - postinstall.mjs delegates to setup.mjs
- **GitHub Actions CI** — builds and tests on macOS, Linux, and Windows
  - build.yml: matrix build for all 5 platform targets on tag push
  - ci.yml: smoke tests on all 3 OS families
- package.json: version 2.2.0, added `prebuilds/` to files, `os` field, Node.js smoke test

## 2.1.1

- packaged Pi-Memory v2 for public git install via Pi (`pi install git:...`)
- added setup/doctor/smoke scripts
- bundled extension, skill, and prompts
- documented install, architecture, extension lifecycle, and agent usage
- clarified three-layer memory model and JSONL-vs-memory positioning
- added manual compaction controls documentation (`/compact-threshold`, `/compact`)
- aligned binary version output with package version (`2.1.1`)
- fixed Linux build portability (`optreset`/`getopt` reset handling + POSIX declarations)
- improved packaged extension binary resolution order:
  1) `PI_MEMORY_BIN`
  2) `~/.pi/memory/pi-memory`
  3) `pi-memory` in PATH
