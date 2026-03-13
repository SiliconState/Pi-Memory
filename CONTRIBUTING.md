# Contributing

Thanks for contributing to Pi-Memory.

## Philosophy

- Keep code small.
- Preserve C + SQLite core simplicity.
- Add features only when they improve reliability or usability materially.

## Local dev

```bash
npm run setup
npm run doctor
npm run test:smoke
```

> There are no npm dependencies to install — the core is compiled C and the extension runs inside Pi's runtime. `npm run setup` compiles the native binary.

## Repo structure

- `native/` — C source + makefile
- `extensions/` — Pi extension(s)
- `skills/` — Pi skill(s)
- `prompts/` — prompt templates
- `docs/` — developer/user docs

## Pull request process

1. Open issue first for non-trivial changes.
2. Keep PR scoped to one concern.
3. Include before/after behavior.
4. Add or update docs for user-visible behavior.
5. Run smoke checks and include output.

## PR checklist

- [ ] No changes to unrelated files
- [ ] Native build still succeeds (`-Wall -Wextra -Wpedantic` on Unix, supported Windows compiler path still works)
- [ ] `npm run test:smoke` passes
- [ ] Docs updated
- [ ] Backward compatibility considered

## Coding guidelines

### C
- Prefer straightforward control flow over abstraction layers.
- Keep allocations bounded; avoid hidden heap complexity.
- Use prepared statements for SQL.

### Extension TS
- Best-effort behavior in lifecycle hooks (non-fatal on failure).
- Timeouts on external calls.
- Keep event handlers short and explicit.
