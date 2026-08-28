<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# deps

## Purpose

The dependency layer behind `scripts/setup_deps.sh` and `scripts/deps.py`: the immutable lock file,
its typed model, the downloader/installer, and read-only upstream update checks. This is what
vendors the CEF binary distribution and the Geist fonts.

## Key Files

| File | Description |
|------|-------------|
| `dependencies.lock.json` | The lock: artifact URLs, digests, and per-target mappings |
| `__init__.py` | Package marker; imports resolve as `deps.model`, `deps.install`, … |
| `model.py` | Typed lock parsing, target resolution, and the `DependencyError` base |
| `install.py` | Download, archive validation, atomic installation, offline verification |
| `updates.py` | Read-only checks against official upstreams; raises `UpdateError` |

## For AI Agents

### Working In This Directory

- The lock file is the single source of truth for every binary dependency. Never hardcode a URL or
  a version in a script — add it here.
- Installation must stay atomic: extract to a temp location, validate the digest, then move into
  place. A partially installed `third_party/cef/` is worse than none.
- `updates.py` is read-only by design. It reports that a newer artifact exists; it must never
  rewrite the lock on its own. The human process is in `docs/dependency-update-process.md`.
- Installed outputs (`third_party/cef/`, `assets/fonts/`) stay gitignored. Receipts such as
  `assets/fonts/.island-dependency-receipt.json` are what `deps.py verify` checks offline.
- Stdlib only — `urllib`, `hashlib`, `tarfile`.

### Testing Requirements

```bash
python3 -m pytest tests/deps
python3 scripts/deps.py verify
```

### Common Patterns

- Frozen dataclasses with `slots=True`; `Final` constants; every failure is a `DependencyError`
  subclass.

## Dependencies

### Internal

- `scripts/deps.py` (CLI), `scripts/setup_deps.{sh,ps1}` (entrypoints),
  `docs/dependency-update-process.md` (process), `.github/workflows/dependency-check.yml` (weekly
  scheduled check)

<!-- MANUAL: -->
