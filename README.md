# Pi-Memory (v2)

Durable memory for Pi agents, built for **low-overhead reliability**:
- **C core** (`native/pi-memory.c`) for speed + minimal runtime surface
- **TypeScript extension** (`extensions/pi-memory-compact.ts`) for Pi lifecycle automation
- **SQLite DB** (`~/.pi/memory/memory.db`) for single-file durability and simple ops

Repository: https://github.com/SiliconState/Pi-Memory

---

## Why C + SQLite

Pi-Memory is intentionally small:
- no server process
- no external DB
- no runtime framework dependency for core memory engine
- one local DB: `~/.pi/memory/memory.db`

That keeps startup fast, failure modes simple, and behavior predictable.

---

## Install

### One-command install (recommended)

```bash
pi install git:github.com/SiliconState/Pi-Memory
```

This installs all Pi components declared in `package.json`:
- extension (`extensions/pi-memory-compact.ts`)
- skill (`skills/memory/SKILL.md`)
- prompts (`prompts/*.md`)
- native C binary compile via postinstall

### Alternative one-liner installer (curl)

```bash
curl -fsSL https://raw.githubusercontent.com/SiliconState/Pi-Memory/main/scripts/install.sh | bash
```

### npm package (after publish)

```bash
pi install npm:@siliconstate/pi-memory
```

Then verify:

```bash
~/.pi/memory/pi-memory --version
```

### npm global

```bash
npm i -g @siliconstate/pi-memory
pi-memory-setup
pi-memory-doctor
```

### bun global

```bash
bun add -g @siliconstate/pi-memory
pi-memory-setup
pi-memory-doctor
```

> If native compile is skipped during install, run `pi-memory-setup` manually.

---

## Quick Start

```bash
# initialize MEMORY.md markers
pi-memory init

# log memory records
pi-memory log decision "Use SQLite for memory" --choice "Single local DB" --rationale "Simple + durable"
pi-memory log finding "Extension hooks run at session_shutdown" --category architecture --confidence verified
pi-memory log lesson "Forgot to sync MEMORY.md" --fix "Run sync before compaction"
pi-memory log entity "SessionManager" --type concept --description "Pi session tree manager"

# inspect
pi-memory query --limit 20
pi-memory search "compaction"
pi-memory state

# sync docs
pi-memory sync MEMORY.md --limit 15
```

---

## What ships in this package

- `native/pi-memory.c` — core C CLI (SQLite-backed)
- `extensions/pi-memory-compact.ts` — Pi extension
- `skills/memory/SKILL.md` — memory skill
- `prompts/*.md` — reusable prompt templates
- `scripts/setup.mjs` — native build/install helper

---

## Documentation

- [Install Guide](docs/INSTALL.md)
- [Architecture](docs/ARCHITECTURE.md)
- [Extension Lifecycle + Hooks](docs/EXTENSION.md)
- [Agent Usage Guide](docs/AGENT-USAGE.md)
- [Contributing](CONTRIBUTING.md)
- [Security Policy](SECURITY.md)

---

## Development

```bash
npm run setup
npm run doctor
npm run test:smoke
```

---

## License

MIT
