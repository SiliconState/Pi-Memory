# Releasing

## Versioning

- **patch** — bug fixes, packaging fixes, docs corrections
- **minor** — backward-compatible features
- **major** — breaking CLI/schema/behavior changes

---

## Pre-release checklist

Run:

```bash
npm run setup
npm run doctor
npm run test:smoke
npm pack --dry-run
```

Recommended additional checks:
- verify `README.md` and `docs/INSTALL.md` match the real install story
- verify Windows-specific changes have been validated on Windows, not only reviewed on macOS/Linux
- confirm `prebuilds/` contents and `prebuilds/README.md` are accurate for the release branch/tag

---

## Release steps

1. Update `package.json` version.
2. Update `CHANGELOG.md`.
3. Commit release changes.
4. Tag the release:
   ```bash
   git tag vX.Y.Z
   git push origin main --tags
   ```
5. Let GitHub Actions build release artifacts.

---

## GitHub Actions workflow roles

### `ci.yml`
Runs smoke validation on:
- macOS
- Linux
- Windows

### `build.yml`
Builds prebuilt release artifacts for:
- `darwin-arm64`
- `darwin-x64`
- `linux-x64`
- `linux-arm64`
- `win32-x64`

The release workflow uploads combined prebuild artifacts and attaches binaries to GitHub Releases on version tags.

---

## Install-path validation after release

At minimum, verify:

```bash
pi install git:github.com/SiliconState/Pi-Memory
~/.pi/memory/pi-memory --version
npm pack --dry-run
```

And on Windows:

```powershell
$env:USERPROFILE\.pi\memory\pi-memory.exe --version
```

If npm publishing is enabled later, also validate:

```bash
npm publish --access public
```

At the moment, npm publish is still optional/future-facing; `pi install git:...` remains the primary supported user path.
