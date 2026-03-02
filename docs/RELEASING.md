# Releasing

## Versioning

- Patch: bugfix/docs/packaging reliability
- Minor: backward-compatible features
- Major: breaking CLI or schema behavior

## Release steps

1. Update `package.json` version.
2. Update `CHANGELOG.md`.
3. Run:
   ```bash
   npm run setup
   npm run doctor
   npm run test:smoke
   ```
4. Commit + tag:
   ```bash
   git tag vX.Y.Z
   git push origin main --tags
   ```
5. (When npm publishing is live) Publish to npm:
   ```bash
   npm publish --access public
   ```

> npm/bun publish is planned but not currently live. For now, users install via `pi install git:github.com/SiliconState/Pi-Memory`.

## Post-release checks

- `pi install git:github.com/SiliconState/Pi-Memory` (primary install path)
- `~/.pi/memory/pi-memory --version`
- verify extension hooks and ingest path in a real Pi session
