<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# deps

## Purpose

Python `unittest` suite for the dependency resolver in `deps/` and the `scripts/deps.py` CLI:
lock-file parsing, target resolution, archive validation, atomic installation, and offline
verification.

## Key Files

| File | Description |
|------|-------------|
| `test_resolver.py` | Resolution, download, hashing, tar extraction, and CLI behaviour |

## For AI Agents

### Working In This Directory

- There is no `__init__.py` here; tests are collected by pytest from the repository root and import
  `deps.*` as a package plus `scripts/deps.py` via `importlib.util`.
- Tests must stay hermetic: build fixtures with `tempfile` and `tarfile` rather than hitting the
  network or touching the real `third_party/` and `assets/` trees.
- Any change to `deps/dependencies.lock.json` shape needs a matching assertion here — this suite is
  the CI gate that catches lock drift before a build does.

### Testing Requirements

```bash
python3 -m pytest tests/deps
```

This is the "Run dependency unit tests" step of the `portable` job in `.github/workflows/ci.yml`.

## Dependencies

### Internal

- `deps/model.py`, `deps/install.py`, `deps/updates.py`, `scripts/deps.py`

<!-- MANUAL: -->
