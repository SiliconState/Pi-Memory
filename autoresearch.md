# Autoresearch: Pi-Memory Quality & Ecosystem Fit

## Objective
Improve Pi-Memory v2.2.0 to be maximally complementary to the Pi coding agent ecosystem.
Focus areas:
1. **Correctness** — find and fix bugs in C code, edge cases, error handling
2. **Performance** — optimize hot paths (query, search, ingest-session, sync)
3. **Robustness** — improve error handling, Windows edge cases, concurrent access
4. **Ecosystem fit** — ensure the extension, skill, and prompts work seamlessly with Pi's APIs

## Metrics
- **Primary**: smoke_test_pass_rate (count, higher is better) — number of tests passing in the stress test suite
- **Secondary**: compile_warnings (count), benchmark_time_ms (ms), code_quality_issues (count)

## How to Run
`./autoresearch.sh` — outputs `METRIC name=number` lines.

## Files in Scope

### C core (native/)
- `native/pi-memory.c` — main CLI, all commands, JSONL parser, sync engine (~2936 lines)
- `native/compat.h` — cross-platform POSIX/Windows shim layer
- `native/getopt_port.h` — portable getopt_long for MSVC
- `native/Makefile` — Unix build
- `native/Makefile.win` — Windows MSVC build

### Extension
- `extensions/pi-memory-compact.ts` — Pi lifecycle extension (compaction, failover, session management)

### Scripts
- `scripts/setup.mjs` — install helper (prebuilt or compile)
- `scripts/postinstall.mjs` — npm postinstall
- `scripts/doctor.mjs` — health checks
- `scripts/test-smoke.mjs` — cross-platform smoke test

### Package metadata
- `package.json` — package config, pi integration
- `skills/memory/SKILL.md` — agent skill definition
- `prompts/*.md` — reusable prompt templates

## Off Limits
- `native/sqlite3.c` / `native/sqlite3.h` — upstream SQLite amalgamation, do not modify
- `~/.pi/agent/extensions/memory-compact.ts` — user's custom extension (personal config)
- Do not change the version number (stay at 2.2.0)

## Constraints
- All existing smoke tests must pass after changes
- No new runtime dependencies
- C code must compile cleanly with `-Wall -Wextra -Wpedantic` on macOS
- Extension must use only documented Pi APIs (plus the undocumented but verified `auto_retry_end`)
- Changes should be backward-compatible with existing `memory.db` databases

## What's Been Tried
(updated as experiments run)
