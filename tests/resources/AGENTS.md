<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# resources

## Purpose

Tests for runtime resource-directory resolution — where the running app looks for staged fonts and
icons inside the bundle or beside the executable.

## Key Files

| File | Description |
|------|-------------|
| `app_resources_test.cc` | Path resolution behaviour of `src/main/app_resources.cc` |

## For AI Agents

### Working In This Directory

- Compiled into its own executable, `island_resource_tests`, which links **no** CEF and **no**
  GoogleTest — it is registered with a bare `add_test(NAME ResourcePaths ...)`.
- Keep `app_resources.cc` free of CEF and of GoogleTest assumptions so this target stays cheap.

### Testing Requirements

```bash
ctest --test-dir build -R ResourcePaths --output-on-failure
```

## Dependencies

### Internal

- `src/main/app_resources.{h,cc}`

<!-- MANUAL: -->
