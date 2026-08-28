<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# resources

## Purpose

Committed runtime resources. Unlike `assets/fonts/` and `third_party/cef/` — which are vendored by
`scripts/setup_deps.sh` and gitignored — everything under here is checked in and gated at configure
time.

## Subdirectories

| Directory | Purpose |
|-----------|---------|
| `island/` | The Island resource root staged into the app bundle (see `island/AGENTS.md`) |

## For AI Agents

### Working In This Directory

- The directory layout here mirrors what ships: the build copies it into
  `<bundle>/Contents/Resources/island/` via `island_stage_chrome_resources()`. Renaming a directory
  breaks runtime lookup in `src/main/app_resources.cc`.
- These files are committed on purpose. Do not add them to `.gitignore`.

### Testing Requirements

```bash
tools/icon_pipeline/run.sh verify
ctest --test-dir build -R ResourcePaths --output-on-failure
```

## Dependencies

### Internal

- `tools/icon_pipeline/` (generator), `src/main/{app_resources,icon_catalog}.cc` (consumers), root
  `CMakeLists.txt` (configure-time gate)

<!-- MANUAL: -->
