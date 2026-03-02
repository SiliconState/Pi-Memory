# Extension Lifecycle

File: `extensions/pi-memory-compact.ts`

This extension exists to preserve continuity around compaction and shutdown, while keeping the C binary focused on persistence.

## Event flow summary

1. `session_start`
   - restores threshold settings and internal state
2. `input`
   - captures last user intent for post-compaction resume
3. `session_before_compact`
   - best-effort `pi-memory sync MEMORY.md`
   - optional custom summarization fallback across providers
4. `session_compact`
   - stores compaction summary as finding
   - injects “continue from where we left off…” follow-up
5. `turn_end`
   - monitors context ratio, triggers early compaction at configured threshold
6. `model_select`
   - detects smaller context-window switch and triggers safety compaction if needed
7. `session_shutdown`
   - `ingest-session`
   - `state --summary ...`
   - `sync MEMORY.md`

## Resilience behavior

- All pi-memory calls are best-effort (non-fatal on failure)
- Compaction fallback rotates across configured models on quota/auth/provider issues
- Includes timeout controls via env vars

## Key environment variables

- `PI_MEMORY_BIN` — explicit binary path override
- `PI_COMPACT_THRESHOLD` — default `0.6`
- `PI_COMPACTION_MODEL_TIMEOUT_MS`
- `PI_COMPACTION_WATCHDOG_MS`
- `PI_COMPACTION_MAX_MODEL_ATTEMPTS`
- `PI_COMPACTION_FALLBACK_MODELS` (comma-separated)

## Why this split matters

- Core durability and query logic remain in C/SQLite
- Session UX and model-aware behavior remain in TypeScript extension hooks
- Each layer stays small and testable
