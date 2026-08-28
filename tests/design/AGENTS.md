<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# design

## Purpose

Drift guard for the design-token contract. `DESIGN.md` holds the single machine-readable token
source — a fenced ```design-tokens``` JSON block. This suite proves that block is complete and
well formed, that `site/assets/css/site.css` resolves to exactly those values, and that the pinned
assertions in `tests/design_tokens_test.cpp` still agree with it.

## Key Files

| File | Description |
|------|-------------|
| `test_token_contract.py` | Contract completeness plus CSS and native parity checks |

## For AI Agents

### Working In This Directory

- `DESIGN.md` is the only place token values are authored. Every check here is an equality
  assertion against it — never introduce a second source of truth, and never "fix" a failure by
  editing the expected value in one consumer.
- Native parity is deliberately covered in two weaker layers rather than by parsing Markdown from
  C++; that trade-off is documented in the module docstring. Preserve it.
- Changing a token means updating `DESIGN.md`, `src/main/design_tokens.cc`,
  `tests/design_tokens_test.cpp`, and `site/assets/css/site.css` together.

### Testing Requirements

```bash
python3 -m pytest tests/design
```

This is the "Run design token drift guard" step of the `portable` job in `.github/workflows/ci.yml`.

## Dependencies

### Internal

- `DESIGN.md`, `src/main/design_tokens.{h,cc}`, `tests/design_tokens_test.cpp`,
  `site/assets/css/site.css`

<!-- MANUAL: -->
