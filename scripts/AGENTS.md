<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# scripts

## Purpose

Every non-CMake entrypoint: vendoring the binary dependencies and packaging unsigned candidates.

## Key Files

| File | Description |
|------|-------------|
| `setup_deps.sh` | POSIX entrypoint that vendors CEF into `third_party/cef/` and Geist into `assets/fonts/`; supports `--dry-run` |
| `setup_deps.ps1` | Windows PowerShell equivalent |
| `deps.py` | CLI over the `deps/` package: resolve, install, `verify` |
| `package.py` | Builds a per-target unsigned artifact; `uv run`-compatible PEP 723 header, stdlib only |
| `version.py` | Version tooling over the root `VERSION` file: `show`, `bump major\|minor\|patch\|pre\|release [--pre LABEL]` (rotates `CHANGELOG.md`), `sync` (site markers, `site/changelog.html`, Windows manifest), `check [--tag vX.Y.Z]`, `notes [VERSION\|vVERSION\|Unreleased]` (prints that `CHANGELOG.md` section; the release body); `REPO_URL` is the one place the GitHub repository URL is written; stdlib only |
| `release_assets.py` | Used by `build-release.yml`: verifies the six downloaded per-target archives against their own checksum lines, requires exactly one per target, and writes them plus a combined `SHA256SUMS.txt` (`<sha256>  <file name>` per line, sorted, LF) for the GitHub release; imports `Target`/`archive_name`/`SEMVER` from `package.py`; stdlib only |
| `site_set_domain.py` | Rewrites the site's canonical/SEO URLs to a new domain |
| `package_resources.py` | Declares the font/icon files staged into a packaged artifact and their digests |

That table is the complete contents of this directory on `main`. A hybrid native/container build
router (`island.py`, `container_runner.py`, `image_lock.py`, `task_model.py`, `report_writer.py`)
exists only as uncommitted work in some working trees — do not document, import, or invoke those
modules until they land.

## For AI Agents

### Working In This Directory

- `deps.py` deliberately *removes* its own directory from `sys.path` so `deps.*` resolves to the
  package at the repository root rather than to this file.
- The pytest suites reach these scripts by path (`Path(__file__).resolve().parents[2]`) and by
  package-relative import, not through a root `conftest.py` — there is no `conftest.py` on `main`.
- Stdlib only. No third-party runtime dependencies in any script here.
- `setup_deps.sh` outputs stay gitignored. Do not replace the vendoring with CMake `FetchContent`
  and do not commit the binaries.

### Testing Requirements

```bash
bash -n scripts/setup_deps.sh
./scripts/setup_deps.sh --dry-run
python3 scripts/deps.py verify
python3 -m pytest tests/package tests/deps tests/version
```

- The archive name `island_browser-<version>-<target>.<zip|tar.gz>` (`package.archive_name`) and
  the combined `SHA256SUMS.txt` format are a published contract: GitHub release assets and the
  in-browser updater depend on them. Change them only together with the updater.

### Common Patterns

- `from __future__ import annotations`, frozen `@dataclass(slots=True)` models, and `Final`
  module-level constants.
- Validation raises a typed error (`DependencyError`) rather than exiting from library code; only
  the CLI layer maps errors to exit codes.

## Dependencies

### Internal

- `deps/` — the lock model and installer that `deps.py` drives
- `resources/island/icons/`, `assets/fonts/` — the payload `package_resources.py` describes

<!-- MANUAL: -->
