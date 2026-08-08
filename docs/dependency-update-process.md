# Dependency update process

Island's binary dependency source of truth is `deps/dependencies.lock.json`. It pins CEF 150 for
all six intended desktop targets and pins Geist to an immutable GitHub commit archive.

## Daily commands

```bash
bash -n scripts/setup_deps.sh
./scripts/setup_deps.sh --dry-run
./scripts/setup_deps.sh
./scripts/setup_deps.sh --force
python3 scripts/deps.py verify
python3 scripts/deps.py check-updates
```

Setup syntax is `./scripts/setup_deps.sh [--dry-run] [--force] [--target <target>]`.

- `--dry-run` prints the resolved CEF URL and Geist source without writing dependencies.
- A normal setup downloads only when dependencies are absent; valid existing installs are verified
  and reused.
- `--force` replaces the vendored dependency directories.
- `verify` checks installed receipts and dependency tree integrity.
- `check-updates` is report-only; it does not edit the lock file.
- `--target <target>` selects one of the six locked desktop targets instead of the host target.

## Pin policy

- CEF and Geist updates must be reviewed in a pull request that changes the lock file intentionally.
- CEF targets must remain exactly: `macosx64`, `macosarm64`, `windows64`, `windowsarm64`,
  `linux64`, `linuxarm64`.
- CEF archives come from `cef-builds.spotifycdn.com`; Geist archives come from the pinned GitHub
  commit source.
- The PR must record the new version/commit, expected SHA-256 values, and verification evidence.
- Do not auto-merge dependency update PRs and do not auto-release from dependency checks.

## Verification behavior

The installer validates the lock file, download host, archive size limits, SHA-256 digest, archive
paths, CEF sentinel `cmake/cef_macros.cmake`, approved Geist font files, license files, and installed
receipts. Re-running setup without `--force` verifies the existing tree and skips network downloads
only when the tree is valid.

## CI cache behavior

GitHub Actions derives a dependency cache key from the lock-file digest plus target, then caches
`third_party/cef` and `assets/fonts`. Any lock-file change produces a new cache key. Cache restores
are still followed by install/verify, so a stale or tampered cache should fail instead of becoming a
trusted input.

The scheduled dependency-check workflow uploads per-target reports and writes a summary only; it
never modifies `deps/dependencies.lock.json`, opens a PR, merges a PR, or publishes a release.
