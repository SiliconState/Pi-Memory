# Pi-Memory (v2)

> **Pi package + native memory engine** for the [Pi coding agent](https://github.com/badlogic/pi-mono).
> Install Pi first, then install Pi-Memory.

Pi-Memory gives Pi agents a durable local memory layer built from three small pieces:
- a **native C CLI** for persistence and queries
- a **Pi extension** for compaction/session lifecycle automation
- a **`MEMORY.md` bridge** for human-readable project continuity

Repository: https://github.com/SiliconState/Pi-Memory

---

## Why this exists

Pi session JSONL files are excellent for forensics, but they are not optimized for fast recall.
Pi-Memory adds a second layer:

- **raw session JSONL** for full history
- **structured memory** for durable decisions, findings, lessons, entities, and project state

That makes long-running work easier to resume after compaction, handoff, or reboot.

---

## Three-layer model

| Layer | Responsibility | Location |
|---|---|---|
| Native core | Structured persistence + query/search/export/sync | `~/.pi/memory/pi-memory` / `%USERPROFILE%\.pi\memory\pi-memory.exe` |
| SQLite store | Durable source of truth | `~/.pi/memory/memory.db` / `%USERPROFILE%\.pi\memory\memory.db` |
| Project bridge | Human-readable context projection | `MEMORY.md` in your project |

---

## Platform support

Pi-Memory has **native source support** for:
- macOS
- Linux
- Windows

### Current prebuilt status on this branch

| Platform | Native support | In-repo prebuilt |
|---|---:|---:|
| macOS arm64 | ✅ | ✅ |
| macOS x64 | ✅ | ✅ |
| Linux x64 | ✅ | release/CI artifact or compile fallback |
| Linux arm64 | ✅ | release/CI artifact or compile fallback |
| Windows x64 | ✅ | compile fallback today; prebuilt can be shipped by release workflow |

If a matching prebuilt is absent, `scripts/setup.mjs` automatically falls back to compiling from source.

---

## Install

### Recommended for Pi users

```bash
pi install git:github.com/SiliconState/Pi-Memory
```

This is the primary install path because Pi reads the package metadata and installs:
- the extension
- the skill
- the prompts
- the native binary via `postinstall`

Verify:

```bash
~/.pi/memory/pi-memory --version
```

On Windows:

```powershell
$env:USERPROFILE\.pi\memory\pi-memory.exe --version
```

### npm / bun readiness

The package structure is ready for **npm** and **bun** installs from git or a packed tarball.

Examples:

```bash
npm install github:SiliconState/Pi-Memory
bun add github:SiliconState/Pi-Memory
```

Notes:
- `postinstall` runs `scripts/setup.mjs`
- prebuilt binaries are used when present
- otherwise the package compiles from source
- **Pi users should still prefer `pi install git:...`** so Pi registers the extension/skill/prompt metadata
- npm publish is not live yet

### Unix convenience installer

```bash
curl -fsSL https://raw.githubusercontent.com/SiliconState/Pi-Memory/main/scripts/install.sh | bash
```

This helper is **Unix-only**.

---

## Quick start

### Binary path

If `pi-memory` is not on your PATH, use one of these:

- macOS / Linux: `~/.pi/memory/pi-memory`
- Windows: `%USERPROFILE%\.pi\memory\pi-memory.exe`

### Basic usage

```bash
BIN="${PI_MEMORY_BIN:-$HOME/.pi/memory/pi-memory}"

# initialize project memory markers
"$BIN" init

# write memory
"$BIN" log decision "Use SQLite for agent memory" \
  --choice "Single local database" \
  --rationale "Simple, durable, low operational overhead"

"$BIN" log finding "Extension syncs MEMORY.md before compaction" \
  --category architecture \
  --confidence verified

"$BIN" log lesson "Wrong project attribution caused noisy sync output" \
  --fix "Pass --project explicitly when needed"

"$BIN" log entity "SessionManager" --type concept --description "Pi session tree manager"

# inspect memory
"$BIN" query --limit 20
"$BIN" search "compaction"
"$BIN" state <project>

# project bridge
"$BIN" sync MEMORY.md --limit 15
```

For PowerShell, use the Windows binary path directly or set `PI_MEMORY_BIN`.

---

## What Pi-Memory stores

Primary record types:
- **decisions** — what was chosen and why
- **findings** — facts, discoveries, compaction summaries
- **lessons** — failures and fixes
- **entities** — named tools, services, concepts, contracts
- **sessions** — ingested Pi session metadata and stats
- **project_state** — current phase, summary, next actions, rollups

Session ingest and shutdown automation also carry:
- `session_id` links across records
- token/cost rollups
- compaction summaries
- auto-extracted decisions/lessons/entities from session JSONL

---

## Session JSONL vs Pi-Memory

| Dimension | Pi session JSONL | Pi-Memory |
|---|---|---|
| Purpose | Complete transcript | Curated durable memory |
| Noise level | High | Low / intentional |
| Scope | One session | Cross-session / cross-project |
| Best use | Replay, auditing, debugging | Recall, continuity, handoff |

Think of it as:
- **JSONL = camera footage**
- **Pi-Memory = engineering notebook**

You want both.

---

## Compaction / continuity behavior

The extension automates continuity around compaction and shutdown:
- syncs `MEMORY.md` before compaction
- stores compaction summaries as findings
- captures the latest user intent for auto-resume after auto-compactions
- ingests the current session JSONL on shutdown
- updates `project_state`
- syncs `MEMORY.md` again at shutdown
- performs provider/model failover for compaction-related resilience

Inside Pi you can also use:

```text
/compact-threshold
/compact-threshold 75%
/compact-threshold reset
/compact
```

---

## Development

```bash
npm run setup
npm run doctor
npm run test:smoke
npm pack --dry-run
```

### Native builds

**macOS / Linux**
```bash
cd native && make
```

**Windows (MSVC Developer Command Prompt)**
```cmd
cd native
nmake /F Makefile.win
```

---

## Documentation

- [Install Guide](docs/INSTALL.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Extension Lifecycle](docs/EXTENSION.md)
- [Agent Usage Guide](docs/AGENT-USAGE.md)
- [Contributing](CONTRIBUTING.md)
- [Security Policy](SECURITY.md)

---

## License

MIT
