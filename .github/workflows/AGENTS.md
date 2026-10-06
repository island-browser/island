<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-10-06 -->

# workflows

## Purpose

The CI, packaging, and release automation. Together these produce the cross-platform evidence that
the root `AGENTS.md` requires before any non-macOS-arm64 support claim can be made.

## Which workflow proves what

**Most automatic triggers are paused** to save GitHub Actions minutes. Two workflows run on
pushes to `main`: `build-release.yml` (when anything that ships changes — it builds all six
targets and publishes unsigned prereleases) and `version-tag.yml` (when `VERSION` changes). The
product site and its Pages deploy live in their own repository, `island-browser/site`, which reads
`VERSION` and `CHANGELOG.md` from here. Everything else runs only on a manual
`workflow_dispatch` (plus `workflow_call` for `ci.yml` and `package.yml`). Build, test, and
package locally with the root `AGENTS.md` Verification commands before committing. To re-enable a
paused workflow, restore its original `on:` block from git history.

| File | Trigger | Evidence it produces |
|------|---------|----------------------|
| `ci.yml` | dispatch, `workflow_call` (automatic triggers paused) | The authoritative gate. `portable` (ubuntu-24.04) runs shell syntax + dry run, dependency tests, package tests, the design-token drift guard, and `icon_pipeline verify`. `native-macos`, `native-linux`, `native-windows` each configure, build Release, and run native tests per target, uploading `ci-diagnostics-<target>` on failure. |
| `build-release.yml` | Push to `main` touching `src/**`, `cmake/**`, `CMakeLists.txt`, `deps/**`, `resources/**`, `tests/**`, `VERSION`, the packaging scripts, or itself/`package.yml`; dispatch (optional `commit`: a SHA or tag on `main`) | Calls `package.yml` for all six targets, then publishes **prereleases only**, and only when all six succeed: the rolling `nightly` prerelease (deleted and recreated so its tag moves to the commit; never moved backwards), and — the first time a build of the `v<VERSION>` commit succeeds — the `v<VERSION>` prerelease titled `Island <VERSION> (unsigned)` with that version's `CHANGELOG.md` section. Package version: `<VERSION>` for a run that creates `v<VERSION>`, else `<VERSION>-nightly.<run number>`. Assets: six `island_browser-<version>-<target>.<zip or tar.gz>` archives, one installer per target (`scripts/installers.py`: `.dmg` with an ad-hoc signed `Island.app` for macOS, `.deb` for Linux, Inno Setup `-setup.exe` for Windows), plus one combined `SHA256SUMS.txt` (`scripts/release_assets.py`). Only the two publish jobs get `contents: write` (plus `actions: read` for `gh run download`). |
| `package.yml` | `workflow_call` from `build-release.yml`; dispatch (builds only, publishes nothing) | Unsigned candidates for all six targets (`macosx64`, `macosarm64`, `linux64`, `linuxarm64`, `windows64`, `windowsarm64`): deps install, Release build, ctest, `scripts/package.py`, metadata check, artifact `unsigned-candidate-<version>-<target>`. Version is the `version` input, else `<VERSION>-ci.<run number>` |
| `release.yml` | Dispatch only (the `v*` tag trigger is paused) | The stable-release **gate**. It requires the tag to match `VERSION`, asserts `scripts/package.py` still emits `signed: False` / `publicReleaseEligible: False`, rejects unprotected tags, and then **fails on purpose** — stable publishing stays blocked until signing and notarization are implemented. |
| `search.yml` | Dispatch only (automatic triggers paused) | The only automated coverage of the search kernel. Configures `src/search` as its own source root with `-DISLAND_ENABLE_SEARCH=ON` on all six targets, builds `island_search`/`island_search_tests`/`island_search_posting_codec_harness`, and runs `ctest --no-tests=error` |
| `dependency-check.yml` | Dispatch only (the weekly cron is paused) | Read-only upstream check for newer pinned dependencies |
| `version-tag.yml` | Push to `main` touching `VERSION`; dispatch | Tags the commit `v<VERSION>` (annotated, by `github-actions[bot]`) unless the tag exists; needs `contents: write` on that job only |
| `claude.yml` | Dispatch only (comment triggers paused) | Claude Code agent invoked from a comment |
| `claude-code-review.yml` | Dispatch only (PR triggers paused) | Automated Claude review pass on pull requests |

## For AI Agents

### Working In This Directory

- **`release.yml` failing is the correct outcome.** Do not "fix" it. Its final step exits 1 by
  design, and the assertions it makes against `scripts/package.py` mean a change to that script's
  candidate metadata will break the gate in a meaningful way.
- `search.yml` is the reason the search kernel has any CI coverage at all: `ci.yml` configures the
  browser root with default options, where `ISLAND_ENABLE_SEARCH` is `OFF`, so none of the search
  targets are built there. If you change `src/search/CMakeLists.txt`, `search.yml` is the workflow
  that will catch it.
- Third-party actions are pinned by full commit SHA. Match that when adding steps.
- `permissions:` is `contents: read` at workflow level, with write escalated only per job
  (`build-release.yml`'s publish jobs, `version-tag.yml`). Preserve that shape.
- Everything `build-release.yml` publishes is a GitHub **prerelease** marked unsigned; it never
  creates a stable release. Because every release is a prerelease, the REST `/releases/latest`
  endpoint returns 404 — consumers such as the in-browser updater list `/releases` instead.
- `nightly.yml` was removed: `build-release.yml` owns the `nightly` prerelease now.
- A `v<VERSION>` tag whose commit never had a successful build (for example `v0.4.0`, tagged
  before this pipeline existed) has no release; dispatch `build-release.yml` with
  `commit: v<VERSION>` to build that exact commit and publish it. A run on a later commit only
  publishes `nightly` and warns, because its archives would not match the tag.
- `ci.yml` has five jobs: `portable`, `native-macos`, `native-linux`, `native-windows`, and
  `ci-success` as the aggregate gate.

### Testing Requirements

Workflow changes cannot be verified locally; a PR run is the only real check. A YAML parse is a
cheap pre-flight, but note it needs PyYAML — the repository's own Python is stdlib-only, so do not
add this as a dependency of anything under `scripts/` or `deps/`:

```bash
python3 -c "import yaml,glob;[yaml.safe_load(open(f)) for f in glob.glob('.github/workflows/*.yml')]"
```

## Dependencies

### Internal

- `scripts/` (deps, package, icon verify), `deps/dependencies.lock.json`, `tests/` (all suites),
  `src/search/` (via `search.yml`)

<!-- MANUAL: -->
