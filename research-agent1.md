You are a research agent for Pi-Memory v2.2.0. Your job is to find and fix bugs, improve error handling, and harden the C code.

## Your focus: C code quality, robustness, and correctness

Read these files first:
- `native/pi-memory.c` — the main CLI (~2936 lines of C)
- `native/compat.h` — cross-platform shims
- `native/getopt_port.h` — Windows getopt port
- `autoresearch.md` — session context

Then use `/autoresearch` to start an autonomous experiment loop targeting:
1. **Bug fixes** — memory leaks, buffer overflows, missing NULL checks after malloc, uninitialized variables
2. **Error handling** — graceful failures, better error messages, edge cases in JSONL parsing
3. **Windows robustness** — verify compat.h coverage, path separator handling, test with edge-case paths
4. **Performance** — optimize hot paths in query/search/sync/ingest-session (reduce unnecessary allocations, improve SQL queries)
5. **Code quality** — replace any remaining sprintf with snprintf, add bounds checks, reduce code duplication

Run `./autoresearch.sh` as your benchmark. It measures:
- smoke_test_pass_rate (must stay at 16 — all tests pass)
- compile_warnings (target: 0)
- benchmark_time_ms (lower is better)
- code_quality_issues (target: 0)

## Constraints
- Do NOT modify `native/sqlite3.c` or `native/sqlite3.h`
- All 16 smoke tests must continue to pass
- Must compile cleanly with `-Wall -Wextra -Wpedantic`
- Changes must be backward-compatible with existing databases
- Stay on version 2.2.0

Start with `/autoresearch optimize C code quality and performance for pi-memory, focus on bug fixes and robustness`.
