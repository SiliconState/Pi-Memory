You are a research agent for Pi-Memory v2.2.0. Your job is to improve the TypeScript extension, skill definition, and overall Pi ecosystem integration.

## Your focus: Extension quality, ecosystem fit, and documentation

Read these files first:
- `extensions/pi-memory-compact.ts` — the Pi lifecycle extension
- `skills/memory/SKILL.md` — the agent skill definition
- `prompts/memory-sync.md` and `prompts/memory-ingest-session.md` — prompt templates
- `package.json` — pi integration config
- `autoresearch.md` — session context
- `README.md` — project documentation

Then use `/autoresearch` to start an autonomous experiment loop targeting:
1. **Extension robustness** — improve error handling in execPiMemory, timeout edge cases, race conditions in compaction locking
2. **SKILL.md quality** — make the skill definition more useful for agents (better examples, clearer command reference, edge case documentation)
3. **Prompt template improvements** — make prompts more effective and reusable
4. **Package integration** — ensure the pi config in package.json is optimal, test that all components are correctly declared
5. **Documentation accuracy** — verify README matches actual behavior, update any stale references

Run `./autoresearch.sh` as your benchmark. It measures:
- smoke_test_pass_rate (must stay at 16 — all tests pass)
- compile_warnings (target: 0)
- benchmark_time_ms (lower is better)
- code_quality_issues (target: 0)

## Constraints
- Do NOT modify `native/sqlite3.c` or `native/sqlite3.h`
- All 16 smoke tests must continue to pass
- Extension must use only verified Pi APIs
- Do not add hardcoded model/provider preferences (keep failover generic)
- Stay on version 2.2.0

Start with `/autoresearch optimize extension quality and Pi ecosystem fit for pi-memory, focus on robustness and skill definition`.
