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
5. Publish to npm:
   ```bash
   npm publish --access public
   ```

## Post-release checks

- `pi install npm:@siliconstate/pi-memory@X.Y.Z`
- `pi-memory-doctor`
- verify extension hooks and ingest path in a real Pi session
