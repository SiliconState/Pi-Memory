# Changelog

## 2.1.1

- packaged Pi-Memory v2 for public git install via Pi (`pi install git:...`)
- added setup/doctor/smoke scripts
- bundled extension, skill, and prompts
- documented install, architecture, extension lifecycle, and agent usage
- clarified three-layer memory model and JSONL-vs-memory positioning
- added manual compaction controls documentation (`/compact-threshold`, `/compact`)
- aligned binary version output with package version (`2.1.1`)
- improved packaged extension binary resolution order:
  1) `PI_MEMORY_BIN`
  2) `~/.pi/memory/pi-memory`
  3) `pi-memory` in PATH
