# Autoresearch: Pi-Memory C Quality, Robustness, and Performance

## Objective
Improve the native C implementation in Pi-Memory v2.2.0 with an emphasis on:
1. **Correctness and robustness** — fix edge cases, NULL handling, buffer safety, file I/O failures, and JSONL/session-ingest parsing failures.
2. **Performance** — speed up common native operations, especially `query`, `search`, `sync`, and `ingest-session`.
3. **Code quality** — eliminate unsafe patterns, reduce duplication where it improves maintainability, and keep the code warning-clean under strict compiler flags.

## Metrics
- **Primary**: `benchmark_time_ms` (ms, lower is better)
- **Secondary**:
  - `smoke_test_pass_rate` (count, must stay at 16)
  - `compile_warnings` (count, target 0)
  - `code_quality_issues` (count, target 0)

## How to Run
`./autoresearch.sh` — compiles the native binary, runs a timed native workload, runs the 16-step smoke suite, and outputs structured `METRIC` lines.

## Files in Scope
### Native core
- `native/pi-memory.c` — main CLI, SQLite operations, sync engine, JSONL/session ingestion
- `native/compat.h` — cross-platform POSIX/Windows shim layer
- `native/getopt_port.h` — Windows getopt portability layer
- `native/Makefile` — Unix native build
- `native/Makefile.win` — Windows MSVC build

### Benchmark / session context
- `autoresearch.md` — session operating context
- `autoresearch.sh` — benchmark driver for this optimization loop
- `scripts/test-smoke.mjs` — correctness regression suite used by the benchmark

## Off Limits
- `native/sqlite3.c` / `native/sqlite3.h` — upstream SQLite amalgamation, do not modify
- Do not change the version number (must remain `2.2.0`)
- No new runtime dependencies

## Constraints
- All 16 smoke tests must continue to pass
- Native code must compile cleanly with `-Wall -Wextra -Wpedantic`
- Changes must be backward-compatible with existing `memory.db` databases
- Prefer simple, auditable fixes over risky rewrites

## What’s Been Tried
- Session reset on 2026-03-23 to focus specifically on native C quality/robustness/performance rather than ecosystem-fit work.
- Benchmark updated to time native operations relevant to this goal (`log`, `query`, `search`, `export`, `state`, `sync`, `ingest-session`) while still enforcing the 16-step smoke suite.
- Baseline established at `benchmark_time_ms=460` with full smoke pass (`16/16`).
- **Kept:** added explicit `SCHEMA_VERSION` usage in migration path and removed `db_exists` gating so legacy column/index migrations always run when `user_version` is behind.
- **Kept:** reordered DB open path to avoid unnecessary WAL initialization on already-upgraded schemas, while preserving migration safety and schema init correctness.
- Observed benchmark noise and occasional compile-stage instability when concurrent agents touch the same branch; treat single-run wins carefully and prefer repeated confirmation runs.
