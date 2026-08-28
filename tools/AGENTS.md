<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# tools

## Purpose

Container for offline developer tooling — code that generates committed artifacts but is never
built into or shipped with the browser.

## Subdirectories

| Directory | Purpose |
|-----------|---------|
| `icon_pipeline/` | Renders the committed runtime icon PNGs from vendored SVG sources (see `icon_pipeline/AGENTS.md`) |

## For AI Agents

### Working In This Directory

- Tools here are run by hand and their **outputs** are committed; the tools themselves are not part
  of the CMake build.
- Vendored third-party sources carry their own licenses and notices. Keep them alongside the
  vendored files.

## Dependencies

### Internal

- `resources/island/icons/` — the generated, committed output

<!-- MANUAL: -->
