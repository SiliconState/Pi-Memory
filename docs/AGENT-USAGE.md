# Agent Usage Guide

This guide is for agent authors/operators using Pi-Memory in Pi workflows.

If `pi-memory` is not on PATH in your environment, call it explicitly as `~/.pi/memory/pi-memory`.

## Core operating rules

1. Log decisions when choices are made, not after the session ends.
2. Log findings with confidence and source whenever possible.
3. Log lessons from failures immediately after fix confirmation.
4. Keep entities high-signal (tools, services, contracts, concepts).
5. Sync `MEMORY.md` before and after major compaction or handoff.

## Minimal command set

```bash
pi-memory state <project>
pi-memory query --type decision --limit 10
pi-memory search "<keyword>"
pi-memory sync MEMORY.md --limit 15
```

## Logging patterns

### Decision
```bash
pi-memory log decision "Adopt X" \
  --choice "Use X in production path" \
  --rationale "Lower complexity + proven behavior" \
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
  --why "No intent capture before compact" \
  --fix "Store last non-command input and resume with it"
```

## Manual compaction control during session

Inside Pi:

```text
/compact-threshold
/compact-threshold 75%
/compact-threshold reset
/compact
```

Optionally record the decision in memory:

```bash
pi-memory state <project> --summary "Compaction threshold set to 75%"
```

## Session ingest workflow

```bash
pi-memory ingest-session ~/.pi/agent/sessions/.../session.jsonl --dry-run
pi-memory ingest-session ~/.pi/agent/sessions/.../session.jsonl
pi-memory sessions --limit 20
```

## Quality checks for agents

- searchability: can critical terms be found via `search`?
- traceability: do records include `--session-id` where available?
- continuity: does `MEMORY.md` contain updated decision/state markers?
- precision: avoid noisy or duplicate entity spam
