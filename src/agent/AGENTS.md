<!-- Parent: ../AGENTS.md -->
<!-- Generated: 2026-10-06 | Updated: 2026-10-18 -->

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
| `browser_tools.{h,cc}` | `AgentBrowserHost` (what the window provides), `BrowserToolbox` (the tools: `browser_*` window/tab/space tools, `page_*` read/snapshot/click/type/key/scroll/evaluate/screenshot built on DevTools calls). Consequential calls go through its private `Gate()`; `kFactsJs` reads the element facts the gate needs (`ParseFacts`) |
| `mcp_server.{h,cc}` | MCP `initialize`/`ping`/`tools/list`/`tools/call`; tool calls hop to the owner thread through a `Dispatcher` |
| `http_server.{h,cc}` | Loopback-only HTTP/1.1 server, one thread per connection, `Connection: close` |
| `http_client.{h,cc}` | Blocking loopback POST used by the bridge and the end-to-end tests |
| `agent_endpoint.{h,cc}` | Request policy (loopback `Host`/`Origin`, bearer token, `POST /mcp`), discovery file (`agent-endpoint.json`, mode 0600, next to `session.json`) |
| `acp_client.{h,cc}` | ACP v1 client state machine: `initialize` → `session/new` → `session/prompt`, `session/update`, `session/request_permission`, `session/cancel` |
| `agent_providers.{h,cc}` | Provider registry (Claude Code, Codex, OpenCode, Gemini CLI, Qwen Code, Goose, custom): command, required executable, install hint, docs URL; PATH availability detection (`ExecutableSearchDirs`, `FindExecutable`); `ResolveAgentCommand` (`ISLAND_AGENT_COMMAND` > provider preset or custom command) |
| `agent_process.{h,cc}` | fork/exec of the agent with stdio pipes, line reader, stderr tail, EOF→SIGTERM→SIGKILL shutdown of the process group |
| `agent_transcript.{h,cc}` | Folds ACP events into the panel's conversation model; `ToJson()` feeds the view |
| `plan_graph.{h,cc}` | The research plan as a graph, built from ACP plan entries and their `_meta.island` (node kinds question/step/needs_you/join/result, states, at most 3 branch levels), validated, ordered and laid out on a grid; `plan.html` draws its `PlanGraphJson` |
| `action_gate.{h,cc}` | The action gate (agent-actions spec §2): `Classify` decides whether a page or browser action is kFree, kConfirm or kBlocked, from element facts, the page URL and the trusted sites; the rules table is below; CEF-free |
| `agent_task_queue.{h,cc}` | `AgentTaskQueue`: runs the person's agent work one task at a time (`TaskKind` chat/research/collect/ask, `TaskState`), with queue order, move, remove, stop and a waiting state for confirmations and plan approval; the window starts tasks through a `StartFn`; CEF-free |
| `plan_edits.{h,cc}` | The person's plan edits (skips, edited instructions) and the messages that send them back to the agent (`BuildPlanRunMessage`, `BuildExpandMessage`) |
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

### Action gate and confirmations

Every consequential page or browser action passes `Classify` before it runs. The gate sees only
facts the tool layer gathered (`ElementFacts`), the page URL, the navigation target after
`ResolveNavigationTarget` (never the raw argument) and the trusted sites. Rules are checked in
the order below and the first match decides. Rules 1–2 and "unclassified" ignore trust; the
others become kFree on a trusted host. Summaries are what the card shows the person.

| Order | `GateRule` | Applies to | Result | Trust covers it |
|-------|------------|------------|--------|-----------------|
| 1 | `kPassword` | `page_type`, `page_press_key` into a password or one-time-code field | kBlocked, never asked | no |
| 2 | `kCardField` | `page_type` into a card field (`autocomplete` `cc-*`, or a name/id with card, cvv, cvc, csc, ccnum, cc-num, expir, exp-date, exp_date, security-code) | kConfirm | no |
| – | `kUnclassified` | `page_type` without facts; `page_press_key` other than Enter without facts | kConfirm | no |
| 3 | `kSubmit` | `page_click` on a submit control, or a click without facts | kConfirm | yes |
| 4 | `kConsequentialWord` | `page_click` whose text or `aria-label` holds a word from `kConsequentialWords` | kConfirm | yes |
| 5 | `kEnterInForm` | `page_type` with `submit`, or Enter in a form (or Enter without facts) | kConfirm | yes |
| 6 | `kScript` | `page_evaluate` | kConfirm | yes |
| 7 | `kForeignNavigation` | `browser_navigate` to another origin, in a tab the person opened (agent-opened tabs are free) | kConfirm | yes |
| 8 | `kClosePersonTab` | `browser_close_tab` on a tab the person opened | kConfirm | yes |

Not in the table: a Plan tab is refused before the gate asks anyone (`AgentTabInfo::refusal`), and a
page with no host or origin is never trusted or granted. "Allow for this task" grants are keyed by
(host, rule, scope) and cleared at each task start (`ResetTaskGrants`); a decision with
`trusted_allowed == false` offers neither that grant nor "Trust this site". Cards time out after
120 s. Withdrawn cards answer `kWithdrawn` ("ask again"), not `kCancel` ("declined"). The host
(`WindowAgentHost`) owns the card; the toolbox only sees `RequestConfirmation`.

The word list lives in `kConsequentialWords` (`action_gate.cc`): English and Russian entries,
matched as whole words, case-insensitive, with spaces normalized and Cyrillic folded.

### Extension points

Add one at a time, in the same commit as its test, and update the table above.

**A new gate rule** (`action_gate.h`, `action_gate.cc`):
1. Append the enumerator to `GateRule` (after `kUnclassified`). Do not reorder: the values are
   compared in tests and logged in summaries.
2. Add the check to `Classify` at its place in the order. The first matching rule wins, so a rule
   that must beat an existing one goes above it. Set `action`, `rule`, `summary` and
   `trusted_allowed`. Leave the scope and host tail alone.
3. If the rule needs a new fact, add the field to `ElementFacts`, read it in `ParseFacts` and
   collect it in `kFactsJs` (`browser_tools.cc`). Keep `tests/agent/facts_probe.mjs` passing.
4. Test in `tests/agent/action_gate_test.cpp`: the ask, the trusted case (free) and the
   `trusted_allowed` value. `ctest -R ActionGate` runs them.

**A new gated tool** (`browser_tools.{h,cc}`):
1. Add a handler `void BrowserToolbox::<Name>(const json::Value& args, ToolCallback done)`. Bad
   arguments return `ToolResult::Error`.
2. Refuse first: `if (auto refused = Refusal(tab)) { done(*refused); return; }`.
3. Build a `GateInput` with `tool` set to the tool name, facts (`ReadFocusedFacts`, or the locator
   script), `page_url`, `target_url` (for navigation) and `tab_opened_by_agent`.
4. Call `Gate(input, tab, run, done, after_wait)`. `run` receives `done` and performs the host call.
   Pass `after_wait` when the page can change while the card is open: it re-reads the target,
   compares it with what was approved (`SameElement`) and acts only on the fresh state.
5. Register it in the constructor with `add(name, title, description, schema, read_only)` and
   dispatch it in `BrowserToolbox::Call`. Window-level actions without a page go in the `kActions`
   table instead, which maps the name to an `AgentBrowserAction` the host runs.
6. A tool that can be consequential goes through `Gate` as above, and its rule (if it is new) is
   added with the gate steps. A tool that is always safe skips `Gate` entirely. Test it in
   `tests/agent/browser_tools_test.cpp` against `tests/agent/fake_browser_host.h`.

**A new task kind** (`agent_task_queue.h`, then the window):
1. Append the enumerator to `TaskKind` (`kChat`, `kResearch`, `kCollect`, `kAsk` today).
2. Enqueue it with `AgentTaskQueue::Enqueue(kind, prompt, context, now_ms)`. The queue never looks
   at the kind: it only orders and runs tasks.
3. In the window, add a branch to `BrowserWindow::StartQueuedTask` (`src/main/browser_window.cc`)
   that sends the prompt, and set `in_flight_is_chat_` for it. A kind that needs its own turn-end
   handling adds a hook beside `FinishCollectTurn` in `FinishInFlightTurn`.
4. Add its name to the `kind` switch that builds the panel's task list. The compiler's
   `-Wswitch` warning shows a missed case.
5. Tests: `tests/agent/agent_task_queue_test.cpp` for the ordering and the stop and wait rules;
   `BrowserWindowPages` tests for the window path.

Queue contract: `StartFn` runs synchronously and may call back into the queue. Turn-scoped calls
(`OnTurnFinished`, `OnWaiting`, `OnProgress`) carry the task id and are ignored for any task that is
not live. `StopCurrent` waits for the turn end; `StopCurrentAndAdvanceNow` is for a turn that never
started or will never report.

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
