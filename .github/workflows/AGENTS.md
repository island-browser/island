<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# workflows

## Purpose

The CI, packaging, and release automation. Together these produce the cross-platform evidence that
the root `AGENTS.md` requires before any non-macOS-arm64 support claim can be made.

## Which workflow proves what

| File | Trigger | Evidence it produces |
|------|---------|----------------------|
| `ci.yml` | PR, push to `main`, dispatch, `workflow_call` | The authoritative gate. `portable` (ubuntu-24.04) runs shell syntax + dry run, dependency tests, package tests, the design-token drift guard, and `icon_pipeline verify`. `native-macos`, `native-linux`, `native-windows` each configure, build Release, and run native tests per target, uploading `ci-diagnostics-<target>` on failure. |
| `package.yml` | After a successful `Island CI` on `main`, or dispatch | Unsigned candidates for all six targets: `macosx64`, `macosarm64`, `linux64`, `linuxarm64`, `windows64`, `windowsarm64` |
| `nightly.yml` | After a successful `Package unsigned candidates` on `main`, or dispatch with a run id | Republishes a completed package run as a nightly release |
| `release.yml` | Push of a `v*` tag | The stable-release **gate**. It asserts `scripts/package.py` still emits `signed: False` / `publicReleaseEligible: False`, rejects unprotected tags, and then **fails on purpose** — stable publishing stays blocked until signing and notarization are implemented. |
| `search.yml` | PR, push to `main`, dispatch | The only automated coverage of the search kernel. Configures `src/search` as its own source root with `-DISLAND_ENABLE_SEARCH=ON` on all six targets, builds `island_search`/`island_search_tests`/`island_search_posting_codec_harness`, and runs `ctest --no-tests=error` |
| `dependency-check.yml` | Weekly cron (`17 4 * * 1`), dispatch | Read-only upstream check for newer pinned dependencies |
| `pages.yml` | Push to `main` touching `site/**`, dispatch | Deploys the product site to GitHub Pages |
| `claude.yml` | `issue_comment`, `pull_request_review_comment` | Claude Code agent invoked from a comment |
| `claude-code-review.yml` | PR opened / synchronize / ready_for_review / reopened | Automated Claude review pass on pull requests |

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
  (`nightly.yml`). Preserve that shape.
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
  `src/search/` (via `search.yml`), `site/` (pages)

<!-- MANUAL: -->
