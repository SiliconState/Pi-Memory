# Architecture

Pi-Memory is intentionally split into three layers:

1. **Core memory engine** (`native/pi-memory.c`)
2. **Pi runtime integration** (`extensions/pi-memory-compact.ts`)
3. **Project memory bridge** (`MEMORY.md`)

The design goal is simple: keep persistence tiny and predictable, and keep Pi-specific behavior at the extension boundary.

---

## 1) Core memory engine (C + SQLite)

The native binary is the source of truth.

### Storage locations

### macOS / Linux
```text
~/.pi/memory/pi-memory
~/.pi/memory/memory.db
```

### Windows
```text
%USERPROFILE%\.pi\memory\pi-memory.exe
%USERPROFILE%\.pi\memory\memory.db
```

### Primary tables

- `decisions`
- `findings`
- `lessons`
- `entities`
- `sessions`
- `project_state`

### CLI capabilities

- `log decision|finding|lesson|entity`
- `query` / `search` / `recent` / `projects`
- `state <project>`
- `sync MEMORY.md`
- `ingest-session <session.jsonl>`
- `sessions`
- `export --format md|json`

### Reliability choices

- bundled SQLite amalgamation — no system SQLite dependency
- `sqlite3_busy_timeout` to reduce lock failures
- prepared statements for all write/query paths
- schema versioning via `PRAGMA user_version`
- fast-path DB open on already-upgraded schemas
- migration-only WAL enablement instead of enabling WAL on every open
- foreign keys enabled on open
- session ingest is idempotent

---

## 2) Extension layer (TypeScript)

The extension automates memory hygiene around compaction, failover, and shutdown.

### Key hooks

- `session_before_compact`
  - best-effort `pi-memory sync MEMORY.md`
  - builds the compaction summary via model fallback logic
- `session_compact`
  - stores compaction summary as a finding
  - resumes the last user intent after auto-compaction
- `auto_retry_end`
  - bridges final transient provider failures to another provider/model when available
- `turn_end`
  - monitors context usage and triggers early compaction when needed
  - updates the status-bar context meter
- `model_select`
  - warns or compacts when switching to a smaller context model
- `session_shutdown`
  - ingests the current Pi session JSONL
  - updates `project_state`
  - syncs `MEMORY.md`

### Binary resolution

The extension resolves the native binary in this order:
1. `PI_MEMORY_BIN`
2. the default user `.pi/memory` location for the current OS
3. `pi-memory` on PATH

### Project-key behavior

The extension tries to keep project naming aligned with the native CLI by checking:
1. `PI_MEMORY_PROJECT`
2. `MEMORY.md` header (`# Memory — <project>`)
3. current directory basename

---

## 3) `MEMORY.md` bridge

`MEMORY.md` is not the source of truth.
The database is.

`MEMORY.md` exists so a new Pi session can quickly load curated context from the working tree.

Flow:
- database records are updated through the CLI and extension
- `pi-memory sync MEMORY.md` projects the current state into marker blocks
- the next session sees the synced file in the repo context

Required marker pairs:

```html
<!-- pi-memory:decisions:start --> ... <!-- pi-memory:decisions:end -->
<!-- pi-memory:state:start --> ... <!-- pi-memory:state:end -->
```

---

## Session JSONL vs Pi-Memory

They are complementary.

| Layer | Role |
|---|---|
| Pi session JSONL | exhaustive transcript, auditing, replay |
| Pi-Memory DB | distilled semantic memory, recall, continuity |

### Automatic transcript pipeline

| Step | What happens | Component |
|---|---|---|
| 1 | Pi writes raw session JSONL | Pi runtime |
| 2 | shutdown hook runs `ingest-session` | extension |
| 3 | ingest updates `sessions`, extracted memories, rollups | native CLI |
| 4 | extension writes final state + syncs `MEMORY.md` | extension + native CLI |
| 5 | next session loads synced repo context | Pi runtime |

Raw JSONL is **not** the curated memory layer.
It is the raw source that the memory system mines and summarizes.

---

## Skill + prompts

- `skills/memory/SKILL.md` — agent operating guidance
- `prompts/memory-sync.md` — sync/review workflow
- `prompts/memory-ingest-session.md` — ingest/review workflow

---

## Design principles

- keep the core memory engine small and auditable
- keep Pi-specific workflow logic in the extension
- prefer local durability over extra infrastructure
- keep the install story predictable across macOS, Linux, and Windows
