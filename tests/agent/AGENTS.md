<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-10-06 | Updated: 2026-10-06 -->

# agent

## Purpose

GoogleTest suites for the CEF-free agent kernel in `src/agent/`. The `island_agent_tests` target is
defined in `src/agent/CMakeLists.txt` (not `tests/CMakeLists.txt`) so the standalone
`cmake -S src/agent` configuration can build it without CEF.

## Key Files

| File | Description |
|------|-------------|
| `fake_browser_host.h` | Scriptable `AgentBrowserHost` that records calls and replays queued DevTools replies |
| `json_util_test.cpp` | Unicode escapes, surrogates, doubles, control-character escaping, builders |
| `browser_tools_test.cpp` | Tool definitions, argument validation, page-tool DevTools sequences (trusted mouse/keyboard events) |
| `mcp_server_test.cpp` | MCP initialize/version negotiation, tools/list, tools/call, error mapping |
| `agent_endpoint_test.cpp` | Request policy, live HTTP round trips, concurrency, discovery file, stdio bridge end to end |
| `acp_client_test.cpp` | ACP handshake, HTTP vs stdio MCP hand-off, streaming updates, permissions, cancel, failures |
| `agent_process_test.cpp` | Command-line splitting, child stdio, exit codes, stubborn-child termination |
| `agent_session_test.cpp` | Full conversation against a real fake agent subprocess (`fixtures/fake_acp_agent.py`): queued first prompt, streaming, permissions, cancel, crash + relaunch |
| `fixtures/fake_acp_agent.py` | Minimal ACP agent over stdio used by the session tests (needs `python3`) |
| `panel/agent_panel_behavior.js` | Optional Playwright check of `src/main/pages/agent.html` (messages, keys, no HTML injection) |
| `agent_transcript_test.cpp` | Event folding, in-place tool/plan updates, permission resolution, bounds |

## For AI Agents

- The process and endpoint tests spawn real children and sockets; keep them POSIX-guarded.
- Write `\uXXXX` escapes in test sources through a tool that does not decode them, and check with
  `grep -n '\\u'` afterwards: a decoded escape silently turns an escape test into a UTF-8 test.

<!-- MANUAL: -->
