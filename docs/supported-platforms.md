# Supported platforms

Island targets six desktop dependency/build targets:

| Target | Platform | Architecture | Evidence status |
| --- | --- | --- | --- |
| `macosarm64` | macOS | Apple Silicon arm64 | Locally built and tested at the current head (commit `4805c71`): 242/242 browser ctest (`ISLAND_ENABLE_SEARCH` off), 148/148 search-kernel ctest (`ISLAND_ENABLE_SEARCH` standalone build), pytest 70 passed plus 89 subtests (`tests/deps tests/package tests/design`). App-run, smoke, and packaging evidence on record predates Phase 3 and has not been re-verified against the current head; the Phase 3 manual acceptance pass (U10) is outstanding. Green on native CI, but the newest green full-matrix run (33511735871, commit `b939cd0`) predates Phase 3. |
| `macosx64` | macOS | Intel x64 | Native CI: configure/build/CTest green at commit `b939cd0` (`Island CI` run 33511735871), which predates Phase 3. No Phase 3-era CI evidence and no local run/smoke/packaging evidence. |
| `windows64` | Windows | x64 | Native CI: configure/build/CTest green at commit `b939cd0` (`Island CI` run 33511735871), which predates Phase 3. No Phase 3-era CI evidence and no local run/smoke/packaging evidence. |
| `windowsarm64` | Windows | ARM64 | Native CI: configure/build/CTest green at commit `b939cd0` (`Island CI` run 33511735871), which predates Phase 3. No Phase 3-era CI evidence and no local run/smoke/packaging evidence. |
| `linux64` | Linux | x64 | Native CI: configure/build/CTest green under Xvfb at commit `b939cd0` (`Island CI` run 33511735871), which predates Phase 3. No Phase 3-era CI evidence and no local run/smoke/packaging evidence. |
| `linuxarm64` | Linux | ARM64 | Native CI: configure/build/CTest green under Xvfb at commit `b939cd0` (`Island CI` run 33511735871), which predates Phase 3. No Phase 3-era CI evidence and no local run/smoke/packaging evidence. |

Evidence-over-claims posture: a green CI run is build/test evidence only. Do not infer app-run,
smoke-test, or packaging success from it, and do not carry Phase 2-era claims forward to Phase 3
code without re-verifying them (Phase 3 U9 owns that refresh; its locally completable part — the
workflow audit and the local macOS arm64 facts below — is done as of 2026-09-19; the refresh push
happened the same day (`e1ba0ed` on `main`), but every workflow job was **not started** — GitHub
reported "recent account payments have failed or your spending limit needs to be increased", so no
run evidence exists yet and the next successful run must follow a billing fix).

## Implemented features vs. evidence

The following browser features are implemented on `main` at commit `4805c71` (Phase 3 units U4–U8,
plus the earlier sidebar/palette units and the welcome/bookmark-import/appearance unit): a tab strip
in the rail with New/Close Tab (`Cmd/Ctrl+T`, `Cmd/Ctrl+W`), previous/next (`Cmd/Ctrl+Shift+[`/`]`)
and direct-index switching (`Cmd/Ctrl+1..9`); a space switcher with New/Close Space, rename (F2
accelerator and the macOS Browser menu "Rename Space…" through an overlay textfield) and reorder
(menu "Move Space Left/Right" — there is no drag or hover affordance; that is deferred); split view
(`Cmd/Ctrl+Shift+S` pairs the active tab with its adjacent tab; the divider adjusts through the
Move Split Divider menu items and the `kMoveDividerLeft/Right` commands, not by dragging — the
pinned CEF distribution exposes no mouse events on custom views); the command palette on
`Cmd/Ctrl+K` (open tabs of the active space, all spaces, and a go-to-URL row that submits through
the same `AddressBarModel`/`ParseAndValidate` path as the rail's address control); the search
palette on `Cmd/Ctrl+Shift+K` (five fixed providers); the hideable sidebar (`Cmd/Ctrl+B` toggle on
every platform, macOS hover-reveal via the 12/16-DIP edge bands); clean-quit session restore
(`session.json` under the platform app-data directory, restored URLs re-validated, invalid ones
falling back to the fixed startup page); and a first-run welcome overlay (appearance choice plus
detected-browser bookmark import into a validated local store, surfaced as a command-palette group;
"Show Welcome…" and the Appearance menu reopen or change either afterwards).

All of the above is backed by macOS arm64 local build/test evidence only. Windows/Linux chrome
input paths go through the same accelerator registrations, but no native CI evidence covers
Phase 3-era code on those targets yet and no local run evidence exists; the manual acceptance
checklists (`tests/manual/`) have not been executed on any target.

## Packaging evidence

Packaging evidence is tracked separately and is currently *worse* than the CI rows above: the most
recent `package.yml` run that actually executed (33513717574, on `main` after `b939cd0`) succeeded
for `macosarm64`, `macosx64`, `linux64`, and `linuxarm64` but **failed** for `windows64` and
`windowsarm64`, and the run after it (33635598552) was skipped. Treat packaging as unverified for
the Windows targets and unverified for Phase 3-era code on every target, and re-check
`package.yml`'s latest run before citing packaging status at all. Unsigned candidates only; stable
public release remains blocked until signing and notarization verification are implemented (see
`docs/release-process.md`).

## macOS local verification

```bash
./scripts/setup_deps.sh
cmake -B build -S .
cmake --build build
ctest --test-dir build --output-on-failure
open build/src/main/island_browser.app
```

Smoke mode:

```bash
open build/src/main/island_browser.app --args --island-smoke-test
```

The smoke page should show the `ISLAND_PHASE1_SMOKE_OK` marker, make no external startup request,
block popups, and allow history back/forward on its local history link. After quitting the app,
verify there are no remaining `island_browser` helper processes. The macOS app bundle is
`build/src/main/island_browser.app`.

```bash
pgrep -fl island_browser || true
```

The default suite reports **242 tests** (browser ctest with `ISLAND_ENABLE_SEARCH` off). The Phase
S0 search kernel builds and tests separately (148 tests, still gated behind `ISLAND_ENABLE_SEARCH`
— see the root `AGENTS.md`), and the Python suites report 70 passed plus 89 subtests:

```bash
python3 -m pytest tests/deps tests/package tests/design
```

## Cross-platform verification

`.github/workflows/ci.yml` defines native jobs for all six targets and installs and verifies the
pinned CEF and Geist dependencies before configure, build, and CTest. Linux jobs run tests under
`xvfb-run` (`CefInitialize` needs an X11 display even for tests that never show a window). The
newest green full-matrix run is `Island CI` run 33511735871 on `main` at commit `b939cd0` — that
commit predates the Phase 3 tab/space/palette/session-restore units and the search/container work,
so it is build/test evidence for the Phase 2-era tree only. Phase 3 U9's locally completable work
is done as of 2026-09-19: the `Island CI` and `Package unsigned candidates` definitions were
re-audited against the current tree and need no change (they trigger on every push to `main` with
no path filters or caches, and the default suite builds every Phase 3 test target), so the next
push will produce the refreshed evidence without configuration work. The refresh push landed as
`e1ba0ed` on 2026-09-19; all `Island CI`, `Package unsigned candidates`, and Pages jobs on it were
**not started** because of the account's GitHub Actions billing block, so no green run exists yet.
Until a run started after the billing fix goes green, cite no Phase 3-era native CI evidence for
any target — the macOS arm64 row above is local evidence only.

Local macOS arm64 packaging evidence (2026-09-22, this machine): the full `Package unsigned
candidates` recipe for `macosarm64` was reproduced locally on the current tree — Release
configure/build (242/242 ctest with `-C Release`), then `scripts/package.py` produced
`island_browser-0.3.0-local.1-macosarm64.zip`, verified by the workflow's own recipe (SHA256SUMS
digest match, `build-metadata.json` exactly `{format: zip, notarized: false,
publicReleaseEligible: false, signed: false, target: macosarm64}`), and the extracted app launched
with `--island-smoke-test` (smoke page rendered, clean quit, no leftover processes). Zip entries
carry 0755 modes for the main and helper binaries; extraction must preserve them (Info-ZIP
`unzip` and macOS Archive Utility do; Python `zipfile.extractall` does not).

Clean-checkout contract re-verified 2026-09-22 from a true fresh clone (no vendored
`third_party/` or `assets/`): `scripts/setup_deps.sh` performed the real CEF and Geist downloads
("Dependencies ready"), `deps.py verify` passed, a fresh configure/build produced 0 errors, and
the full suite ran green there — 242/242 ctest, pytest 70 passed + 89 subtests, plus a
`--island-smoke-test` launch of that clone's app with a clean quit.
