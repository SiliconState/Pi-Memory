# Architecture

Pi-Memory v2 is intentionally split into two layers:

1. **Core memory engine** (`native/pi-memory.c`)
2. **Pi runtime integration** (`extensions/pi-memory-compact.ts`)

## Core (C + SQLite)

The C binary is the source of truth for persistence.

### Storage

SQLite database:

```text
~/.pi/memory/memory.db
```

Primary tables:
- `decisions`
- `findings`
- `lessons`
- `entities`
- `sessions`
- `project_state`

### CLI capabilities

- `log decision|finding|lesson|entity`
- `query` / `search` / `recent` / `projects`
- `state` (phase, summary, next actions + rollups)
- `sync` (`MEMORY.md` marker replacement)
- `ingest-session` (Pi JSONL -> semantic memory)
- `sessions` (ingested session index)
- `export` (md/json)

### Reliability choices

- `sqlite3_busy_timeout` to reduce lock failures under concurrent writes
- parameterized SQL statements for injection safety
- idempotent session ingest
- WAL mode + foreign keys enabled

## Extension (TypeScript)

The extension automates operational memory hygiene around compaction and shutdown.

### Key hooks

- `session_before_compact`
  - syncs `MEMORY.md` best-effort
  - can run custom compaction fallback model flow
- `session_compact`
  - logs compaction summary as finding
  - restores user intent after compaction
- `turn_end`
  - threshold monitoring + early compaction trigger
- `model_select`
  - warns/compacts if switching to smaller context model
- `session_shutdown`
  - ingests session JSONL
  - updates project state
  - syncs `MEMORY.md`

### Binary resolution

The packaged extension resolves pi-memory in this order:
1. `PI_MEMORY_BIN` env var
2. `~/.pi/memory/pi-memory`
3. fallback command `pi-memory` in PATH

## Skill + Prompts

- `skills/memory/SKILL.md`: operational policy and command usage for agents
- `prompts/*.md`: reusable workflows for sync + ingest review

## Design principles

- Keep core memory engine tiny and predictable
- Keep integration logic at extension boundary
- Use local-only durability by default
- Prefer explicit CLI contracts over hidden automation
