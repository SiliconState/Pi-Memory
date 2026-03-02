---
name: memory
description: Persist and retrieve project knowledge using pi-memory (SQLite at ~/.pi/memory/memory.db). Use when you need to log an architectural decision, record a finding or lesson, check what was decided before, sync a MEMORY.md file with live DB content, bootstrap memory for a new project, or ingest a Pi session file to extract metadata, decisions, lessons, and entities. Works across all projects — project is auto-detected from git remote or cwd. All log commands accept --session-id to cross-reference entries to Pi sessions.
---

# pi-memory v2.1 — Durable Agent Memory

A single compiled binary (`~/.pi/memory/pi-memory`) backed by SQLite.
Works across every project. Survives context resets, compactions, and machine reboots.

## Binary Location

```
~/.pi/memory/pi-memory        (installed binary)
~/.pi/memory/memory.db        (database)
```

If `pi-memory` is not in PATH, run `~/.pi/memory/pi-memory ...` directly.

## Project Auto-Detection

When `--project` is omitted, the project is resolved in this order:
1. `PI_MEMORY_PROJECT` environment variable
2. `git remote get-url origin` → repo name (last path component, no `.git`)
3. `basename` of current working directory
4. `"global"` fallback

**You almost never need to type `--project` when working inside a git repo.**

## Bootstrap a New Project

```bash
# From inside the project directory:
pi-memory init

# With explicit name or custom file:
pi-memory init my-project --file docs/MEMORY.md
```

Creates a `MEMORY.md` with live sync markers. Does not overwrite if the file already exists.

## Log a Decision

Use immediately after making any architectural, technical, or process choice.

```bash
pi-memory log decision "Title summarising the choice" \
  --choice     "Exactly what was decided" \
  --rationale  "Why this over alternatives" \
  --context    "Why the decision was needed at all" \
  --alternatives "Other options that were considered" \
  --consequences "What this opens up or closes off" \
  --tags       "security,api,contracts" \
  --project    <proj>          # omit to auto-detect
  --session-id <uuid>          # auto-set by extension
```

**When to log:**
- Any library/framework selection
- Any API contract or schema change
- Any security-relevant implementation choice
- Any trade-off accepted knowingly

## Log a Finding

Facts discovered during research, testing, or reading code.

```bash
pi-memory log finding "The fact in one sentence" \
  --source     "URL or file:line" \
  --category   "architecture|security|performance|spec|gotcha|compaction-summary" \
  --confidence "verified|assumption|unverified" \
  --tags       "contracts,sdk" \
  --project    <proj>
  --session-id <uuid>
```

## Log a Lesson

Post-mortems on bugs, reviewer failures, or wasted effort.

```bash
pi-memory log lesson "What broke or was wrong" \
  --why  "Root cause" \
  --fix  "What to do instead next time" \
  --tags "sdk,review" \
  --project <proj>
  --session-id <uuid>
```

## Log an Entity

Named things — tools, services, contracts, people, concepts.

```bash
pi-memory log entity "TreasuryFactory" \
  --type        "service|tool|contract|framework|concept|person" \
  --description "One-line description" \
  --notes       "Anything that doesn't fit above" \
  --project     <proj>
  --session-id  <uuid>
```

Entities use `UPSERT` — re-running the same name updates in-place.

## Read Memory

```bash
# Current project state (shows session rollup stats)
pi-memory state                         # auto-detected project
pi-memory state my-project              # explicit

# Update state
pi-memory state my-project \
  --phase   "Week 3 — SDK" \
  --summary "Interceptor done, client timeout fix pending" \
  --next    "Fix timeout|Update types|Re-run review"

# Query decisions/findings/lessons (supports session filtering)
pi-memory query --limit 20
pi-memory query --type decision --limit 10
pi-memory query --type lesson --session-id "783b05c6-..."

# Full-text search across all projects (supports session filtering)
pi-memory search "nonce"
pi-memory search "timeout" --project my-project
pi-memory search "factory" --session-id "162cf943-..."

# Most recent N entries across all projects (shows session IDs)
pi-memory recent --n 20

# List all projects with record counts
pi-memory projects

# Export everything as Markdown or JSON (includes session IDs)
pi-memory export --format md
pi-memory export --format json > memory-backup.json
```

## Sync MEMORY.md

If the project has a `MEMORY.md` with these HTML comment markers:

```
<!-- pi-memory:decisions:start --> ... <!-- pi-memory:decisions:end -->
<!-- pi-memory:state:start -->     ... <!-- pi-memory:state:end -->
```

Run this to replace the content between them with live DB data:

```bash
pi-memory sync MEMORY.md                         # auto-detected project, limit 10
pi-memory sync MEMORY.md --project my-project --limit 15
```

**When to sync:** At the start of every work session, and after logging new decisions.

## Environment Variable Pin

To pin a project name in your shell, `.env`, or script:

```bash
export PI_MEMORY_PROJECT=my-project
pi-memory log decision "..."    # no --project needed
pi-memory sync MEMORY.md        # no --project needed
```

## Ingest a Pi Session (v2.1)

Parse a Pi session `.jsonl` file and extract metadata + semantic content into pi-memory.

```bash
# Dry run — see what would be extracted without writing
pi-memory ingest-session ~/.pi/agent/sessions/.../.../session.jsonl --dry-run

# Ingest for real
pi-memory ingest-session ~/.pi/agent/sessions/.../.../session.jsonl

# Override project detection
pi-memory ingest-session session.jsonl --project my-project
```

### What it extracts:
- **Session metadata:** UUID, project, cwd, model, provider, timestamps
- **Stats:** total tokens, cost, message/user/assistant/tool counts, compaction count
- **Compaction summaries** → stored as `finding(category=compaction-summary)`
- **Auto-decisions:** detected from assistant messages containing decision language ("decided to", "chose to", "we will use", etc.)
- **Auto-lessons:** error→fix pairs where a tool error is followed by an assistant fix
- **Auto-entities:** CamelCase identifiers, tool names, and domain concepts extracted from conversation text
- **Project rollup:** updates `project_state` with session_count, total_tokens, total_cost, last_model/provider

### Idempotency:
- Re-ingesting the same session updates the session record
- Compaction summaries are content-deduplicated (not just count-based)
- Decisions and lessons are deduplicated by title+choice or what_failed+fix
- Entities are upserted (existing records updated, not duplicated)

The extension auto-ingests the current session on shutdown. Use this command for historical sessions.

## List Ingested Sessions

```bash
pi-memory sessions                          # all projects
pi-memory sessions --project my-project     # filter by project
pi-memory sessions --limit 50              # more results
```

Shows: session ID, project, model, token count, cost, message count, date.

## Session Cross-References

All `log` commands accept `--session-id <uuid>` to link entries back to specific Pi sessions:

```bash
pi-memory log decision "Title" --choice "..." --session-id "783b05c6-..."
pi-memory log finding  "Fact"  --session-id "783b05c6-..."
pi-memory log lesson   "Bug"   --session-id "783b05c6-..."
pi-memory log entity   "Name"  --session-id "783b05c6-..."
```

Query and search both support `--session-id` filtering:

```bash
pi-memory query --type lesson --session-id "162cf943-..."
pi-memory search "factory" --session-id "162cf943-..."
```

The extension automatically passes `--session-id` on all `pi-memory log` calls.

## Automatic Behaviors (via memory-compact extension)

### Before compaction:
- Syncs MEMORY.md with live DB data

### After compaction:
- Stores the compaction summary as a finding in pi-memory

### On model switch:
- If the new model has a **smaller context window** and context is already above the compaction threshold, triggers **immediate compaction**
- If context is within 10% of threshold, shows a warning notification
- Prevents the "context at 75%" scenario when switching from codex (272k) to opus (200k)

### On session shutdown:
- Auto-ingests the current session file (extracts decisions, lessons, entities)
- Updates project state with session summary
- Syncs MEMORY.md

### Context monitoring:
- `turn_end` hook monitors context usage and triggers early compaction at 60% (configurable)
- Status bar shows real-time context usage with a visual bar
- Use `/compact-threshold` command to adjust (e.g., `/compact-threshold 75%`)

### All log calls:
- Automatically include `--session-id` for cross-referencing

## Schema (v2.1)

### Tables:
- `decisions` — architectural/technical choices (+ session_id)
- `findings` — facts, discoveries, compaction summaries (+ session_id)
- `lessons` — error post-mortems (+ session_id)
- `entities` — named things with type/description
- `sessions` — ingested session metadata with tokens/cost/model
- `project_state` — per-project phase/summary/next + session rollup stats

### Indexes:
- `idx_decisions_session_id`, `idx_findings_session_id`, `idx_lessons_session_id` — fast session-filtered queries
- `idx_sessions_project` — fast project-filtered session listing

## Workflow: Starting a Session

```bash
# 1. See what project pi-memory thinks you're in
pi-memory projects

# 2. Check current state (includes session stats)
pi-memory state

# 3. See recent decisions
pi-memory query --type decision --limit 10

# 4. Sync MEMORY.md if present
pi-memory sync MEMORY.md 2>/dev/null || true
```

## Workflow: Ending a Session

```bash
# 1. Log any decisions made
pi-memory log decision "..." --choice "..." --rationale "..."

# 2. Update project state
pi-memory state --phase "..." --summary "..." --next "task1|task2"

# 3. Sync MEMORY.md
pi-memory sync MEMORY.md

# 4. Log any lessons from failures
pi-memory log lesson "..." --why "..." --fix "..."
```

## Workflow: Batch Ingest Historical Sessions

```bash
# Ingest all sessions for a project
find ~/.pi/agent/sessions -name "*.jsonl" -type f | while read f; do
  pi-memory ingest-session "$f"
done

# Check results
pi-memory projects
pi-memory sessions --project my-project
```

## Rebuild the Binary

If you modify `~/.pi/memory/pi-memory.c`:

```bash
cd ~/.pi/memory
cc -O2 -Wall -Wextra -o /usr/local/bin/pi-memory pi-memory.c -lsqlite3
pi-memory help
```
