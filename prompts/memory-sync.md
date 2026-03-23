---
description: Sync MEMORY.md from live pi-memory database and summarize the delta
---
Sync target file: `${1:-MEMORY.md}`
Project override (optional): `${2:-auto}`

Steps:
1) Run `pi-memory sync "$1"` when project is auto-detected, or `pi-memory sync "$1" --project "$2"` when override is provided.
2) Show a minimal diff summary for the synced file (sections changed + key lines added/removed).
3) Summarize new/changed decisions, findings, lessons, and entities in <=10 bullets.
4) Suggest the next 3 highest-value actions for this project.

If sync fails:
- Report the exact failure.
- Check whether the file exists and contains both marker pairs:
  - `<!-- pi-memory:decisions:start --> ... <!-- pi-memory:decisions:end -->`
  - `<!-- pi-memory:state:start --> ... <!-- pi-memory:state:end -->`
- Provide the exact command to recover and retry.
