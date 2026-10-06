<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-10-06 | Updated: 2026-10-06 -->

# agent

## Purpose

The AI-agent integration kernel. It makes Island drivable by AI agents in two directions:

- **Agents drive the browser (MCP).** `BrowserToolbox` defines the browser tools, `McpServer` speaks
  the Model Context Protocol, and `AgentEndpoint` serves it at `http://127.0.0.1:<port>/mcp`
  (Streamable HTTP, JSON responses). Any MCP client can connect, and `island_mcp_bridge` relays
  stdio for clients that only launch stdio servers.
- **The browser hosts an agent (ACP).** `AcpClient` makes Island an Agent Client Protocol client:
  it runs an agent subprocess (`AgentProcess`), streams its replies, tool calls, plan, and
  permission prompts into `AgentTranscript`, and hands the agent Island's own MCP endpoint in
  `session/new`, so the in-browser agent uses the same tools.

All code is CEF-free. The browser side (BrowserWindow implementing `AgentBrowserHost`, the sidebar
agent panel) lives in `src/main/`.

## Key Files

| File | Description |
|------|-------------|
| `jsonrpc.{h,cc}` | JSON-RPC 2.0 classify/build helpers shared by MCP and ACP |
| `browser_tools.{h,cc}` | `AgentBrowserHost` (what the window provides), `BrowserToolbox` (21 tools: `browser_*` window/tab/space tools, `page_*` read/snapshot/click/type/key/scroll/evaluate/screenshot built on DevTools calls) |
| `mcp_server.{h,cc}` | MCP `initialize`/`ping`/`tools/list`/`tools/call`; tool calls hop to the owner thread through a `Dispatcher` |
| `http_server.{h,cc}` | Loopback-only HTTP/1.1 server, one thread per connection, `Connection: close` |
| `http_client.{h,cc}` | Blocking loopback POST used by the bridge and the end-to-end tests |
| `agent_endpoint.{h,cc}` | Request policy (loopback `Host`/`Origin`, bearer token, `POST /mcp`), discovery file (`agent-endpoint.json`, mode 0600, next to `session.json`) |
| `acp_client.{h,cc}` | ACP v1 client state machine: `initialize` → `session/new` → `session/prompt`, `session/update`, `session/request_permission`, `session/cancel` |
| `agent_providers.{h,cc}` | Provider registry (Claude Code, Codex, OpenCode, Gemini CLI, Qwen Code, Goose, custom): command, required executable, install hint, docs URL; PATH availability detection (`ExecutableSearchDirs`, `FindExecutable`); `ResolveAgentCommand` (`ISLAND_AGENT_COMMAND` > provider preset or custom command) |
| `agent_process.{h,cc}` | fork/exec of the agent with stdio pipes, line reader, stderr tail, EOF→SIGTERM→SIGKILL shutdown of the process group |
| `agent_transcript.{h,cc}` | Folds ACP events into the panel's conversation model; `ToJson()` feeds the view |
| `mcp_bridge_main.cc` | `island_mcp_bridge` executable |

## For AI Agents

### Working In This Directory

- Keep it CEF-free. Anything that needs a CEF type belongs in `src/main/`.
- Tool results are data for a model: keep `ToolResult` text compact JSON, and report bad arguments
  as `is_error` results, never as protocol errors (MCP reserves protocol errors for unknown tools
  and malformed requests).
- Page scripts in `browser_tools.cc` return `JSON.stringify(...)` strings so values survive
  `returnByValue`; never interpolate caller text into a script except through `JsStringLiteral`.
- Security posture of the endpoint: loopback bind, DNS-rebinding checks, a per-launch 256-bit
  token, owner-only discovery file. Do not add a CORS allowance or a non-loopback bind.
- POSIX only for now: on Windows `LoopbackHttpServer::Start` and `AgentProcess::Start` fail with
  an explicit message. That is a documented gap.
- Provider availability is a hint, never a gate: agents launch through `$SHELL -lc`, whose `PATH`
  can differ from the app's (a Dock-launched macOS app sees a minimal one). Only add a preset whose
  ACP command is verified against the agent's own docs; the deprecated
  `@zed-industries/claude-code-acp` survives only as the prefs-migration constant
  `kLegacyClaudeAgentCommand`.
- `json_util.h` (in `src/main/`) is the shared parser; it now handles `\uXXXX` escapes and
  fractional numbers (`Type::kDouble`).

### Testing Requirements

```bash
cmake -B build-agent -S src/agent
cmake --build build-agent
ctest --test-dir build-agent --output-on-failure
```

72 tests as of 2026-10-06, also clean under `-fsanitize=address,undefined` and
`-fsanitize=thread`. The browser root builds the same `island_agent_tests` target.

## Dependencies

### Internal

- `src/main/json_util.h`

### External

- POSIX sockets, pipes, and `fork`/`execvp`; GoogleTest for tests

<!-- MANUAL: -->
