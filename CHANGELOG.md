# Changelog

## 2.1.1

- packaged Pi-memory v2 for public install (npm/bun/pi package)
- added setup/doctor/smoke scripts
- bundled extension, skill, and prompts
- documented install, architecture, extension lifecycle, and agent usage
- improved packaged extension binary resolution order:
  1) `PI_MEMORY_BIN`
  2) `~/.pi/memory/pi-memory`
  3) `pi-memory` in PATH
