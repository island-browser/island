<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# icon_pipeline

## Purpose

Offline, reproducible generation of Island's runtime icon PNGs. It takes the vendored SVG sources,
renders every icon × color-role × size × scale combination, and writes both the PNGs and the
manifest that the build and `src/main/icon_catalog.cc` read.

## Key Files

| File | Description |
|------|-------------|
| `icon_pipeline.cc` | The renderer/manifest generator |
| `sha256.{h,cc}` | Digest implementation used to make the output verifiable |
| `icon-sources.lock.json` | Pinned upstream icon sources and their digests |
| `run.sh` | Entrypoint; `run.sh verify` re-checks the committed resources |
| `test.sh` | Pipeline self-test |
| `licenses/` | Upstream license texts for the vendored icon sets (no nested `AGENTS.md`: license text only) |
| `vendor/` | Vendored SVG sources (no nested `AGENTS.md`: vendored inputs only) |
| `THIRD_PARTY_NOTICES.txt` | Attribution for Lucide (ISC) and Feather (MIT) |

## For AI Agents

### Working In This Directory

- The output count is a build-enforced invariant: the root `CMakeLists.txt` reads
  `resources/island/icons/manifest.json` and **fails configuration unless it declares exactly 192
  PNG resources**, each matching `^resources/island/icons/png/[^/]+\.png$` and present on disk.
  Adding or removing an icon means updating that expected count in `CMakeLists.txt` too.
- This tool is not part of the CMake build. Run it by hand; commit its outputs under
  `resources/island/icons/`.
- License obligations are real. New sources go in `icon-sources.lock.json` with their digest, and
  their license text goes in `licenses/` plus `THIRD_PARTY_NOTICES.txt`.

### Testing Requirements

```bash
tools/icon_pipeline/run.sh verify
tools/icon_pipeline/test.sh
```

`run.sh verify` is the "Verify committed icons" step of the `portable` job in
`.github/workflows/ci.yml`.

## Dependencies

### Internal

- `resources/island/icons/` (output), `src/main/icon_catalog.cc` (consumer), root `CMakeLists.txt`
  (manifest gate)

### External

- Lucide (ISC) and Feather (MIT) icon sets, vendored under `vendor/`

<!-- MANUAL: -->
