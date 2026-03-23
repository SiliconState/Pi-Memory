# Agent Usage Guide

This guide is for agent authors/operators using Pi-Memory in Pi workflows.

If `pi-memory` is not on PATH, use the installed binary directly:
- macOS / Linux: `~/.pi/memory/pi-memory`
- Windows: `%USERPROFILE%\.pi\memory\pi-memory.exe`

If you want one portable override, set `PI_MEMORY_BIN`.

---

## Core operating rules

1. Log decisions when choices are made, not at the end of the week.
2. Log findings with source and confidence whenever possible.
3. Log lessons immediately after the fix is confirmed.
4. Keep entities high-signal.
5. Sync `MEMORY.md` before or after major compaction/handoff points.
6. Use `--project` explicitly when project attribution matters more than convenience.
7. Keep `--session-id` when you care about traceability back to Pi sessions.

---

## Minimal command set

```bash
pi-memory state <project>
pi-memory query --type decision --limit 10
pi-memory search "keyword"
pi-memory sync MEMORY.md --limit 15
```

---

## Logging patterns

### Decision
```bash
pi-memory log decision "Adopt X" \
  --choice "Use X in the production path" \
  --rationale "Lower complexity and proven behavior" \
  --tags "architecture"
```

### Finding
```bash
pi-memory log finding "SDK exposes model_select event" \
  --source "docs/extensions.md" \
  --category "architecture" \
  --confidence verified
```

### Lesson
```bash
pi-memory log lesson "Compaction resumed without user intent" \
  --why "Intent was not captured before compaction" \
  --fix "Store last non-command input and resume with it"
```

### Entity
```bash
pi-memory log entity "SessionManager" \
  --type concept \
  --description "Pi session tree manager"
```

---

## Project attribution tips

Pi-Memory auto-detects project names from:
1. `PI_MEMORY_PROJECT`
2. git remote name / repo context
3. working directory basename

The extension also uses the `MEMORY.md` header when present.

When accuracy matters more than convenience, prefer:

```bash
pi-memory sync MEMORY.md --project my-project
pi-memory state my-project
pi-memory ingest-session session.jsonl --project my-project
```

---

## Session ingest workflow

```bash
pi-memory ingest-session ~/.pi/agent/sessions/.../session.jsonl --dry-run
pi-memory ingest-session ~/.pi/agent/sessions/.../session.jsonl
pi-memory sessions --limit 20
```

Recommended review pattern:
1. dry-run first
2. inspect extracted counts
3. ingest for real if signal quality looks reasonable
4. query recent decisions/findings/lessons/entities
5. clean up noisy records if needed

---

## Manual compaction controls inside Pi

```text
/compact-threshold
/compact-threshold 75%
/compact-threshold reset
/compact
```

Optionally record the change in memory:

```bash
pi-memory state <project> --summary "Compaction threshold set to 75%"
```

---

## Quality checks for agents

Before ending a session, ask:
- **searchability** — can critical terms be found via `search`?
- **traceability** — do important records carry `--session-id`?
- **continuity** — is `MEMORY.md` synced if the repo uses it?
- **signal quality** — did we avoid noisy/duplicate entity spam?
- **project correctness** — do records belong to the intended project?

---

## Common failure modes

- **`pi-memory` not found**
  - use the absolute binary path or set `PI_MEMORY_BIN`
- **wrong project name**
  - pass `--project` explicitly
  - or set `PI_MEMORY_PROJECT`
- **`MEMORY.md` didn’t update**
  - verify the marker pairs exist exactly
- **ingest extracted noisy records**
  - use `--dry-run` first and review counts before writing
