# Island Browser — Agent Actions: Confirmations, Task Queue, Context Asks, Collect from Tabs

## Status and baseline

- Proposed 2026-10-17. Direction A of the next big release (A → B → C → D; the release umbrella
  spec lists the other directions). Decisions in §9 come from the brainstorming session with the
  project owner on 2026-10-17 and are binding.
- Baseline: branch `release/next`, cut from `design/macos-native-spec` at `dcc27a1` (Native look,
  Plan tab). It merges the final `design/macos-native-spec` once Tasks 11 and 13 of that
  plan land. Work happens in worktree `next-agent`, branch `next/agent`, merged into
  `release/next`.
- What exists and is reused:
  - `src/agent/browser_tools.{h,cc}`: the MCP tool surface (`page_click`, `page_type`,
    `page_press_key`, `page_evaluate`, `browser_navigate`, `browser_open_tab`, …) on top of
    `AgentBrowserHost`. The sidebar ACP agent and external MCP clients both reach the browser only
    through these tools (`WindowAgentHost`, the MCP endpoint, the stdio bridge).
  - `src/agent/agent_session.{h,cc}`: one ACP session with a single queued prompt
    (`queued_prompt_`) and ACP permission requests (`ResolvePermission`), shown as a "Permission"
    card in `src/main/pages/agent.html`.
  - `src/main/address_indicators.h`: `AgentTabState {kIdle, kReading, kWaitingForYou}` drives the
    agent dot in the Native address field.
  - Research (`browser_window_research.cc`, `report_store.*`, `pages/report.html`) and the Plan tab.
  - `BrowserWindow::OnBeforeContextMenu` (today: "Open in peek") and the ⌘K command palette
    (`command_palette.h`, `PaletteCommandEntry`).
- This is a spec, not an accepted plan. Implementation starts after the owner accepts it and a
  plan written from it.

## Goals

1. The agent can act on pages (click, type, submit) without asking about every step, and never
   does anything irreversible or sensitive without a person's explicit yes.
2. Agent work keeps going in the background, one task at a time, with a visible queue the person
   controls.
3. The agent is one gesture away from whatever the person is looking at: a selection, a link, the
   current page, or a set of tabs.
4. Data the agent collects from several tabs lands in a table the person can read and export.

## Non-goals

- Several agent tasks running in parallel, several ACP sessions, or a task manager for them.
- Persisting the task queue across restarts (a restart empties it; the running task is cancelled).
- The agent ever reading or typing a password, in any mode, on any domain.
- Autofill, a password manager, or payment handling.
- Changing the agent's own (non-Island) tools; ACP permission requests keep their existing card.
- Scheduling tasks for later or recurring tasks.

## 1. Principles

- One gate. Every consequential page action is checked in one place, `ActionGate`, inside the
  CEF-free tool layer, so the sidebar agent, external MCP clients and the stdio bridge get the same
  rules. Nothing in the UI decides what is safe.
- Reversible is free, irreversible asks. Reading, scrolling, clicking links, typing into ordinary
  fields and opening pages are allowed. Submitting, paying, sending, deleting, card fields and
  taking a person's own tab to another site ask first.
- The person is always findable. A task that waits for a person turns the agent dot to
  `kWaitingForYou`, badges the sidebar Agent button and stops the queue until answered.
- Defaults are safe: a gate that cannot classify an action treats it as consequential.

## 2. The action gate (A1)

### 2.1 Classification

`src/agent/action_gate.{h,cc}` (CEF-free, unit-tested) classifies a tool call into
`ActionClass {kFree, kConfirm, kBlocked}` from the tool name, its arguments and a small
`ElementFacts` record the tool layer gathers about the target element before acting.

`ElementFacts` (filled by one `Runtime.evaluate` on the resolved element, same mechanism
`page_click` already uses to find the element center):
`tag`, `type` (input type), `autocomplete`, `name`, `id`, `aria_label`, `text` (trimmed visible
text, first 80 chars), `in_form` (bool), `form_action_origin`, `is_submit` (button/input of type
submit or a button without a type inside a form), `page_origin`.

Rules, in order:

| # | Situation | Class |
|---|---|---|
| 1 | `page_type` / `page_press_key` into an `input type=password`, or an element whose `autocomplete` is `current-password` / `new-password` / `one-time-code` | **kBlocked** (any domain, trusted or not) |
| 2 | `page_type` into a card field: `autocomplete` starting with `cc-`, or `name`/`id` matching `card|cc-?num|cvc|cvv|csc|expir` (case-insensitive) | kConfirm (always, also on trusted domains; see §9.3) |
| 3 | `page_click` on an element with `is_submit`, or whose text / aria-label matches the consequential word list (§2.2) | kConfirm |
| 4 | `page_press_key` Enter while focus is in a form field (`in_form`) | kConfirm |
| 5 | `page_evaluate` | kConfirm (arbitrary script can do anything) |
| 6 | `browser_navigate` of a tab the **person** opened to an origin different from the tab's current one | kConfirm |
| 7 | `browser_close_tab` of a tab the person opened | kConfirm |
| 8 | Anything else (`page_read`, `page_snapshot`, `page_scroll`, `page_screenshot`, `browser_open_tab`, navigation of agent-opened tabs, clicks on links and ordinary buttons, typing into ordinary fields) | kFree |

Rules 3–7 become kFree on a trusted domain (§2.4). Rule 1 never changes. Rule 2 stays kConfirm.
If `ElementFacts` cannot be gathered (evaluation error, detached node), rules 3–4 resolve to
kConfirm.

"Opened by the person" vs "opened by the agent": `Tab` gains `opened_by_agent` (set by
`OpenTab` from the tool layer, by Research and by Collect; persisted in the session). Rule 6
exists so the agent can browse freely in its own tabs while research opens many sites.

### 2.2 Consequential words

A fixed list in `action_gate.cc`, matched as whole words, case-insensitive, on trimmed text and
aria-label: English `submit, send, post, publish, buy, purchase, order, checkout, pay, place order,
confirm, delete, remove, unsubscribe, transfer, book, reserve, sign up, subscribe`; Russian
`отправить, опубликовать, купить, оплатить, заказать, оформить, подтвердить, удалить, перевести,
забронировать, подписаться`. The list is data, covered by a test that each word classifies as
kConfirm.

### 2.3 The confirmation card

A kConfirm call does not run; the tool layer asks `AgentBrowserHost::RequestConfirmation(
ConfirmationRequest, callback)` and waits.

`ConfirmationRequest`: `id`, `tool`, a one-line human `summary` built by the gate (for example
`Click "Place order" on shop.example.com`, `Type into the card number field on pay.example.com`,
`Run a script on news.example.com`, `Open bank.example.com in your tab "Inbox"`), `origin`,
`tab` (index), `trusted_allowed` (false for card fields).

The card appears in the agent panel (`agent.html`) above the composer, styled like the existing
Permission card: title "Allow this action?", the summary, the origin in mono, and buttons:
**Allow once**, **Allow for this task** (same origin and same rule, until the task ends),
**Trust this site** (hidden when `trusted_allowed` is false), **Cancel** (the default for Esc).

While a card is open:
- the task's tab shows `AgentTabState::kWaitingForYou`; the Native sidebar Agent button shows a
  small accent dot; if the panel is closed, nothing pops up on its own;
- the queue (§3) does not advance;
- the tool call is answered with the outcome. Cancel answers the tool with an error result
  `"The person declined this action."` so the agent can choose another route.

External MCP clients get the same card. Their call waits up to 120 s; on timeout it returns
`"No one confirmed this action in time."` and the card is withdrawn.

### 2.4 Trusted domains

- Pref `agent_trusted_sites`: a list of hosts (lowercase, without a leading `www.`). A host
  matches when it equals an entry or ends with `"." + entry`.
- "Trust this site" adds the current page host (without `www.`) and allows the pending action.
- Settings → Agent & tools gets a "Trusted sites" list with Remove buttons and a short line:
  "The agent can submit forms and run scripts on these sites without asking. It never types
  passwords."
- Trusted sites change rules 3–7 only (§2.1).

## 3. Background tasks and the queue (A4)

### 3.1 Model

`src/agent/agent_task_queue.{h,cc}` (CEF-free): an ordered list of `AgentTask` and at most one
running task.

`AgentTask`: `id` (`t` + number), `kind` (`kChat`, `kResearch`, `kCollect`, `kAsk` for context
asks), `title` (first line of the request, 60 chars), `prompt`, `context` (page URL, title,
selection), `created_ms`, `state` (`kQueued`, `kRunning`, `kWaitingForYou`, `kDone`, `kFailed`,
`kCancelled`), `progress` (`done`, `total` from the latest ACP plan of the task, if any).

Operations: `Enqueue`, `MoveUp`/`MoveDown`, `Remove` (queued only), `StopCurrent` (cancels the
ACP turn; the next task starts), `OnTurnFinished`, `OnWaiting(bool)`. The queue starts the next
task when the current one finishes and nothing waits for a person. Finished tasks stay listed until
the panel's "Clear finished" or the next restart.

`AgentSession::queued_prompt_` stays as the "connecting" buffer; the task queue sits above it and
hands it one prompt at a time.

### 3.2 Behaviour

- Sending a chat message while a task runs enqueues it and shows "Queued · #2" under the message.
- Research and Collect enter the same queue. A research task in the Plan tab's "Waiting for
  approval" state counts as waiting for a person: the queue holds until Run or Cancel.
- Closing the agent panel does not stop anything. The task keeps running; the agent dot and the
  sidebar Agent button show its state.
- The sidebar Agent button (Native) shows `Agent · 3/7` while a task with a plan runs, and
  `Agent · 2 queued` when tasks wait. Arc and Graphite show the same text in their agent row.

### 3.3 Panel

`agent.html` gets a "Tasks" strip at the top when the queue is not empty: the running task (title,
progress, Stop), then queued tasks with ↑ ↓ and ✕, then finished ones (collapsed, "Clear
finished"). ⌘K "Agent tasks" opens the panel scrolled to the strip.

## 4. Asking from context (A2)

| Entry point | Action | Task |
|---|---|---|
| Context menu on a selection | "Ask agent about selection" | `kAsk`; composer opens prefilled with the selection as a quote chip; the person types the question |
| Context menu on a selection | "Explain" | `kAsk`; sends "Explain this in plain words." with the selection |
| Context menu on a link | "Summarize linked page" | opens the link in Peek for the person and enqueues `kAsk` "Summarize this page." with the link URL; the agent reads it in its own background tab, which it closes afterwards |
| ⌘K | "Summarize page" | `kAsk` with the current page as context |
| ⌘K | "Ask about page…" | opens the panel with the page chip, cursor in the composer |
| ⌘K | "Collect from tabs…" | §5 |
| ⌘K | "Agent tasks" | §3.3 |
| ⌘J | open the agent | the composer shows the current tab as a toggle chip (on by default; struck through when off, and then the prompt carries no page); ⌘J does not read the selection, so the prompt never carries one. Only the context-menu and quote path has a removable chip and a selection |

Context goes into the prompt as a short block (URL, title, the selection up to 4 KB). The agent
reads page text itself with `page_read`; Island does not paste whole pages into prompts.

The context-menu items join "Open in peek" in `OnBeforeContextMenu` with new
`MENU_ID_USER_FIRST + n` ids; they are hidden while no agent is configured.

## 5. Collect from tabs (A3)

### 5.1 Flow

1. ⌘K "Collect from tabs…" opens a sheet in the agent panel: a checklist of the active space's
   tabs (all non-pinned tabs checked), a text field "What to collect" (placeholder "price, rating
   and delivery time for each product"), and **Collect**.
2. Collect enqueues a `kCollect` task. Its prompt (`src/main/collect_prompt.{h,cc}`, CEF-free)
   lists the chosen tabs by index, title and URL, asks the agent to `page_read` each one and to
   answer with exactly one Markdown table (first column: the source, as a link) between the same
   report markers Research uses, followed by notes on anything it could not find.
3. The result is stored through `ReportStore` with `kind: "collection"` and opens as a Report tab.
   The Plan tab is not used. The Reports list shows a table icon for collections.

### 5.2 Export

`report.html` gets an **Export** menu: **CSV**, **JSON**, **Markdown**.

- Markdown: the report as stored.
- CSV and JSON: built from the first Markdown table of the report by
  `src/main/table_export.{h,cc}` (CEF-free): header row → keys; cells unescaped from Markdown
  (links become their URL in CSV/JSON plus a `<column> title` sibling only if the text differs);
  CSV per RFC 4180 with CRLF and a UTF-8 BOM so spreadsheets open it correctly; JSON as an array
  of objects. Both are disabled (with a tooltip "No table in this report") when there is no table.
- Saving uses the platform save dialog (`CefBrowserHost::RunFileDialog`, save mode) with the
  default name `<report title>.csv|json|md` in the downloads directory (`download_path.h`).

`ResearchReport` gains `kind` (`"research"` default, `"collection"`); schema version 2 reads
version 1 files as research.

## 6. Where each piece lives

| Piece | Files |
|---|---|
| Gate and element facts | `src/agent/action_gate.{h,cc}`, `browser_tools.cc` (gather facts, ask the host), `browser_tools.h` (`RequestConfirmation` on `AgentBrowserHost`, `ConfirmationRequest`) |
| Card and trusted sites | `window_agent_host.cc`, `browser_window.cc` (card state to `agent.html`), `pages/agent.html`, `pages/settings.html`, `prefs_store.*` |
| Tab origin flag | `tab.{h,cc}` (`opened_by_agent`), `session_store.cc` |
| Queue | `src/agent/agent_task_queue.{h,cc}`, `agent_session.*`, `browser_window.cc`, `pages/agent.html`, `browser_chrome.cc` (Agent button text and dot) |
| Context asks | `browser_window.cc` (`OnBeforeContextMenu`, `OnContextMenuCommand`), `command_palette.*`, `pages/agent.html` (chips) |
| Collect and export | `src/main/collect_prompt.*`, `src/main/table_export.*`, `report_store.*`, `pages/report.html`, `browser_window_research.cc` (shared report-opening path) |

New sources are added to the existing targets; `src/agent/` files keep building standalone
(`cmake -S src/agent`).

## 7. Testing

- `action_gate_test.cpp`: every row of §2.1 for trusted and untrusted hosts, every word of §2.2,
  password and card fields, missing facts → kConfirm, host matching (`www.`, subdomains, a
  look-alike host such as `example.com.evil.net` does not match `example.com`).
- `browser_tools_test.cpp`: a kConfirm click does not dispatch input events until the fake host
  answers Allow; Cancel returns the declined error; password typing never reaches `Input.insertText`;
  "Allow for this task" covers the next same-origin, same-rule call and nothing else.
- `agent_task_queue_test.cpp`: order, move, remove, stop, waiting blocks advance, finished
  retention, progress from plan entries.
- `table_export_test.cpp`: escaping (commas, quotes, newlines, pipes), links, ragged rows, no table,
  BOM and CRLF.
- `collect_prompt_test.cpp`, `report_store_test.cpp` (kind round trip, v1 → v2).
- `browser_window_*_test.cpp`: context-menu items present only with an agent configured; a card
  sets `kWaitingForYou`; trusted-site add/remove persists.
- `tests/design/test_local_pages.py`: card buttons, Tasks strip, Export menu, chips, reduced
  motion, no hard-coded colors.
- Dev captures (light and dark): a new dev action `agent_fixture:confirm` shows a pending card and a
  two-task queue; `report_fixture:collection` shows a collection report with the Export menu open.

## 8. Acceptance

1. On an untrusted site the agent fills a search box and clicks a result without a card; clicking
   "Place order" shows the card; Cancel leaves the page untouched and the agent continues.
2. "Trust this site" lets the next submit on that site run without a card; removing the site in
   Settings brings the card back.
3. The agent can never type into a password field (an error result every time, also on trusted
   sites); a card field always asks.
4. With the panel closed, a task runs to the end; a card makes the agent dot and Agent button show
   "waiting for you"; the next queued task starts only after the answer.
5. Three queued tasks can be reordered and one removed; Stop ends the current one and starts the
   next.
6. Selection → "Explain", link → "Summarize linked page", ⌘K "Summarize page" and ⌘J with the page
   chip each produce an answer in the panel.
7. Collect over three tabs opens a Report tab with one table; Export CSV opens in Numbers/Excel with
   correct columns and non-ASCII text; JSON is an array of objects.
8. ctest and pytest green; light and dark captures reviewed.

## 9. Decisions (owner, 2026-10-17)

1. A1 = option 1 with trusted domains: reversible actions run without asking; submit / buy / pay /
   send / delete, card fields and cross-domain navigation ask; trusted sites skip the card; password
   fields are always blocked for the agent.
2. A4 = one background task at a time plus a queue the person can reorder, remove from and stop.
3. Controller ruling (flag for the owner at spec review): card fields ask even on trusted sites,
   because a trusted site still should not get card data silently. Cost if wrong: drop rule 2's
   exception, one line plus a test.
4. Controller ruling: "cross-domain navigation mid-task" applies to tabs the person opened (rule 6);
   the agent's own tabs navigate freely, otherwise research would ask on every source. Cost if
   wrong: rule 6 also covers agent tabs, one condition.
5. A3 = Report tab with a table plus Export to CSV / JSON / Markdown.
6. A2 = the entry points in §4, answers in the panel, through the queue.
