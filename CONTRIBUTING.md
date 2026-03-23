# Contributing

Thanks for contributing to Pi-Memory.

## Philosophy

- keep the code small
- preserve the C + SQLite core simplicity
- prefer reliability over cleverness
- keep cross-platform behavior explicit and testable

---

## Local development

```bash
npm run setup
npm run doctor
npm run test:smoke
npm pack --dry-run
```

Notes:
- there are no runtime npm dependencies for the core memory engine
- `npm run setup` copies a matching prebuilt when available, otherwise compiles from source
- `bun run setup` is also supported

---

## Repo structure

- `native/` — C source + makefiles + SQLite amalgamation
- `extensions/` — Pi extension(s)
- `skills/` — Pi skill(s)
- `prompts/` — prompt templates
- `docs/` — user/developer docs
- `prebuilds/` — platform-specific prebuilt binaries when committed

---

## Pull request process

1. Open an issue first for non-trivial changes.
2. Keep the PR scoped to one concern.
3. Include before/after behavior.
4. Update docs for user-visible behavior.
5. Run the validation steps and include output.
6. Avoid bundling research artifacts or unrelated local tooling into the PR.

---

## PR checklist

- [ ] No unrelated files changed
- [ ] Native build still succeeds cleanly
- [ ] `npm run doctor` passes
- [ ] `npm run test:smoke` passes
- [ ] Docs updated where needed
- [ ] Backward compatibility considered
- [ ] Cross-platform impact considered (macOS / Linux / Windows)

---

## Coding guidelines

### C
- prefer straightforward control flow over abstraction layers
- keep allocations bounded and obvious
- use prepared statements for SQL
- do not modify the bundled SQLite amalgamation unless intentionally updating upstream
- preserve compatibility with existing `memory.db` files

### Extension / JS / TS
- keep lifecycle hooks best-effort and non-fatal
- use timeouts on external calls
- avoid hardcoded provider assumptions where generic behavior is possible
- keep install/runtime helpers consistent with `scripts/setup.mjs`
