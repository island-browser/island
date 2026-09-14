<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# docker

## Purpose

The container half of the hybrid build. A multi-stage Dockerfile anchored to an immutable base
digest, plus the lock that pins each stage per architecture.

## Status

Untracked, together with `scripts/container_runner.py` and `tests/container/`. The governing
document is `docs/build-execution-policy.md`.

## Key Files

| File | Description |
|------|-------------|
| `Dockerfile` | Multi-stage build; base is `ubuntu@${UBUNTU_BASE_DIGEST}` (multi-arch index for `ubuntu:24.04`) |
| `images.lock.json` | Per-stage, per-architecture digests validated by `scripts/image_lock.py` |

## For AI Agents

### Working In This Directory

- **The base image digest is the reproducibility anchor, not per-package apt versions.** Pinning
  apt versions is fragile across image rebuilds and is deliberately not done; the `hadolint
  ignore=DL3008` comments encode that decision.
- Required stages are `ubuntu-base`, `portable-tests`, `linux-build`, `dev`, and required
  architectures are `linux/amd64` and `linux/arm64`. `scripts/image_lock.py` fails if any is
  missing, so adding or renaming a stage means updating both files and `tests/container/`.
- Docker resolves the architecture-specific image from the multi-arch index at pull time; do not
  replace the index digest with a single-arch one.

### Testing Requirements

```bash
python3 -m pytest tests/container
```

Image builds themselves run in `.github/workflows/linux-container.yml` and
`.github/workflows/portable-container.yml`.

## Dependencies

### Internal

- `scripts/image_lock.py`, `scripts/container_runner.py`, `scripts/island.py`,
  `docs/build-execution-policy.md`, `.dockerignore`

<!-- MANUAL: -->
