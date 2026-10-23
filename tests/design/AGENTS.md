<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-08-28 | Updated: 2026-08-28 -->

# design

## Purpose

Drift guard for the design-token contract. `DESIGN.md` holds the single machine-readable token
source — a fenced ```design-tokens``` JSON block. This suite proves that block is complete and
well formed and that the pinned assertions in `tests/design_tokens_test.cpp` still agree with it.
The product site's CSS parity check lives in the site repository
(`island-browser/site`, `tests/test_token_parity.py`) and reads this contract.

## Key Files

| File | Description |
|------|-------------|
| `test_token_contract.py` | Contract completeness plus native parity checks |
| `test_local_pages.py` | Built-in pages (`src/main/pages/`): no external resources, shared bridge, start page and Settings contract, the agent panel's look contract (`AgentPanelLook`: status pill tones, round send pinned to the right, neutral focus rings, no hex colours but `#FFF`, one-line tool rows), and the agent panel Markdown renderer's escaping (run through Node when installed) |

## For AI Agents

### Working In This Directory

- `DESIGN.md` is the only place token values are authored. Every check here is an equality
  assertion against it — never introduce a second source of truth, and never "fix" a failure by
  editing the expected value in one consumer.
- Native parity is deliberately covered in two weaker layers rather than by parsing Markdown from
  C++; that trade-off is documented in the module docstring. Preserve it.
- Changing a token means updating `DESIGN.md`, `src/main/design_tokens.cc`,
  `tests/design_tokens_test.cpp`, and the site repository's `assets/css/site.css` together.

### Testing Requirements

```bash
python3 -m pytest tests/design
```

This is the "Run design token drift guard" step of the `portable` job in `.github/workflows/ci.yml`.

## Dependencies

### Internal

- `DESIGN.md`, `src/main/design_tokens.{h,cc}`, `tests/design_tokens_test.cpp`

<!-- MANUAL: -->
