<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# package

## Purpose

Python `unittest` suite for the packaging path: `scripts/package.py` (per-target archive
production) and `scripts/package_resources.py` (the font and icon resource manifest staged into
each artifact).

## Key Files

| File | Description |
|------|-------------|
| `__init__.py` | Makes this a package so tests can use `from .package_fixture import …` |
| `package_fixture.py` | `PackageFixture` dataclass: synthesizes a throwaway build tree and repository root |
| `test_package.py` | Archive layout, tar/zip contents, and hashing for each target |
| `test_package_resources.py` | Resource manifest completeness and digests |
| `test_release_assets.py` | `scripts/release_assets.py`: the six-archive release set and the combined `SHA256SUMS.txt` the in-browser updater verifies against |

## For AI Agents

### Working In This Directory

- Unlike `tests/deps/`, this directory **is** a package (`__init__.py`) because the two test modules
  share `package_fixture`. Keep relative imports relative.
- Tests shell out to `scripts/package.py` via `subprocess` against a fixture tree — never against a
  real `build/` directory.
- CI installs fixtures first ("Install package test fixtures" step) before running this suite; a
  local run without that step can fail for environmental reasons rather than real ones.

### Testing Requirements

```bash
python3 -m pytest tests/package
```

This is the "Run package unit tests" step of the `portable` job in `.github/workflows/ci.yml`.

## Dependencies

### Internal

- `scripts/package.py`, `scripts/package_resources.py`

<!-- MANUAL: -->
