# Island Agent Actions Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** The agent acts on pages behind one confirmation gate, runs one background task at a time
from a visible queue, can be asked from any selection, link or page, and collects data from tabs
into an exportable table.

**Architecture:** Decisions live in CEF-free units (`src/agent/action_gate.*`,
`src/agent/agent_task_queue.*`, `src/main/collect_prompt.*`, `src/main/table_export.*`) with
unit tests. `BrowserToolbox` asks the gate before every page action and, for kConfirm, asks the
host through a new `AgentBrowserHost::RequestConfirmation`. `BrowserWindow` turns pending
confirmations and the queue into state for `pages/agent.html` and the sidebar Agent button.

**Tech Stack:** C++20, CEF 150 Alloy + cef_views, GoogleTest, pytest (`tests/design`), local HTML
pages with ThemeJson CSS variables.

**Spec:** `docs/superpowers/specs/2026-10-17-island-agent-actions-design.md` (§9 Decisions are
binding; §9.3 and §9.4 were confirmed by the owner on 2026-10-17).

## Global Constraints

- Work in worktree `next-agent`, branch `next/agent`, cut from `release/next` **after**
  `release/next` has merged the finished `design/macos-native-spec` (Tasks 11 and 13 of the Native
  plan add `Tab::group_parent` and the Plan-tab grouping this plan builds on).
- The agent never types into a password field (`input type=password`, or autocomplete
  `current-password`, `new-password`, `one-time-code`), on any site, trusted or not.
- Card fields (autocomplete starting with `cc-`, or name/id matching `card|cc-?num|cvc|cvv|csc|expir`)
  always ask, also on trusted sites.
- Unclassifiable actions ask (missing element facts → kConfirm).
- External MCP confirmations time out after **120 s** with `"No one confirmed this action in time."`;
  Cancel returns `"The person declined this action."`.
- Pref key `agent.trusted_sites` (list of lowercase hosts without leading `www.`); a host matches an
  entry when equal to it or ending with `"." + entry`.
- No persistent `CefWindow` overlay (`src/main/overlay_lifetime.h`); all new UI lives in the agent
  panel, local pages, the context menu, the ⌘K palette and existing sidebar views.
- `src/agent/` keeps building standalone (`cmake -S src/agent`); no CEF include there.
- Every UI change works in Native, Arc and Graphite, light and dark; page CSS uses ThemeJson
  variables only.
- Minimize test runs: implementers run only targeted gtest filters and one pytest file; the
  controller runs the full ctest and pytest once after each merge.
- CHANGELOG `[Unreleased]` entry for each user-visible change; commit trailer
  `Co-Authored-By: Claude Opus 5.5 <noreply@anthropic.com>`.

## Review Focus

1. A page that relabels its submit button after the facts were read (text changes between facts and
   click): the gate must classify from facts read in the same evaluation that returns the click
   point — Task 2 test `FactsAndClickPointComeFromOneEvaluation`.
2. Look-alike hosts (`example.com.evil.net`, `evil-example.com`, `EXAMPLE.com.`) must not match a
   trusted `example.com` — Task 1 test `LookAlikeHostsDoNotMatch`.
3. The panel is closed and an external MCP client waits for a card: the card must still be there
   when the panel opens and must disappear after the 120 s timeout — Task 4 test
   `ConfirmationTimesOutAndWithdrawsTheCard`.
4. The person stops a task that is waiting for a confirmation: the pending tool call must be
   answered (declined) exactly once and the next task must start — Task 6 test
   `StoppingAWaitingTaskDeclinesItsCardAndAdvances`.
5. Collect over tabs with non-ASCII text, pipes and newlines in cells: CSV must round-trip in a
   spreadsheet (BOM, CRLF, quoting) — Task 8 test `CsvQuotesPipesNewlinesAndKeepsUtf8`.

---

### Task 1: The action gate

**Files:**
- Create: `src/agent/action_gate.h`, `src/agent/action_gate.cc`
- Modify: `src/agent/CMakeLists.txt` (add the source to the agent library and the test to its
  test target, next to `plan_graph.cc` / `plan_graph_test.cpp`)
- Test: `tests/agent/action_gate_test.cpp`

**Interfaces:**
- Produces:

```cpp
namespace island::agent {
enum class ActionClass : std::uint8_t { kFree, kConfirm, kBlocked };
enum class GateRule : std::uint8_t {
    kNone, kPassword, kCardField, kSubmit, kConsequentialWord, kEnterInForm, kScript,
    kForeignNavigation, kClosePersonTab,
};
struct ElementFacts {
    std::string tag, type, autocomplete, name, id, aria_label, text;
    bool in_form = false;
    bool is_submit = false;
    std::string page_origin;  // "https://shop.example.com"
};
struct GateInput {
    std::string tool;                         // "page_click", "page_type", ...
    std::optional<ElementFacts> facts;        // target or focused element, when gathered
    std::string key;                          // page_press_key: the key name
    std::string page_url;                     // the tab's current URL
    std::string target_url;                   // browser_navigate: the new URL
    bool tab_opened_by_agent = false;
};
struct GateDecision {
    ActionClass action = ActionClass::kFree;
    GateRule rule = GateRule::kNone;
    std::string summary;                      // "Click \"Place order\" on shop.example.com"
    std::string host;                         // page host without "www."
    bool trusted_allowed = true;              // false for card fields
};
[[nodiscard]] std::string HostOf(std::string_view url);          // lowercase, no "www.", no port
[[nodiscard]] bool HostMatchesTrusted(std::string_view host,
                                      const std::vector<std::string>& trusted);
[[nodiscard]] bool IsConsequentialText(std::string_view text);
[[nodiscard]] GateDecision Classify(const GateInput& input,
                                    const std::vector<std::string>& trusted_sites);
}  // namespace island::agent
```

- [ ] **Step 1: Write the failing tests** in `tests/agent/action_gate_test.cpp`:

```cpp
#include "action_gate.h"
#include <gtest/gtest.h>

namespace island::agent {
namespace {
ElementFacts Button(std::string text, bool submit = false, bool in_form = false) {
    ElementFacts f; f.tag = "button"; f.text = std::move(text); f.is_submit = submit;
    f.in_form = in_form; f.page_origin = "https://shop.example.com"; return f;
}
ElementFacts Input(std::string type, std::string autocomplete = {}, std::string name = {}) {
    ElementFacts f; f.tag = "input"; f.type = std::move(type);
    f.autocomplete = std::move(autocomplete); f.name = std::move(name); f.in_form = true;
    f.page_origin = "https://shop.example.com"; return f;
}
GateInput Call(std::string tool, std::optional<ElementFacts> facts = {}) {
    GateInput in; in.tool = std::move(tool); in.facts = std::move(facts);
    in.page_url = "https://shop.example.com/cart"; return in;
}
const std::vector<std::string> kNone;
const std::vector<std::string> kTrusted = {"example.com"};

TEST(ActionGateTest, ReadingAndLinksAreFree) {
    for (const char* tool : {"page_read", "page_snapshot", "page_scroll", "page_screenshot",
                             "browser_open_tab", "browser_list_tabs"}) {
        EXPECT_EQ(Classify(Call(tool), kNone).action, ActionClass::kFree) << tool;
    }
    EXPECT_EQ(Classify(Call("page_click", Button("Next page")), kNone).action, ActionClass::kFree);
    EXPECT_EQ(Classify(Call("page_type", Input("search")), kNone).action, ActionClass::kFree);
}
TEST(ActionGateTest, PasswordFieldsAreBlockedEvenWhenTrusted) {
    for (auto facts : {Input("password"), Input("text", "current-password"),
                       Input("text", "new-password"), Input("text", "one-time-code")}) {
        for (const auto* trusted : {&kNone, &kTrusted}) {
            GateDecision d = Classify(Call("page_type", facts), *trusted);
            EXPECT_EQ(d.action, ActionClass::kBlocked);
            EXPECT_EQ(d.rule, GateRule::kPassword);
        }
    }
    EXPECT_EQ(Classify(Call("page_press_key", Input("password")), kTrusted).action,
              ActionClass::kBlocked);
}
TEST(ActionGateTest, CardFieldsAlwaysAsk) {
    for (auto facts : {Input("text", "cc-number"), Input("text", "cc-csc"),
                       Input("text", {}, "cardNumber"), Input("tel", {}, "cvv"),
                       Input("text", {}, "exp-date-expiry")}) {
        GateDecision d = Classify(Call("page_type", facts), kTrusted);
        EXPECT_EQ(d.action, ActionClass::kConfirm) << facts.name << facts.autocomplete;
        EXPECT_EQ(d.rule, GateRule::kCardField);
        EXPECT_FALSE(d.trusted_allowed);
    }
}
TEST(ActionGateTest, SubmitsAndConsequentialWordsAskUnlessTrusted) {
    EXPECT_EQ(Classify(Call("page_click", Button("Go", true, true)), kNone).rule, GateRule::kSubmit);
    GateDecision word = Classify(Call("page_click", Button("Place order")), kNone);
    EXPECT_EQ(word.action, ActionClass::kConfirm);
    EXPECT_EQ(word.rule, GateRule::kConsequentialWord);
    EXPECT_EQ(word.summary, "Click \"Place order\" on shop.example.com");
    EXPECT_EQ(Classify(Call("page_click", Button("Place order")), kTrusted).action,
              ActionClass::kFree);
}
TEST(ActionGateTest, EveryConsequentialWordAsks) {
    for (const char* w : {"submit", "send", "post", "publish", "buy", "purchase", "order",
                          "checkout", "pay", "place order", "confirm", "delete", "remove",
                          "unsubscribe", "transfer", "book", "reserve", "sign up", "subscribe",
                          "отправить", "опубликовать", "купить", "оплатить", "заказать",
                          "оформить", "подтвердить", "удалить", "перевести", "забронировать",
                          "подписаться"}) {
        EXPECT_TRUE(IsConsequentialText(w)) << w;
    }
    EXPECT_FALSE(IsConsequentialText("Payload viewer"));   // whole words only
    EXPECT_FALSE(IsConsequentialText("Bookmarks"));
    EXPECT_TRUE(IsConsequentialText("  BUY NOW "));
}
TEST(ActionGateTest, EnterInAFormAsks) {
    GateInput in = Call("page_press_key", Input("text"));
    in.key = "Enter";
    EXPECT_EQ(Classify(in, kNone).rule, GateRule::kEnterInForm);
    in.key = "Tab";
    EXPECT_EQ(Classify(in, kNone).action, ActionClass::kFree);
}
TEST(ActionGateTest, ScriptsAskUnlessTrusted) {
    EXPECT_EQ(Classify(Call("page_evaluate"), kNone).rule, GateRule::kScript);
    EXPECT_EQ(Classify(Call("page_evaluate"), kTrusted).action, ActionClass::kFree);
}
TEST(ActionGateTest, ForeignNavigationAsksOnlyInPersonTabs) {
    GateInput in = Call("browser_navigate");
    in.target_url = "https://bank.other.org/";
    EXPECT_EQ(Classify(in, kNone).rule, GateRule::kForeignNavigation);
    in.tab_opened_by_agent = true;
    EXPECT_EQ(Classify(in, kNone).action, ActionClass::kFree);
    in.tab_opened_by_agent = false;
    in.target_url = "https://shop.example.com/other";
    EXPECT_EQ(Classify(in, kNone).action, ActionClass::kFree);  // same origin
}
TEST(ActionGateTest, ClosingAPersonTabAsks) {
    GateInput in = Call("browser_close_tab");
    EXPECT_EQ(Classify(in, kNone).rule, GateRule::kClosePersonTab);
    in.tab_opened_by_agent = true;
    EXPECT_EQ(Classify(in, kNone).action, ActionClass::kFree);
}
TEST(ActionGateTest, MissingFactsAsk) {
    EXPECT_EQ(Classify(Call("page_click"), kNone).action, ActionClass::kConfirm);
    GateInput in = Call("page_press_key");
    in.key = "Enter";
    EXPECT_EQ(Classify(in, kNone).action, ActionClass::kConfirm);
}
TEST(ActionGateTest, LookAlikeHostsDoNotMatch) {
    EXPECT_TRUE(HostMatchesTrusted("example.com", kTrusted));
    EXPECT_TRUE(HostMatchesTrusted("shop.example.com", kTrusted));
    EXPECT_FALSE(HostMatchesTrusted("example.com.evil.net", kTrusted));
    EXPECT_FALSE(HostMatchesTrusted("evil-example.com", kTrusted));
    EXPECT_EQ(HostOf("https://WWW.Example.com.:8443/x"), "example.com");
    EXPECT_EQ(HostOf("not a url"), "");
}
}  // namespace
}  // namespace island::agent
```

- [ ] **Step 2: Run and confirm they fail** (missing header):
  `cmake --build build --target island_agent_tests 2>&1 | tail -3`.
- [ ] **Step 3: Implement** `action_gate.{h,cc}` with the interface above. Rules in this order:
  password (kBlocked) → card field (kConfirm, `trusted_allowed=false`) → for `page_click`:
  missing facts → kConfirm/kSubmit summary "Click an element on <host>"; `is_submit` → kSubmit;
  consequential text or aria-label → kConsequentialWord → for `page_press_key`: key `Enter` with
  missing facts or `in_form` → kEnterInForm → `page_evaluate` → kScript → `browser_navigate` with
  `!tab_opened_by_agent` and `HostOf(target)!=HostOf(page_url)` → kForeignNavigation →
  `browser_close_tab` with `!tab_opened_by_agent` → kClosePersonTab → otherwise kFree. Rules
  kSubmit…kClosePersonTab become kFree when `HostMatchesTrusted(HostOf(page_url), trusted)`.
  `HostOf` lowercases, strips scheme, userinfo, port, a trailing dot and a leading `www.`; returns
  `""` without `://`. `IsConsequentialText` lowercases ASCII, trims, and matches each word/phrase
  of spec §2.2 at word boundaries (a boundary is the string edge or a char that is not a letter or
  digit; treat bytes ≥ 0x80 as letters so Cyrillic words are not split). Summaries:
  `Click "<text>" on <host>`, `Type into the card number field on <host>`,
  `Press Enter in a form on <host>`, `Run a script on <host>`, `Open <target host> in your tab`,
  `Close your tab on <host>`; text is clipped to 40 chars with `…`.
- [ ] **Step 4: Run** `./build/src/agent/island_agent_tests --gtest_filter='ActionGate*'` → PASS.
- [ ] **Step 5: Commit** `git commit -m "Add the agent action gate"`.

### Task 2: Gate every page action in the toolbox

**Files:**
- Modify: `src/agent/browser_tools.h` (`AgentTabInfo::opened_by_agent`, `ConfirmationRequest`,
  `ConfirmationAnswer`, two new `AgentBrowserHost` methods, toolbox grants),
  `src/agent/browser_tools.cc`, `tests/agent/fake_browser_host.h`,
  `src/main/window_agent_host.{h,cc}` (temporary implementations: `TrustedSites()` returns `{}`,
  `RequestConfirmation` answers `kCancel` — Task 4 replaces them)
- Test: `tests/agent/browser_tools_test.cpp`

**Interfaces:**
- Consumes: Task 1 (`Classify`, `GateInput`, `ElementFacts`, `GateDecision`).
- Produces:

```cpp
enum class ConfirmationAnswer : std::uint8_t { kAllowOnce, kAllowForTask, kTrustSite, kCancel, kTimeout };
struct ConfirmationRequest {
    std::string id;            // "c1", "c2", ... unique per toolbox
    std::string tool;
    std::string summary;
    std::string host;
    std::optional<std::size_t> tab;
    bool trusted_allowed = true;
};
// AgentBrowserHost additions (pure virtual):
virtual std::vector<std::string> TrustedSites() = 0;
virtual void RequestConfirmation(ConfirmationRequest request,
                                 std::function<void(ConfirmationAnswer)> done) = 0;
// AgentTabInfo addition:
bool opened_by_agent = false;
// BrowserToolbox additions:
void ResetTaskGrants();   // forgets "Allow for this task" grants; the queue calls it per task
```

- [ ] **Step 1: Failing tests** in `browser_tools_test.cpp` (the fake gets `std::vector<std::string>
  trusted;`, `std::vector<ConfirmationRequest> confirmations;`, `std::deque<ConfirmationAnswer>
  answers;` — an empty `answers` answers `kCancel`):

```cpp
TEST(BrowserToolsTest, ConsequentialClickWaitsForAllow) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    host.QueueScriptResult(R"({"ok":true,"x":10,"y":20,"facts":{"tag":"button",
        "text":"Place order","in_form":true,"is_submit":false,
        "page_origin":"https://shop.example.com"}})");
    host.tabs[0].url = "https://shop.example.com/cart";
    host.answers.push_back(ConfirmationAnswer::kAllowOnce);
    ToolResult r = CallTool(toolbox, "page_click", Value::MakeObject().Set("ref", Value::Int(3)));
    EXPECT_FALSE(r.is_error);
    ASSERT_EQ(host.confirmations.size(), 1u);
    EXPECT_EQ(host.confirmations[0].summary, "Click \"Place order\" on shop.example.com");
    EXPECT_EQ(host.devtools.back().method, "Input.dispatchMouseEvent");
}
TEST(BrowserToolsTest, CancelledClickDispatchesNoInput) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    host.QueueScriptResult(R"({"ok":true,"x":1,"y":1,"facts":{"tag":"button","text":"Delete",
        "page_origin":"https://a.test"}})");
    ToolResult r = CallTool(toolbox, "page_click", Value::MakeObject().Set("ref", Value::Int(1)));
    EXPECT_TRUE(r.is_error);
    EXPECT_EQ(r.content.at(0).text, "The person declined this action.");
    for (const auto& call : host.devtools) EXPECT_NE(call.method, "Input.dispatchMouseEvent");
}
TEST(BrowserToolsTest, PasswordTypingNeverReachesInsertText) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    host.trusted = {"a.test"};
    host.QueueScriptResult(R"({"ok":true,"facts":{"tag":"input","type":"password",
        "page_origin":"https://a.test"}})");
    ToolResult r = CallTool(toolbox, "page_type",
        Value::MakeObject().Set("ref", Value::Int(2)).Set("text", Value::String("hunter2")));
    EXPECT_TRUE(r.is_error);
    EXPECT_TRUE(host.confirmations.empty());
    for (const auto& call : host.devtools) EXPECT_NE(call.method, "Input.insertText");
}
TEST(BrowserToolsTest, AllowForTaskCoversSameHostAndRuleOnly) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    host.tabs[0].url = "https://a.test/";
    host.answers.push_back(ConfirmationAnswer::kAllowForTask);
    CallTool(toolbox, "page_evaluate", Value::MakeObject().Set("expression", Value::String("1")));
    CallTool(toolbox, "page_evaluate", Value::MakeObject().Set("expression", Value::String("2")));
    EXPECT_EQ(host.confirmations.size(), 1u);
    toolbox.ResetTaskGrants();
    CallTool(toolbox, "page_evaluate", Value::MakeObject().Set("expression", Value::String("3")));
    EXPECT_EQ(host.confirmations.size(), 2u);
}
TEST(BrowserToolsTest, FactsAndClickPointComeFromOneEvaluation) {
    FakeBrowserHost host;
    BrowserToolbox toolbox(host);
    host.QueueScriptResult(R"({"ok":true,"x":5,"y":5,"facts":{"tag":"a","text":"Next",
        "page_origin":"https://a.test"}})");
    CallTool(toolbox, "page_click", Value::MakeObject().Set("ref", Value::Int(1)));
    int evaluations = 0;
    for (const auto& call : host.devtools) evaluations += call.method == "Runtime.evaluate";
    EXPECT_EQ(evaluations, 1);
}
```

  Also: `TrustSiteAnswerAllowsAndIsNotAskedAgainWhenHostTrustsIt` (after kTrustSite the fake adds
  the host to `trusted`; the next submit is not asked), `ForeignNavigationOfPersonTabAsks`,
  `AgentTabNavigatesFreely`, `TimeoutReturnsTheTimeoutError`.
- [ ] **Step 2: Run** `--gtest_filter='BrowserToolsTest.*'` → the new tests fail.
- [ ] **Step 3: Implement.** `ClickTargetScript` and `FocusScript` return a `facts` object built by
  one shared JS helper `__islandFacts(el)` (tag, type, autocomplete, name, id, aria-label, trimmed
  `innerText`/`value` up to 80 chars, `in_form: !!el.form || !!el.closest('form')`, `is_submit`,
  `page_origin: location.origin`). `page_type` without a locator and `page_press_key` read
  `document.activeElement` facts through one `Runtime.evaluate` before acting. A private
  `BrowserToolbox::Gate(GateInput, std::function<void()> run, ToolCallback done)` calls `Classify`
  with `host_.TrustedSites()`; kFree runs; kBlocked answers
  `"The agent cannot type into password fields."`; kConfirm checks the task grants
  (`std::set<std::pair<std::string host, GateRule>>`), otherwise calls `RequestConfirmation` and
  maps the answer: kAllowOnce/kTrustSite → run; kAllowForTask → add grant, run; kCancel → declined
  error; kTimeout → timeout error. `browser_navigate`/`browser_close_tab` use
  `ListTabs()` to find the tab's URL and `opened_by_agent`.
- [ ] **Step 4: Run** the filter → PASS; `cmake -S src/agent -B build-agent && cmake --build
  build-agent` still builds standalone.
- [ ] **Step 5: Commit** `git commit -m "Ask before consequential agent page actions"`.

### Task 3: Agent-opened tabs and trusted sites persist

**Files:**
- Modify: `src/main/tab.h` (`SetOpenedByAgent(bool)`, `opened_by_agent()`),
  `src/main/session_store.{h,cc}` (`TabState::opened_by_agent`, optional key `"opened_by_agent"`),
  `src/main/prefs_store.{h,cc}` (`PrefsState::agent_trusted_sites`, optional key
  `"agent_trusted_sites"` as a JSON array of strings, normalized with `agent::HostOf`-style rules
  on load: lowercase, no `www.`, deduplicated, empty entries dropped),
  `src/main/window_agent_host.cc` (`OpenTab` marks the new tab, `ListTabs` fills
  `opened_by_agent`, `TrustedSites()` returns the prefs list), `src/main/browser_window_research.cc`
  and `src/main/agent_navigation.cc` (tabs opened for research are marked)
- Test: `tests/tab_test.cpp`, `tests/session_store_test.cpp`, `tests/prefs_store_test.cpp`,
  `tests/window_agent_host_test.cpp`

**Interfaces:**
- Consumes: Task 2 (`AgentTabInfo::opened_by_agent`, `TrustedSites()`).
- Produces: `BrowserWindow::AddTrustedSite(std::string host)`,
  `BrowserWindow::RemoveTrustedSite(std::string_view host)` (save prefs, re-render pages),
  `const std::vector<std::string>& BrowserWindow::trusted_sites() const`.

- [ ] **Step 1: Failing tests:** `TabTest.OpenedByAgentDefaultsFalse`,
  `SessionStoreTest.OpenedByAgentRoundTripsAndOldFilesLoad`,
  `PrefsStoreTest.TrustedSitesRoundTripNormalizedAndDeduplicated` (input
  `["WWW.Example.com", "example.com", "", "Shop.Test"]` loads as `["example.com", "shop.test"]`),
  `WindowAgentHostTest.OpenTabMarksTheTabAsOpenedByAgent`.
- [ ] **Step 2: Run** the four filters → FAIL.
- [ ] **Step 3: Implement** as listed.
- [ ] **Step 4: Run** the filters → PASS.
- [ ] **Step 5: Commit** `git commit -m "Remember agent-opened tabs and trusted sites"`.

### Task 4: The confirmation card

**Files:**
- Modify: `src/main/window_agent_host.{h,cc}` (real `RequestConfirmation`: hop to the UI thread,
  hand the request to the window), `src/main/browser_window.{h,cc}` (pending confirmations,
  timeout, panel state, agent dot, Agent button dot), `src/main/pages/agent.html` (card),
  `src/main/pages/settings.html` (Trusted sites list), `src/main/browser_chrome.{h,cc}`
  (`SetAgentNeedsYou(bool)` draws a 6 DIP accent dot on the Native Agent button's trailing edge
  and on the Arc/Graphite agent row), `src/main/dev_capture.{h,cc}` (action `agent_fixture:confirm`)
- Test: `tests/browser_window_pages_test.cpp`, `tests/window_agent_host_test.cpp`,
  `tests/design/test_local_pages.py`, `tests/dev_capture_test.cpp`

**Interfaces:**
- Consumes: Tasks 2–3.
- Produces: `void BrowserWindow::ShowConfirmation(agent::ConfirmationRequest,
  std::function<void(agent::ConfirmationAnswer)>)`;
  `void BrowserWindow::AnswerConfirmation(std::string_view id, agent::ConfirmationAnswer)`;
  `bool BrowserWindow::HasPendingConfirmation() const`; panel message
  `{type:"confirm", id, answer:"once"|"task"|"trust"|"cancel"}`; panel state key
  `confirmations: [{id, summary, host, trusted_allowed}]`; settings message
  `{type:"remove_trusted_site", host}`; test seam
  `void BrowserWindow::AdvanceConfirmationClockForTest(std::chrono::seconds)`.

- [ ] **Step 1: Failing tests:**
  - `ConfirmationShowsInPanelStateAndMarksTheTabWaiting` (state JSON lists the card; the tab's
    `AgentTabState` is `kWaitingForYou`);
  - `AnsweringTrustAddsTheSiteAndAllows`;
  - `ConfirmationTimesOutAndWithdrawsTheCard` (advance 120 s → callback gets `kTimeout`, the
    state no longer lists the card, the agent dot returns to its previous state);
  - `CardSurvivesClosingAndReopeningThePanel`;
  - `RemoveTrustedSiteMessagePersists`;
  - `test_local_pages.py`: agent.html renders `.confirm-card` with buttons "Allow once",
    "Allow for this task", "Trust this site" (absent when `trusted_allowed` is false), "Cancel";
    Esc posts `cancel`; settings.html has a "Trusted sites" list with Remove buttons and the copy
    "The agent can submit forms and run scripts on these sites without asking. It never types
    passwords."; only ThemeJson variables.
  - `DevCaptureTest.ParsesAgentFixtureConfirm`.
- [ ] **Step 2: Run** the filters and the pytest file → FAIL.
- [ ] **Step 3: Implement.** The window keeps `std::vector<PendingConfirmation>` (request,
  callback, deadline = now + 120 s, tab id). A 1 s UI-thread timer only while the list is non-empty
  fires timeouts. Answers call the callback exactly once and remove the entry; window close answers
  every pending entry `kCancel`. The card is rendered from state like the existing Permission card
  (`permissionNode` in agent.html) and placed above the composer.
- [ ] **Step 4: Run** the filters and pytest → PASS. Take dev captures
  `--island-dev-actions=theme:light,toggle_agent,agent_fixture:confirm` and `theme:dark,…` and look
  at both.
- [ ] **Step 5: Commit** `git commit -m "Show agent confirmations in the panel"`.

### Task 5: The task queue

**Files:**
- Create: `src/agent/agent_task_queue.h`, `src/agent/agent_task_queue.cc`
- Modify: `src/agent/CMakeLists.txt`
- Test: `tests/agent/agent_task_queue_test.cpp`

**Interfaces:**
- Produces:

```cpp
namespace island::agent {
enum class TaskKind : std::uint8_t { kChat, kResearch, kCollect, kAsk };
enum class TaskState : std::uint8_t { kQueued, kRunning, kWaitingForYou, kDone, kFailed, kCancelled };
struct AgentTask {
    std::string id;           // "t1", "t2", ...
    TaskKind kind = TaskKind::kChat;
    std::string title;        // first line of prompt, at most 60 chars (UTF-8 safe) + "…"
    std::string prompt;
    std::string context;
    std::int64_t created_ms = 0;
    TaskState state = TaskState::kQueued;
    std::size_t done = 0, total = 0;   // plan progress
};
class AgentTaskQueue {
  public:
    using StartFn = std::function<void(const AgentTask&)>;   // send this task's prompt
    explicit AgentTaskQueue(StartFn start);
    const AgentTask& Enqueue(TaskKind kind, std::string prompt, std::string context,
                             std::int64_t now_ms);
    bool MoveUp(std::string_view id);
    bool MoveDown(std::string_view id);
    bool Remove(std::string_view id);              // queued only
    void StopCurrent();                            // running/waiting → kCancelled, then advance
    void OnTurnFinished(bool ok);                  // running → kDone/kFailed, then advance
    void OnWaiting(bool waiting);                  // running ↔ kWaitingForYou; waiting blocks advance
    void OnProgress(std::size_t done, std::size_t total);
    void ClearFinished();
    [[nodiscard]] const AgentTask* current() const;
    [[nodiscard]] const std::vector<AgentTask>& tasks() const;  // display order
    [[nodiscard]] std::size_t queued_count() const;
};
}  // namespace island::agent
```

- [ ] **Step 1: Failing tests:** `EnqueueStartsWhenIdle`, `SecondTaskWaitsForTheFirst`,
  `MoveUpAndDownReorderQueuedOnly`, `RemoveRefusesTheRunningTask`, `StopAdvancesToTheNext`,
  `WaitingBlocksAdvanceUntilAnswered`, `FailedTurnStillAdvances`, `ClearFinishedKeepsLiveTasks`,
  `TitlesClipOnUtf8Boundaries` (a 70-char Cyrillic prompt gets a 60-codepoint title plus "…"),
  `ProgressTracksTheCurrentTask`. Each asserts the exact sequence of `StartFn` calls.
- [ ] **Step 2: Run** `--gtest_filter='AgentTaskQueue*'` → FAIL.
- [ ] **Step 3: Implement**; `StartFn` is called synchronously from `Enqueue`/`OnTurnFinished`/
  `StopCurrent`/`OnWaiting(false)` when the queue advances.
- [ ] **Step 4: Run** → PASS.
- [ ] **Step 5: Commit** `git commit -m "Add the agent task queue"`.

### Task 6: Route agent work through the queue

**Files:**
- Modify: `src/main/browser_window.{h,cc}` (`SubmitAgentPrompt` and the panel's send path enqueue
  `kChat`; ACP turn end → `OnTurnFinished`; plan updates → `OnProgress`; pending confirmations and
  "Waiting for approval" research → `OnWaiting`; each start calls `toolbox().ResetTaskGrants()`),
  `src/main/browser_window_research.cc` (research enqueues `kResearch`),
  `src/main/pages/agent.html` (Tasks strip, "Queued · #n" under a queued message),
  `src/main/browser_chrome.cc` (Agent button text `Agent · 3/7` / `Agent · 2 queued`),
  `src/main/browser_window.cc` palette command `{.id = "agent_tasks", .title = "Agent tasks"}`
- Test: `tests/browser_window_pages_test.cpp`, `tests/browser_window_test.cpp`,
  `tests/design/test_local_pages.py`

**Interfaces:**
- Consumes: Task 5 (`AgentTaskQueue`), Task 4 (`HasPendingConfirmation`, `AnswerConfirmation`).
- Produces: panel messages `{type:"task_move", id, dir:"up"|"down"}`, `{type:"task_remove", id}`,
  `{type:"task_stop"}`, `{type:"tasks_clear_finished"}`; panel state key `tasks: [{id, kind,
  title, state, done, total}]`; `std::string AgentButtonLabel(const AgentTaskQueue&)` (free
  function in `browser_chrome.cc`, unit-tested).

- [ ] **Step 1: Failing tests:** `ChatWhileATaskRunsIsQueued`, `ResearchEntersTheQueue`,
  `StoppingAWaitingTaskDeclinesItsCardAndAdvances` (a pending confirmation's callback gets
  `kCancel` exactly once, the next task starts), `ClosingThePanelKeepsTheTaskRunning`,
  `AgentButtonLabel` cases (`"Agent"`, `"Agent · 3/7"`, `"Agent · 2 queued"`),
  `PaletteListsAgentTasks`; pytest: Tasks strip markup with ↑ ↓ ✕ and Stop buttons that post the
  messages above, "Clear finished".
- [ ] **Step 2: Run** → FAIL.
- [ ] **Step 3: Implement.** `AgentSession::Send` stays the transport; the queue's `StartFn`
  calls it. A task that cannot be sent (no agent configured) ends as kFailed with the session's
  error shown in the panel.
- [ ] **Step 4: Run** → PASS; captures: panel with one running and two queued tasks (dev action
  `agent_fixture:queue`), light and dark.
- [ ] **Step 5: Commit** `git commit -m "Run agent work from a visible task queue"`.

### Task 7: Ask the agent from context

**Files:**
- Modify: `src/main/browser_window.{h,cc}` (`OnBeforeContextMenu` adds, after "Open in peek",
  `MENU_ID_USER_FIRST + 1` "Ask agent about selection" and `+ 2` "Explain" when
  `params->GetSelectionText()` is non-empty, `+ 3` "Summarize linked page" for links; all hidden
  while `ResolvedAgentCommand()` is empty; `OnContextMenuCommand` handles them), palette commands
  `summarize_page` "Summarize page", `ask_about_page` "Ask about page…", ⌘J opening the panel with
  the page chip, `src/main/pages/agent.html` (context chip with ✕, quote chip for selections)
- Create: `src/main/agent_context.{h,cc}` (CEF-free): `std::string BuildAgentContext(std::string_view
  url, std::string_view title, std::string_view selection)` — lines `Page: <title>`, `URL: <url>`,
  then `Selection:` and the selection clipped to 4096 bytes on a UTF-8 boundary with `…`
- Test: `tests/agent_context_test.cpp`, `tests/browser_window_test.cpp`,
  `tests/design/test_local_pages.py`

**Interfaces:**
- Consumes: Task 6 (enqueue `kAsk`).
- Produces: panel state key `context_chip: {title, url, selection}`; panel message
  `{type:"clear_context"}`.

- [ ] **Step 1: Failing tests:** `AgentContextTest.ClipsSelectionOnUtf8Boundary`,
  `AgentContextTest.OmitsEmptySelection`, `ContextMenuHasAgentItemsOnlyWithAnAgent`,
  `ExplainEnqueuesAnAskWithTheSelection` (prompt `"Explain this in plain words."`),
  `SummarizeLinkOpensPeekAndEnqueues` (prompt `"Summarize this page."`, context URL = link),
  `PaletteSummarizePageEnqueuesWithPageContext`; pytest: chip markup, ✕ posts `clear_context`.
- [ ] **Step 2: Run** → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** → PASS; capture: panel with a page chip and a quote chip, light and dark.
- [ ] **Step 5: Commit** `git commit -m "Ask the agent about a selection, a link or the page"`.

### Task 8: Collect from tabs and export tables

**Files:**
- Create: `src/main/collect_prompt.{h,cc}`, `src/main/table_export.{h,cc}`
- Modify: `src/main/report_store.{h,cc}` (`ResearchReport::kind`, schema version 2 reading v1 as
  `"research"`), `src/main/browser_window.cc` (palette `collect_from_tabs` "Collect from tabs…",
  panel sheet messages, finishing a `kCollect` task stores `kind="collection"` and opens a Report
  tab through the same path Research uses), `src/main/pages/agent.html` (Collect sheet),
  `src/main/pages/report.html` (Export menu), the Reports list in the palette and in
  `pages/report.html` (a table icon for `kind="collection"`), `src/main/browser_window_research.cc` (export
  message → `RunFileDialog` save in `download_path.h`'s directory)
- Test: `tests/collect_prompt_test.cpp`, `tests/table_export_test.cpp`,
  `tests/report_store_test.cpp`, `tests/browser_window_pages_test.cpp`,
  `tests/design/test_local_pages.py`

**Interfaces:**
- Consumes: Task 6 (enqueue `kCollect`), Research report markers from `research_prompt.h`.
- Produces:

```cpp
struct CollectTab { std::size_t index; std::string title; std::string url; };
[[nodiscard]] std::string BuildCollectPrompt(std::string_view what,
                                             const std::vector<CollectTab>& tabs);
struct Table { std::vector<std::string> header; std::vector<std::vector<std::string>> rows; };
[[nodiscard]] std::optional<Table> FirstMarkdownTable(std::string_view markdown);
[[nodiscard]] std::string TableToCsv(const Table& table);    // UTF-8 BOM, CRLF, RFC 4180
[[nodiscard]] std::string TableToJson(const Table& table);   // array of objects
```

  Panel messages `{type:"collect", what, tabs:[index…]}`; report page message
  `{type:"export", id, format:"csv"|"json"|"md"}`; report state key `has_table`.
- [ ] **Step 1: Failing tests:**

```cpp
TEST(TableExportTest, CsvQuotesPipesNewlinesAndKeepsUtf8) {
    auto t = FirstMarkdownTable(
        "intro\n\n| Source | Цена | Note |\n|---|---|---|\n"
        "| [Shop](https://a.test) | 1 299 ₽ | says \"new\", a\\|b |\n");
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(TableToCsv(*t),
              "\xEF\xBB\xBFSource,Source title,Цена,Note\r\n"
              "https://a.test,Shop,1 299 ₽,\"says \"\"new\"\", a|b\"\r\n");
}
TEST(TableExportTest, JsonIsAnArrayOfObjects) {
    auto t = FirstMarkdownTable("| a | b |\n|---|---|\n| 1 | 2 |\n| 3 |\n");
    ASSERT_TRUE(t.has_value());
    EXPECT_EQ(TableToJson(*t), R"([{"a":"1","b":"2"},{"a":"3","b":""}])");
}
TEST(TableExportTest, NoTableIsNullopt) {
    EXPECT_FALSE(FirstMarkdownTable("just text | with a pipe").has_value());
}
```

  Link cells: a cell that is exactly one Markdown link becomes its URL, and when any row's link
  text differs from its URL, a `<column> title` column follows with the link texts (as in the CSV
  test). Plus `CollectPromptTest.ListsTabsAndAsksForOneTableBetweenMarkers`,
  `ReportStoreTest.KindRoundTripsAndV1LoadsAsResearch`, `CollectTaskOpensACollectionReport`,
  `ExportMessageWritesTheChosenFormat` (test seam replaces the dialog with a path); pytest: Export
  menu with CSV/JSON/Markdown, CSV/JSON disabled with tooltip "No table in this report" when
  `has_table` is false; Collect sheet with tab checklist (non-pinned checked) and the "What to
  collect" field with placeholder "price, rating and delivery time for each product".
- [ ] **Step 2: Run** → FAIL.
- [ ] **Step 3: Implement.**
- [ ] **Step 4: Run** → PASS; captures: Collect sheet; a collection report with the Export menu
  open (dev action `report_fixture:collection`), light and dark.
- [ ] **Step 5: Commit** `git commit -m "Collect data from tabs into an exportable table"`.

### Task 9: Docs

**Files:**
- Modify: `CHANGELOG.md` (check each task's entries read as one coherent Added section),
  `AGENTS.md` ("Current state": agent actions line), `src/agent/AGENTS.md` (gate, queue),
  `src/main/AGENTS.md` (card, context asks, collect/export), `DESIGN.md` (confirmation card and
  Tasks strip in the agent panel section)
- Test: `python3 -m pytest tests/design -q` (doc contract tests)

- [ ] **Step 1:** Update the docs; describe the gate rules in one table in `src/agent/AGENTS.md`.
- [ ] **Step 2:** Run the design pytest → PASS.
- [ ] **Step 3: Commit** `git commit -m "Document agent actions"`.
