# Extension Lifecycle

File: `extensions/pi-memory-compact.ts`

This extension exists to preserve continuity around compaction and shutdown while keeping the native C binary focused on persistence.

---

## Event flow summary

1. `session_start`
   - restores compact-threshold state from session entries
2. `input`
   - captures the last meaningful user intent for post-compaction resume
3. `session_before_compact`
   - best-effort `pi-memory sync MEMORY.md`
   - builds the compaction summary via model fallback logic
4. `session_compact`
   - stores the compaction summary as a finding
   - auto-resumes only for auto-triggered compactions
5. `auto_retry_end`
   - if the active provider/model ultimately fails with a transient error, tries to switch to another eligible provider/model
6. `turn_end`
   - monitors context ratio
   - triggers early compaction when threshold is exceeded
   - updates the status bar context meter
7. `model_select`
   - warns or compacts when switching into a smaller context window
8. `session_shutdown`
   - ingests the current session JSONL
   - updates project state
   - syncs `MEMORY.md`

The shutdown hook uses `ctx.sessionManager.getSessionFile()` to ingest the exact JSONL backing the current session.

---

## Resilience behavior

- all `pi-memory` calls are best-effort in lifecycle hooks
- binary resolution prefers `PI_MEMORY_BIN`, then the user `.pi/memory` location, then PATH
- command launch fallback handles missing-command and permission-style failures
- compaction summarization rotates across eligible models/providers
- transient provider failures can trigger a failover bridge after final retry exhaustion
- timeouts protect external summary generation and install/runtime helper calls

---

## Project-key behavior

The extension resolves project names from:
1. `PI_MEMORY_PROJECT`
2. `MEMORY.md` header (`# Memory — <project>`)
3. current working directory basename

This keeps extension-triggered `sync`, `state`, and ingest operations closer to the native CLI’s project-detection behavior.

---

## Manual runtime controls

Inside an active Pi session:

```text
/compact-threshold          # show current threshold
/compact-threshold 75%      # set threshold
/compact-threshold reset    # restore default
/compact                    # force immediate compaction
```

The extension persists threshold changes into session custom entries and restores them on `session_start`.

---

## Key environment variables

- `PI_MEMORY_BIN` — explicit binary path override
- `PI_MEMORY_PROJECT` — explicit project override
- `PI_COMPACT_THRESHOLD` — default `0.6`
- `PI_COMPACTION_MODEL_TIMEOUT_MS`
- `PI_COMPACTION_WATCHDOG_MS`
- `PI_COMPACTION_MAX_MODEL_ATTEMPTS`
- `PI_COMPACTION_FALLBACK_MODELS` — comma-separated fallback model list

---

## Why this split matters

- persistence and schema logic stay in C/SQLite
- session UX and provider/model behavior stay in TypeScript
- each layer stays small, debuggable, and replaceable
