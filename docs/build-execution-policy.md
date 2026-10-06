# Build execution policy

Island uses a hybrid build architecture: one predictable Python router routes work either to a
locked, digest-pinned Linux container or to the authoritative native host. Native macOS and Windows
builds, packages, GUI runs, and smoke tests are never containerized. Docker Compose is not used.

## Router

`python3 scripts/island.py` is the single entry point. Commands:

| Command | Mode | Description |
| --- | --- | --- |
| `doctor` | native | Print host capability, Docker availability, lock/image digests, tool versions. |
| `test portable` | docker | Run portable validation (deps Python tests, design drift, icon pipeline, package fixtures) in the locked `portable-tests` image. |
| `build linux --arch x64|arm64` | docker | Build the Linux product in the locked `linux-build` image on matching native architecture. |
| `build native` | native | Configure and build the product with the host toolchain. Never starts Docker. |
| `package native` | native | Package the already-built native product. Never starts Docker. |
| `smoke native` | native | Run the native smoke test. Never starts Docker. |
| `deps fetch --target <target>` | native | Fetch and verify CEF/Geist for a target with `scripts/deps.py`. Runs natively; when the requested target does not match the host's native target it prints a non-blocking dry-run plan with an explicit notice instead of downloading. |

Unsupported combinations are refused with a stable exit code. Native commands must never silently
start Docker.

## Exit categories

| Code | Meaning |
| --- | --- |
| 0 | Success. |
| 2 | Unsupported command or argument combination. |
| 3 | Required host tool missing (Docker required but unavailable, or native toolchain missing). |
| 4 | Task declaration invalid (unknown task, path traversal, undeclared command). |
| 5 | Image lock validation failed (missing digest, floating tag, digest mismatch). |
| 6 | Container run failed (build/test/package failure inside the container). |
| 7 | Report writing or redaction failure. |

## Reports

Every router invocation that performs a task writes a JSON report under `build/reports/` with:

- `task`: the task name from `build/tasks.json`.
- `target`: the resolved target (for example `linux64`).
- `host`: the host platform and architecture.
- `mode`: `native` or `docker`.
- `emulated`: `true` only when the container architecture does not match the host.
- `imageDigest`: the immutable digest of the image used, or `null` for native.
- `depsLockHash`: the SHA-256 of `deps/dependencies.lock.json`.
- `sourceRevision`: the `git` revision at task start, or `unknown`.
- `toolVersions`: selected tool versions recorded by the task.
- `timestamp`: ISO-8601 UTC.
- `exit`: the exit category.
- `artifactHashes`: a map of artifact name to SHA-256, empty for tasks that produce none.

Reports never include secrets, environment variable values, home directory paths, or keychain paths.

## Container constraints

- Source is mounted read-only at `/src`; work happens under a writable `/work`.
- Never mount: `$HOME`, `~/Desktop`, `~/Downloads`, keychain directories, the Docker socket, SSH
  agent or keys, or cloud credential directories.
- Named Docker volumes back dependency and compiler caches; no anonymous bind mounts of host caches.
- Default network is `--network=none`. Network is enabled only for explicit dependency-fetch tasks
  and only to the allowlisted dependency hosts.
- The container runs as a non-root user. When the host is Linux, the caller's UID/GID is mapped so
  that exported artifacts are not root-owned.
- Artifacts are exported by the host wrapper from the container's `/work/out` into
  `build/container-artifacts/`; no root-owned checkout files are left behind.

## Image policy

`docker/images.lock.json` pins every base image by immutable digest. No floating tag is allowed.
The Dockerfile declares three stages:

| Stage | Purpose |
| --- | --- |
| `portable-tests` | Run Python dependency tests, design drift, icon pipeline, package fixtures. |
| `linux-build` | Build and test the Linux product on a matching native architecture. |
| `dev` (optional) | Interactive development shell. Not used by CI. |

The Ubuntu baseline matches the existing native Linux CI runner (`ubuntu-24.04`) for CEF
compatibility. A build or test must fail if the resolved image digest is not recorded in
`docker/images.lock.json`.

## Task allowlist

`docker/tasks.json` is the declarative allowlist (tracked in the repository; the writable
`build/` tree stays generated). Each task declares:

- `networkPolicy`: `none` or `dependency-fetch`.
- `sourceAccess`: `read-only` (always).
- `outputs`: the artifact directories the host wrapper exports.
- `architectures`: the architectures the task supports.
- `entryPoint`: the command the container runs.

The router rejects path traversal in outputs, undeclared commands, and tasks whose declared
architecture does not match the host when a native match is required.

## CI

| Workflow | Purpose |
| --- | --- |
| `portable-container.yml` | Runs portable validation in the locked `portable-tests` image on PRs. |
| `linux-container.yml` | Runs the x64 native container build lane; arm64 only on an actual native arm64 runner. QEMU preflight may be non-blocking and is explicitly non-authoritative. |

Existing native workflows (`ci.yml`, `package.yml`, `build-release.yml`, `release.yml`,
`dependency-check.yml`, `pages.yml`) are untouched and remain the source of truth for native
builds, packages, releases, dependency reports, and site deployment. Container lanes are additive
and never replace native evidence.

## Security

- No secrets are written into images or reports. CI uses no repository secrets for the container
  lanes; images are built from public, digest-pinned bases.
- The Docker socket is never mounted. The host wrapper invokes the Docker CLI as a subprocess; the
  container has no Docker access.
- `.dockerignore` excludes `.git`, `build/`, `third_party/`, `assets/fonts/`, `.omc/`, `.omo/`,
  `.playwright-mcp/`, `*.pen`, `.claude/`, `.codegraph/`, `dist-*`, `site/.omc/`, and any path that
  could leak local state or credentials into the build context.
