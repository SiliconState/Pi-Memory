# Architecture

Pi-Memory v2 is intentionally split into three layers:

1. **Core memory engine** (`native/pi-memory.c`)
2. **Pi runtime integration** (`extensions/pi-memory-compact.ts`)
3. **Project memory bridge** (`MEMORY.md` files synced from DB)

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
- `state <project>` (phase, summary, next actions + rollups)
- `sync` (`MEMORY.md` marker replacement)
- `ingest-session` (Pi JSONL -> semantic memory)
- `sessions` (ingested session index)
- `export` (md/json)

### Reliability choices

- SQLite amalgamation bundled — no system `libsqlite3-dev` dependency
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

## MEMORY.md bridge layer

`MEMORY.md` files are not the source of truth (SQLite is), but they are the context bridge into new sessions.

- Source of truth: `memory.db`
- Projection: `pi-memory sync MEMORY.md`
- Session continuity: Pi loads project files, so synced `MEMORY.md` carries curated memory into fresh sessions

## Session JSONL vs pi-memory

- Session JSONL: exhaustive transcript (high volume, noisy, forensic)
- pi-memory DB: distilled, queryable semantic memory (high signal)

They are complementary. Ingest converts relevant signal from JSONL into structured memory tables.

## Automatic session transcript pipeline (what is actually “auto”)

| Step | What happens automatically | Component |
|---|---|---|
| 1 | Pi runtime writes raw session transcript to `~/.pi/agent/sessions/.../session.jsonl` | Pi core |
| 2 | On `session_shutdown`, extension runs `pi-memory ingest-session <session.jsonl> --project <project>` | `extensions/pi-memory-compact.ts` |
| 3 | Ingest updates `sessions`, stores compaction summaries/findings, extracts decisions/lessons/entities, rolls up `project_state` | `native/pi-memory.c` |
| 4 | Extension writes final state summary and runs `pi-memory sync MEMORY.md` | extension + C binary |
| 5 | Next session starts with synced `MEMORY.md` available as project context | Pi context loading |

Important distinction: raw JSONL is **not** injected directly as prompt context; curated memory derived from it is.

## Skill + Prompts

- `skills/memory/SKILL.md`: operational policy and command usage for agents
- `prompts/*.md`: reusable workflows for sync + ingest review

## Design principles

- Keep core memory engine tiny and predictable
- Keep integration logic at extension boundary
- Use local-only durability by default
- Prefer explicit CLI contracts over hidden automation
