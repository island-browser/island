<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# container

## Purpose

Python `unittest` suite for the hybrid build router — how `scripts/island.py` decides between the
authoritative native host and the digest-pinned Linux container, and for the typed models that
enforce the container's security constraints.

## Status

Untracked, alongside the `scripts/` container modules and `docker/` it covers. The behaviour it
pins is described in `docs/build-execution-policy.md`.

## Key Files

| File | Description |
|------|-------------|
| `__init__.py` | Package marker |
| `test_routing.py` | `scripts/island.py` command routing and exit codes |
| `test_container_runner.py` | `DockerInvocation` construction and `ContainerRunnerError` cases |
| `test_image_lock.py` | `docker/images.lock.json` parsing, required stages and architectures |
| `test_task_model.py` | `build/tasks.json` parsing, path-traversal rejection, policy validation |

## For AI Agents

### Working In This Directory

- These modules are imported flat (`import island`, `from container_runner import …`) — that works
  only because the root `conftest.py` puts `scripts/` on `sys.path`. Run pytest from the repository
  root.
- Tests must never actually start Docker. `test_container_runner.py` patches `subprocess`; keep it
  that way.
- The security constraints under test are load-bearing: network policy limited to
  `none`/`dependency-fetch`, read-only source access, required stages
  (`ubuntu-base`, `portable-tests`, `linux-build`, `dev`) and architectures
  (`linux/amd64`, `linux/arm64`). Loosening an assertion here loosens the build sandbox.

### Testing Requirements

```bash
python3 -m pytest tests/container
```

Container lanes themselves run in `.github/workflows/linux-container.yml` and
`.github/workflows/portable-container.yml`.

## Dependencies

### Internal

- `scripts/island.py`, `scripts/container_runner.py`, `scripts/image_lock.py`,
  `scripts/task_model.py`, `scripts/report_writer.py`, `docker/images.lock.json`

<!-- MANUAL: -->
