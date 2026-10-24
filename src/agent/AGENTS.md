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
| `agent_transcript.{h,cc}` | Folds ACP events into the panel's conversation model; a user turn extracts its sent page title/URL/selection tags into display-only context-chip metadata, restores escaped closing-tag text for display, and `ToJson()` feeds the view |
| `plan_graph.{h,cc}` | The research plan as a graph, built from ACP plan entries and their `_meta.island` (node kinds question/step/needs_you/join/result, states, at most 3 branch levels), validated, ordered and laid out on a grid; `plan.html` draws its `PlanGraphJson` |
| `action_gate.{h,cc}` | The action gate (agent-actions spec §2): `Classify` decides whether a page or browser action is kFree, kConfirm or kBlocked, from element facts, the page URL and the trusted sites; the rules table is below; CEF-free |
| `agent_task_queue.{h,cc}` | `AgentTaskQueue`: runs the person's agent work one task at a time (`TaskKind` chat/research/collect/ask, `TaskState`), with queue order, move, remove, stop and a waiting state for confirmations and plan approval; the window starts tasks through a `StartFn`; CEF-free |
| `plan_edits.{h,cc}` | The person's plan edits (skips, edited instructions) and the messages that send them back to the agent (`BuildPlanRunMessage`, `BuildExpandMessage`) |
| `mcp_bridge_main.cc` | `island_mcp_bridge` executable |

## Bookmark and favorite tool surface

| Tool | Access | Arguments |
|------|--------|-----------|
| `browser_list_bookmarks` | Free | optional exact `folder_path` |
| `browser_list_favorites` | Free | optional `scope`: `global` or persisted decimal space id |
| `browser_add_bookmark` | `kBookmarkMutation` confirmation | `url`, optional `title`, optional `folder_path` |
| `browser_remove_bookmark` | `kBookmarkMutation` confirmation | positive bookmark `id` |
| `browser_add_favorite` | `kBookmarkMutation` confirmation | positive bookmark `id`, `scope` |
| `browser_remove_favorite` | `kBookmarkMutation` confirmation | positive bookmark `id`, `scope` |

The host seam is asynchronous and CEF-free. Its default implementation returns `bookmarks
unavailable`; the window bridge is responsible for supplying a real store implementation.
`ListFavorites(nullopt)` returns every favorite in display order, never non-favorites.

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
| 9 | `kBookmarkMutation` | `browser_add/remove_bookmark`, `browser_add/remove_favorite` | kConfirm | no |

Not in the table: a Plan tab is refused before the gate asks anyone (`AgentTabInfo::refusal`), and a
page with no host or origin is never trusted or granted. "Allow for this task" grants are keyed by
(host, rule, scope) and cleared at each task start (`ResetTaskGrants`); `task_grant_allowed`
controls whether a card offers that grant, while `trusted_allowed` controls only "Trust this site".
`kBookmarkMutation` permits Allow for this task but never a trusted-site bypass, with a grant
narrowly bound to the operation, saved bookmark id and scope. Cards render "Allow for this task"
from `ConfirmationRequest::task_grant_allowed`, independently of `trusted_allowed`; the latter
controls only "Trust this site". Cards time out after 120 s. Withdrawn cards
answer `kWithdrawn` ("ask again"), not `kCancel` ("declined"). The host
(`WindowAgentHost`) owns the card; the toolbox only sees `RequestConfirmation`.

The word list lives in `kConsequentialWords` (`action_gate.cc`): English and Russian entries,
matched as whole words, case-insensitive, with spaces normalized and Cyrillic folded.

Re-check after a card (`after_wait`): the target is found again and `SameElement` compares it with
what was approved. Text and aria-label compare through `LiveLabelsMatch` (`browser_tools.h`). Two
forms are normalized by `NormalizeLiveCounters`: a time-like run (`m:ss` or `h:mm:ss`, minutes and
seconds 00-59, one or two leading digits: "0:42", "1:02:03") becomes `#`, unless a non-ASCII byte
touches it ("５0:42" stays exact), and one trailing parenthesized counter ("Like (12)") becomes
`(#)` when no digit comes before it. So a countdown ("Resend in 0:42" → "0:41",
"Pay now (expires 4:59)" → "4:58") or a counter ("Like (12)" → "(13)", "Checkout (3)" → "(12)")
still matches a second later. Everything else is exact, and a label with a currency mark or a `#`
is compared exactly. Currency marks are `kCurrencyMarks` in `browser_tools.cc`: ASCII-letter entries
match as whole tokens in any case ("SEK", "kr", "USDT"), other entries as substrings ("$", "руб",
"р."). So "Pay $5" → "Pay $500", "Transfer 10 USD" → "Transfer 9000 USD", "Buy 1" → "Buy 10", "Pay $5 (1)"
→ "Pay $5 (2)", "Pay 5 (1)" → "Pay 5 (2)", "Köp 29:90" → "Köp 299:90" and "Betala 29:90 kr" → "Betala
299:90 kr" fail. A literal `#` is unsafe for the same reason: the placeholder must not collide with
it. The page's `SyntheticClickScript` applies the same rule in JavaScript (`kLiveCountersJs`, built
from the same table), and `tests/agent/facts_probe.cjs` runs that copy on the same pairs. Everything
else stays exact: tag, type, autocomplete, name, id, is_submit and page origin. `StillApproved` then
judges the fresh facts with `Classify`: the element must be free now, or need the same action and
rule the person saw. A word change ("Delete account") fails the check, and so does a digits-only
change in name or id. The failing branch of `StillApproved` is belt-and-braces: no fact outside
`SameElement`'s set can turn an approved confirmation into another rule or a block, so no test
reaches it.

### Accepted debt (re-check of live labels)

Still passes by design, because the rule is written that way (a person may still want these fixed later):
- A valid `mm:ss` amount with no currency mark, for example "Köp 29:50" → "59:50" or "Book 10:30" → "11:30".
- An ASCII counter in parentheses, for example "Delete (3)" → "(300)" or "Buy (1)" → "(10)"; only a
  counter with no digit or non-ASCII text before it is normalized.
- A timer and a counter in parentheses that swap, for example "Retry (0:42)" → "Retry (42)": both
  become "Retry (#)", a timer-to-counter pass the two normalizations produce together.
- Text after the 80-character clip in `kFactsJs`: a currency mark or a digit past character 80 is not
  seen by the check.

Everything else fails closed: a changed amount, price, currency label or '#' label drops the action
and asks the person again.

### Extension points

Add one at a time, in the same commit as its test, and update the table above.

**A new gate rule** (`action_gate.h`, `action_gate.cc`):
1. Append the enumerator to `GateRule` (after `kUnclassified`). Do not reorder: the values are
   compared in tests and logged in summaries.
2. Add the check to `Classify` at its place in the order. The first matching rule wins, so a rule
   that must beat an existing one goes above it. Set `action`, `rule`, `summary` and
   `trusted_allowed`. Leave the scope and host tail alone.
3. If the rule needs a new fact, add the field to `ElementFacts`, read it in `ParseFacts` and
   collect it in `kFactsJs` (`browser_tools.cc`). Keep `tests/agent/facts_probe.cjs` passing.
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

**A bookmark or favorite tool** (`browser_tools.{h,cc}`):
1. Keep `AgentBrowserHost` CEF-free: exchange only `AgentBookmarkInfo`, `AgentBookmarkDraft` and
   `AgentFavoriteScope`; asynchronous default methods must fail closed with `bookmarks unavailable`.
2. Register only list/add/remove tools. Listing may call the host directly; never add bulk, import,
   folder-mutation or reorder operations to the agent surface.
3. Validate every argument before listing or asking. For remove/favorite operations, list first and
   reject an unknown bookmark id before `Gate`.
4. All persistent add/remove operations use `kBookmarkMutation`; cards name the operation, title,
   host, folder and/or scope. Do not let a trusted site bypass them. Test the pre-card no-write,
   Allow once/task, cancel, timeout and host-error paths.

**A new currency mark** (`kCurrencyMarks`, `browser_tools.cc`):
1. Add the mark to the table. An ASCII-letter mark matches as a whole token in any case ("SEK");
   any other mark matches as a substring ("руб"). The page copy reads the same table, so no
   JavaScript edit is needed.
2. Add a pair with the new mark to `kLiveCases` in `tests/agent/browser_tools_test.cpp` and to
   `livePairs` in `tests/agent/facts_probe.cjs`. A changed amount must be `false` (must not click).
3. Run `ctest -R BrowserToolsFactsProbe`, which runs the page copy on the same pairs.

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

### Known limitations

- No cancel on disconnect: `AgentEndpoint` does not notice when an MCP client closes its HTTP connection
  while a tool call waits. The confirmation card stays open until answered or its 120 s timeout, and an
  approved action still runs. An external client with a shorter tool timeout may stop waiting first
  (the user guide asks for 150 s). No protocol change in 0.8.

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
