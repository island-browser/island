// Research mode and the Reports list: the BrowserWindow half that connects the
// start page, the agent session, the report store and the Report tab. The pure
// parts (prompt, extraction, outcome, store) live in research_prompt.* and
// report_store.*.
#include <algorithm>
#include <array>
#include <chrono>
#include <fstream>
#include <string>
#include <system_error>
#include <utility>

// The CEF view types must be complete before browser_window.h, whose delegate
// bases hold CefRefPtr<CefWindow>/<CefPanel>/<CefOverlayController> by value.
#include "browser_window.h"
#include "download_path.h"
#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_browser.h"
#include "include/cef_frame.h"
#include "include/cef_parser.h"
#include "include/cef_task.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"
#include "plan_dev_fixture.h"
#include "plan_graph.h"

namespace island {
namespace {

std::int64_t NowMs() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::system_clock::now().time_since_epoch())
        .count();
}

std::string Trimmed(std::string_view text) {
    std::size_t begin = 0;
    std::size_t end = text.size();
    while (begin < end && (text[begin] == ' ' || text[begin] == '\t' || text[begin] == '\n' ||
                           text[begin] == '\r')) {
        ++begin;
    }
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' || text[end - 1] == '\n' ||
                           text[end - 1] == '\r')) {
        --end;
    }
    return std::string(text.substr(begin, end - begin));
}

// A JSON string literal, safe to splice into a script.
std::string JsString(std::string_view text) {
    return json::Serialize(json::Value::String(std::string(text)));
}

// Runs a script in the main frame of `browser`, if it still has one.
void RunInMainFrame(const CefRefPtr<CefBrowser>& browser, const std::string& script,
                    const std::string& url) {
    const CefRefPtr<CefFrame> frame = browser != nullptr ? browser->GetMainFrame() : nullptr;
    if (frame != nullptr) {
        frame->ExecuteJavaScript(script, url, 0);
    }
}

// Clips to at most `limit` bytes without splitting a UTF-8 sequence.
std::string ClipPlanText(std::string text, std::size_t limit) {
    if (text.size() <= limit) return text;
    std::size_t end = limit;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U) --end;
    text.resize(end);
    return text;
}

// Whether the plan has a node `id`, with the ids the graph and the edits use.
bool HasPlanId(const std::vector<agent::AcpPlanEntry>& plan, std::string_view id) {
    const std::vector<std::string> ids = agent::PlanEntryIds(plan);
    return std::find(ids.begin(), ids.end(), std::string(id)) != ids.end();
}

// The plan node's depth (branch points it is nested in), or -1 when it is not in the graph.
int PlanNodeDepth(const std::vector<agent::AcpPlanEntry>& plan,
                  const std::set<std::string>& skipped, std::string_view id) {
    for (const agent::PlanNode& node : agent::BuildPlanGraph(plan, skipped).nodes) {
        if (node.id == id) return node.depth;
    }
    return -1;
}

std::string ResultScript(std::string_view kind, std::string_view text) {
    return "window.islandReportResult&&window.islandReportResult(" + JsString(kind) + "," +
           JsString(text) + ")";
}

// Writes the saved report once the native save dialog is dismissed, then tells
// the page how it went.
class SaveReportCallback final : public CefRunFileDialogCallback {
  public:
    SaveReportCallback(CefRefPtr<CefBrowser> browser, std::string markdown, std::string page_url)
        : browser_(std::move(browser)),
          markdown_(std::move(markdown)),
          page_url_(std::move(page_url)) {}

    void OnFileDialogDismissed(const std::vector<CefString>& file_paths) override {
        CEF_REQUIRE_UI_THREAD();
        if (file_paths.empty()) {
            RunInMainFrame(browser_, ResultScript("cancelled", ""), page_url_);
            return;
        }
        std::filesystem::path path(file_paths.front().ToString());
        if (path.extension().empty()) {
            path += ".md";
        }
        std::ofstream out(path, std::ios::binary | std::ios::trunc);
        if (out.is_open()) {
            out << markdown_;
            out.flush();
        }
        if (!out) {
            RunInMainFrame(browser_, ResultScript("error", "Could not write that file."),
                           page_url_);
            return;
        }
        RunInMainFrame(browser_, ResultScript("saved", path.filename().string()), page_url_);
    }

  private:
    CefRefPtr<CefBrowser> browser_;
    std::string markdown_;
    std::string page_url_;

    IMPLEMENT_REFCOUNTING(SaveReportCallback);
};

}  // namespace

// --- Research -------------------------------------------------------------------

void BrowserWindow::SetResearchStatus(std::string state, std::string message) {
    research_status_.seq += 1;
    research_status_.state = std::move(state);
    research_status_.message = std::move(message);
    if (research_status_.state != "done") {
        research_status_.report_id.clear();
        research_status_.report_title.clear();
    }
    RenderStartPages();
}

BrowserWindow::ResearchStartResult BrowserWindow::StartResearch(std::string_view question) {
    CEF_REQUIRE_UI_THREAD();
    const std::string asked = Trimmed(question);
    if (closing_ || asked.empty()) {
        return {ResearchStart::kFailed, "Ask a question to research."};
    }
    EnsureAgentSession();
    // A research the queue does not know (a plan continuation outside it) blocks a new one;
    // queued research waits behind the queue's own live task instead.
    if (research_.has_value() && agent_queue_.current() == nullptr) {
        return {ResearchStart::kBusy, "A research task is already running."};
    }
    // ISLAND_AGENT_COMMAND pins the command, so its availability is not ours to judge.
    const agent::ResolvedAgentCommand resolved = ResolvedAgent();
    if (resolved.source != agent::AgentCommandSource::kEnvironment) {
        for (const agent::AgentProviderStatus& status : ProviderStatuses()) {
            if (status.provider == nullptr || status.provider->id != resolved.provider_id ||
                status.available) {
                continue;
            }
            std::string message;
            if (resolved.provider_id == agent::kCustomAgentProviderId) {
                message = Trimmed(status.command).empty()
                              ? "No agent command is set. Set one in Settings to use Research."
                              : "The custom agent command was not found. Check it in Settings.";
            } else {
                message = std::string(status.provider->name) +
                          " was not found. Install it or choose another agent in Settings.";
            }
            research_status_.question = asked;
            SetResearchStatus("not_installed", message);
            return {ResearchStart::kNotInstalled, message};
        }
    }

    // The question is the task's title and prompt; the research prompt is built when it starts.
    const agent::AgentTask queued = agent_queue_.Enqueue(agent::TaskKind::kResearch, asked, asked,
                                                         static_cast<std::int64_t>(NowMs()));
    ScheduleLocalPagesRender();
    switch (queued.state) {
        case agent::TaskState::kQueued:
            return {ResearchStart::kQueued, "Queued: it starts when the agent finishes its task."};
        case agent::TaskState::kFailed:
            return {ResearchStart::kFailed, start_error_};
        default:
            return {ResearchStart::kStarted, {}};
    }
}

bool BrowserWindow::StartResearchTask(const agent::AgentTask& queued) {
    const std::string& asked = queued.context;
    ResearchTask task;
    task.question = asked;
    task.space_name = ResearchSpaceName(asked);
    task.turns_ended_before = agent_session_->transcript().turns_ended();
    task.task_id = queued.id;
    // The page context would only distract the researcher: it works from the question.
    if (!SendToAgent(BuildResearchPrompt(asked))) {
        start_error_ = agent_session_->error().empty()
                           ? "The agent could not be started. Check Settings."
                           : agent_session_->error();
        research_status_.question = asked;
        SetResearchStatus("failed", start_error_);
        return false;
    }
    research_ = std::move(task);
    research_status_.question = asked;
    plan_entries_.clear();
    plan_edits_ = {};
    plan_selected_.clear();
    plan_node_tabs_.clear();
    plan_view_.reset();
    plan_notice_.clear();
    // Nothing is approved until a Run goes out; only that clears it.
    plan_unapproved_ = true;
    SetResearchStatus("running", {});
    OpenResearchSpace(research_->space_name);
    OpenPlanTab();
    SetAgentPanelOpen(true);
    return true;
}

void BrowserWindow::OpenResearchSpace(const std::string& name) {
    // The newest space with that name is the research's own (a repeated question reuses it).
    for (std::size_t index = spaces_.size(); index > 0; --index) {
        if (spaces_[index - 1].name() != name) continue;
        // Change the model first and attach once: switching the space and then selecting or
        // appending the Plan tab would swap the content view twice in one UI task.
        Space& reused = spaces_[index - 1];
        std::optional<std::size_t> plan_at;
        for (std::size_t tab = 0; tab < reused.tab_count(); ++tab) {
            const Tab& candidate = reused.tabs()[tab];
            const std::string& current = candidate.navigation_state().snapshot().url;
            if ((current.empty() ? candidate.startup_url() : current) == PlanPageUrl()) {
                plan_at = tab;
                break;
            }
        }
        if (plan_at.has_value()) {
            SelectTabWithOneAttach(index - 1, *plan_at);
            return;
        }
        DetachActiveTabObservers();
        active_space_index_ = index - 1;
        Tab plan{NextTabId()};
        plan.SetStartupUrl(PlanPageUrl());
        reused.AppendTab(std::move(plan));
        AttachActiveTabObservers();
        return;
    }
    // A new space starts with the Plan hub as its only tab, so the view is swapped once and no
    // blank tab with a browser of its own is left behind.
    DetachActiveTabObservers();
    const std::size_t created_index = spaces_.size();
    Space& created = AppendSpace(name);
    Tab plan{NextTabId()};
    plan.SetStartupUrl(PlanPageUrl());
    created.AppendTab(std::move(plan));
    active_space_index_ = created_index;
    AttachActiveTabObservers();
}

void BrowserWindow::CancelResearch() {
    CEF_REQUIRE_UI_THREAD();
    if (!research_.has_value()) {
        return;
    }
    if (research_->awaiting_approval) {
        // Nothing runs while the plan waits, so there is no turn to cancel: the queue moves on
        // at once.
        const std::string id = research_->task_id;
        research_.reset();
        SetResearchStatus("cancelled", "Research cancelled.");
        const agent::AgentTask* live = agent_queue_.current();
        if (!id.empty() && live != nullptr && live->id == id) {
            StopAgentTask();
        }
        return;
    }
    if (agent_session_ == nullptr) {
        return;
    }
    StopAgentTask();
}

void BrowserWindow::EndResearchTask(const std::string& task_id, bool ok) {
    if (task_id.empty()) {
        return;
    }
    if (in_flight_id_ == task_id) {
        in_flight_id_.clear();
    }
    agent_queue_.OnTurnFinished(task_id, ok);
    ScheduleLocalPagesRender();
}

void BrowserWindow::OnAgentSessionChanged() {
    ScheduleLocalPagesRender();
    RefreshAddressIndicators();
    RefreshPlanFromTranscript();
    RenderPlanPages();
    ReportQueueProgress();
    CheckInFlightTurn();
    if (!research_.has_value() || research_->awaiting_approval || research_finish_posted_ ||
        closing_ || agent_session_ == nullptr || agent_session_->busy()) {
        return;
    }
    if (chrome_ == nullptr) {
        // Headless (tests): no message loop to post to.
        FinishResearch();
        return;
    }
    // The session is inside its own event handler here: publish from a posted
    // task so opening tabs never re-enters it.
    research_finish_posted_ = true;
    CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(&BrowserWindow::FinishResearch,
                                                            CefRefPtr<BrowserWindow>(this))));
}

void BrowserWindow::FinishResearch() {
    research_finish_posted_ = false;
    if (closing_ || !research_.has_value() || research_->awaiting_approval ||
        agent_session_ == nullptr || agent_session_->busy()) {
        return;
    }
    CompleteResearch(ClassifyResearchTurn(agent_session_->transcript(),
                                          research_->turns_ended_before, agent_session_->error(),
                                          research_->plan_approved));
}

void BrowserWindow::CompleteResearch(const ResearchOutcome& outcome) {
    CEF_REQUIRE_UI_THREAD();
    if (!research_.has_value()) {
        return;
    }
    if (outcome.kind == ResearchOutcome::Kind::kAwaitingApproval) {
        // The research stays in flight: the plan is shown and the next turn, started by
        // Run, is the one that may report.
        research_->awaiting_approval = true;
        research_->plan_approved = false;
        plan_unapproved_ = true;
        research_->turns_ended_before = TurnsEnded();
        research_finish_posted_ = false;
        SetResearchStatus("awaiting_approval", outcome.message);
        RenderPlanPages();
        // The task waits for the person (no turn end is reported: the Run that follows is the
        // same task's turn).
        SyncQueueWaiting();
        return;
    }
    const ResearchTask task = std::move(*research_);
    research_.reset();
    research_finish_posted_ = false;
    research_status_.question = task.question;
    switch (outcome.kind) {
        case ResearchOutcome::Kind::kCancelled:
            SetResearchStatus("cancelled", outcome.message);
            EndResearchTask(task.task_id, false);
            return;
        case ResearchOutcome::Kind::kFailed:
            SetResearchStatus("failed", outcome.message);
            EndResearchTask(task.task_id, false);
            return;
        case ResearchOutcome::Kind::kAwaitingApproval:  // handled above
            return;
        case ResearchOutcome::Kind::kReport:
            break;
    }
    ResearchReport report;
    report.title = ReportTitle(outcome.markdown, task.question);
    report.question = task.question;
    report.markdown = outcome.markdown;
    report.created_ms = NowMs();
    const std::string id = ReportStore::Add(reports_, std::move(report));
    SaveReports();
    report_in_session_ = id;
    research_status_.report_id = id;
    research_status_.report_title = ReportStore::Find(reports_, id)->title;
    research_status_.seq += 1;
    research_status_.state = "done";
    research_status_.message.clear();
    OpenReportTabImpl(id, task.space_name, /*group_with_plan=*/true);
    RenderStartPages();
    ScheduleLocalPagesRender();
    EndResearchTask(task.task_id, true);
}

// --- Plan tab -------------------------------------------------------------------

namespace {
constexpr std::size_t kMaxPlanInstructionBytes = 1000;
}  // namespace

bool BrowserWindow::SendToAgent(std::string text, std::string context) {
    if (agent_send_for_test_) {
        return agent_send_for_test_(std::move(text));
    }
    return agent_session_ != nullptr && agent_session_->Send(std::move(text), std::move(context));
}

std::uint64_t BrowserWindow::TurnsEnded() const {
    return agent_session_ != nullptr ? agent_session_->transcript().turns_ended() : 0;
}

void BrowserWindow::SendPlanTurn(std::string text, bool approves_plan, bool continues_run) {
    CEF_REQUIRE_UI_THREAD();
    // A plan edit needs the plan to wait for approval; a continuation of the run (retrying a
    // failed step) does not, and works while the run is going or after it finished.
    if (!continues_run && (!research_.has_value() || !research_->awaiting_approval)) {
        plan_notice_ = "The agent can change the plan only while it waits for your approval.";
        RenderPlanPages();
        return;
    }
    if (!agent_send_for_test_ && (agent_session_ == nullptr || agent_session_->busy())) {
        plan_notice_ = "The agent is busy. Try again when it finishes.";
        RenderPlanPages();
        return;
    }
    // A finished run has no task any more; the continuation starts a new one for the same plan.
    const bool started_task = !research_.has_value();
    if (started_task) {
        research_ = ResearchTask{.question = research_status_.question,
                                 .space_name = {},
                                 .turns_ended_before = TurnsEnded()};
    }
    // The turn this sends is the one the research waits for. A continuation keeps the approval
    // it had; a new task for a finished run follows steps that could only fail once approved.
    research_->awaiting_approval = false;
    if (!continues_run) {
        research_->plan_approved = approves_plan;
    } else if (started_task) {
        research_->plan_approved = true;
    }
    research_->turns_ended_before = TurnsEnded();
    if (!SendToAgent(std::move(text))) {
        // Refused (the session is busy or cannot start): the plan keeps waiting.
        if (started_task) {
            research_.reset();
        } else if (!continues_run) {
            research_->awaiting_approval = true;
            research_->plan_approved = false;
        }
        plan_notice_ = agent_session_ != nullptr && !agent_session_->error().empty()
                           ? agent_session_->error()
                           : "The agent could not take the plan.";
        RenderPlanPages();
        return;
    }
    plan_notice_.clear();
    if (!continues_run && approves_plan) {
        plan_unapproved_ = false;
    }
    SyncQueueWaiting();  // the plan no longer waits: the task is running again
    SetResearchStatus("running", {});
    RenderPlanPages();
}

std::string BrowserWindow::PlanView() const {
    if (plan_view_.has_value()) {
        return *plan_view_;
    }
    const bool any_island =
        std::any_of(plan_entries_.begin(), plan_entries_.end(),
                    [](const agent::AcpPlanEntry& entry) { return entry.island.has_value(); });
    return any_island ? "graph" : "list";
}

const std::string& BrowserWindow::PlanPageUrl() const {
    if (plan_page_url_.empty()) {
        plan_page_url_ = LocalPage::PageUrl(LocalPageKind::kPlan);
    }
    return plan_page_url_;
}

bool BrowserWindow::IsPlanPageUrl(std::string_view url) const { return url == PlanPageUrl(); }

std::string BrowserWindow::PlanStateJson() const {
    using json::Value;
    const agent::PlanEdits edits = agent::CarryEditsOver(plan_edits_, plan_entries_);
    const agent::PlanGraph graph = agent::BuildPlanGraph(plan_entries_, edits.skipped);
    Value skipped = Value::MakeArray();
    for (const std::string& id : edits.skipped) {
        skipped.Push(Value::String(id));
    }
    Value instructions = Value::MakeObject();
    for (const auto& [id, text] : edits.instructions) {
        instructions.Set(id, Value::String(text));
    }
    const bool awaiting = research_.has_value() && research_->awaiting_approval;
    Value graph_value = json::Parse(agent::PlanGraphJson(graph)).value_or(Value::Null());
    return json::Serialize(
        Value::MakeObject()
            .Set("theme", ThemeJson())
            .Set("graph", std::move(graph_value))
            .Set("edits", Value::MakeObject()
                              .Set("skipped", std::move(skipped))
                              .Set("instructions", std::move(instructions)))
            .Set("status", Value::MakeObject()
                               .Set("state", Value::String(research_status_.state))
                               .Set("message", Value::String(research_status_.message)))
            .Set("awaiting", Value::Bool(awaiting))
            .Set("running", Value::Bool(research_.has_value() && !awaiting))
            .Set("selected", Value::String(plan_selected_))
            .Set("view", Value::String(PlanView()))
            .Set("notice", Value::String(plan_notice_))
            .Set("depth_limit", Value::Int(agent::kMaxBranchDepth))
            .Set("question", Value::String(research_status_.question)));
}

void BrowserWindow::RenderPlanFrame(CefRefPtr<CefFrame> frame) const {
    if (frame == nullptr) {
        return;
    }
    frame->ExecuteJavaScript(LocalPage::RenderScript(PlanStateJson()), frame->GetURL().ToString(),
                             0);
}

void BrowserWindow::RenderPlanPages() {
    if (closing_) {
        return;
    }
    for (const Space& space : spaces_) {
        for (const Tab& tab : space.tabs()) {
            const CefRefPtr<CefBrowser> browser = tab.browser();
            const CefRefPtr<CefFrame> frame =
                browser != nullptr ? browser->GetMainFrame() : nullptr;
            if (frame != nullptr && IsPlanPageUrl(frame->GetURL().ToString())) {
                RenderPlanFrame(frame);
            }
        }
    }
}

std::optional<std::pair<std::size_t, std::size_t>> BrowserWindow::TabLocation(TabId id) const {
    for (std::size_t space_index = 0; space_index < spaces_.size(); ++space_index) {
        const Space& space = spaces_[space_index];
        for (std::size_t tab_index = 0; tab_index < space.tab_count(); ++tab_index) {
            if (space.tabs()[tab_index].id() == id) return std::pair{space_index, tab_index};
        }
    }
    return std::nullopt;
}

std::optional<agent::PlanNode> BrowserWindow::RunningPlanNode() const {
    const agent::PlanGraph graph = agent::BuildPlanGraph(plan_entries_, plan_edits_.skipped);
    for (const agent::PlanNode& node : graph.nodes) {
        if (node.state == agent::PlanNodeState::kRunning) {
            return node;
        }
    }
    return std::nullopt;
}

TabId BrowserWindow::AppendPlanChildTab(std::string startup_url, std::string node_id,
                                        std::string label, bool activate) {
    const std::optional<std::pair<std::size_t, std::size_t>> location =
        plan_tab_.has_value() ? TabLocation(*plan_tab_) : std::nullopt;
    if (!location.has_value() || location->first != active_space_index_) {
        // The agent tools address the active space, so a Plan hub elsewhere gets no group: the
        // page opens as an ordinary tab and the view stays where the person left it.
        AppendTabToActiveSpace(std::move(startup_url), /*opened_by_agent=*/true);
        return active_tab() != nullptr ? active_tab()->id() : TabId{};
    }
    return AppendChildOfPlan(location->first, std::move(startup_url), std::move(node_id),
                             std::move(label), activate, /*opened_by_agent=*/true);
}

TabId BrowserWindow::AppendChildOfPlan(std::size_t space_index, std::string startup_url,
                                       std::string node_id, std::string label, bool activate,
                                       bool opened_by_agent) {
    Space& space = spaces_[space_index];
    Tab tab{NextTabId()};
    const TabId id = tab.id();
    tab.SetStartupUrl(std::move(startup_url));
    tab.SetGroupLabel(std::move(label));
    // A research page is one the agent opened for the running plan. The Report is not: it is
    // the person's result, so the agent may not navigate or close it without asking.
    tab.SetOpenedByAgent(opened_by_agent);
    if (activate) {
        // The model changes first (space, then selection) and the view is attached once; the
        // browser view is created while the tab is attached (EnsureTabBrowserView).
        DetachActiveTabObservers();
        active_space_index_ = space_index;
        static_cast<void>(space.AppendChildTab(*plan_tab_, std::move(tab), true));
        AttachActiveTabObservers();
    } else {
        // A background page keeps the selection and the content view: its browser is created
        // when the tab is first shown, so the view tree is not touched.
        static_cast<void>(space.AppendChildTab(*plan_tab_, std::move(tab), false));
        UpdateChromeCollections();
    }
    if (!node_id.empty()) {
        plan_node_tabs_[std::move(node_id)] = id;
    }
    return id;
}

bool BrowserWindow::OpenAgentTab(std::string_view text) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return false;
    }
    std::optional<std::string> url = ResolveNewTabTarget(text);
    if (!url.has_value()) {
        return false;
    }
    if (!research_.has_value() || !plan_tab_.has_value()) {
        AppendTabToActiveSpace(std::move(*url), /*opened_by_agent=*/true);
        return true;
    }
    const std::optional<agent::PlanNode> node = RunningPlanNode();
    const std::string label = node.has_value()
                                  ? (node->branch.empty() ? "Step" : "Branch " + node->branch)
                                  : std::string();
    // The page opens active and the model follows at once. The content view swaps in when the
    // previously attached page's browser exists (AttachedBrowserStillCreating), so pages opened
    // in quick succession are loaded one after the other and the ones the agent has already moved
    // past are loaded only when shown (page tools then answer "no live page").
    static_cast<void>(AppendPlanChildTab(std::move(*url), node.has_value() ? node->id : "", label,
                                         /*activate=*/true));
    return true;
}

void BrowserWindow::OpenPlanTab() {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    // One Plan tab at a time: the tracked one stays (a restored session may hold others),
    // and the other Plan tabs are closed as stale.
    std::vector<TabId> plan_tabs;
    for (const Space& space : spaces_) {
        for (const Tab& tab : space.tabs()) {
            const std::string& current = tab.navigation_state().snapshot().url;
            const std::string_view shown = current.empty() ? tab.startup_url() : current;
            if (shown == PlanPageUrl()) plan_tabs.push_back(tab.id());
        }
    }
    // The hub lives in the research space: a Plan tab elsewhere (an earlier research) is stale.
    const auto in_active_space = [this](TabId id) {
        const std::optional<std::pair<std::size_t, std::size_t>> location = TabLocation(id);
        return location.has_value() && location->first == active_space_index_;
    };
    std::optional<TabId> keep;
    if (plan_tab_.has_value() &&
        std::find(plan_tabs.begin(), plan_tabs.end(), *plan_tab_) != plan_tabs.end() &&
        in_active_space(*plan_tab_)) {
        keep = plan_tab_;
    } else {
        for (const TabId candidate : plan_tabs) {
            if (in_active_space(candidate)) {
                keep = candidate;
                break;
            }
        }
    }
    for (const TabId stale : plan_tabs) {
        if (keep.has_value() && stale == *keep) continue;
        if (const auto location = TabLocation(stale)) {
            RemoveStalePlanTabAt(location->first, location->second);
        }
    }
    if (keep.has_value()) {
        if (const auto location = TabLocation(*keep)) {
            static_cast<void>(ActivateTabAt(location->first, location->second));
        }
    } else {
        AppendTabToActiveSpace(PlanPageUrl());
    }
    // Pinning and moving may reorder the active space, so the tab is found by its id.
    const TabId id = keep.has_value() ? *keep : active_tab()->id();
    plan_tab_ = id;
    const auto index_of_plan = [this, id]() -> std::optional<std::size_t> {
        const Space& space = active_space();
        for (std::size_t index = 0; index < space.tab_count(); ++index) {
            if (space.tabs()[index].id() == id) return index;
        }
        return std::nullopt;
    };
    if (const std::optional<std::size_t> index = index_of_plan()) {
        static_cast<void>(SetActiveSpaceTabPinned(*index, true));
    }
    if (const std::optional<std::size_t> index = index_of_plan()) {
        static_cast<void>(MoveTabWithinSpace(active_space_index(), *index, 0));
    }
    RenderPlanPages();
    HideInternalPage();
}

bool BrowserWindow::ConfirmPlanTabClose(TabId id) {
    if (closing_plan_after_confirmation_ || !research_.has_value() || !plan_tab_.has_value() ||
        id != *plan_tab_) {
        return true;
    }
    constexpr std::string_view kTitle = "Stop the research?";
    constexpr std::string_view kStop = "Stop";
    constexpr std::string_view kKeepRunning = "Keep running";
    if (plan_close_confirmation_for_test_) {
        const bool stop = plan_close_confirmation_for_test_(kTitle, kStop, kKeepRunning);
        if (stop) {
            CancelResearch();
        }
        return stop;
    }
    // The confirmation lives in the Plan page: without a live page there is nothing to ask,
    // so closing the tab stops the research instead of doing nothing.
    const std::optional<std::pair<std::size_t, std::size_t>> location = TabLocation(id);
    const Tab* plan =
        location.has_value() ? &spaces_[location->first].tabs()[location->second] : nullptr;
    if (plan == nullptr || plan->browser() == nullptr ||
        plan->browser()->GetMainFrame() == nullptr) {
        CancelResearch();
        return true;
    }
    ShowPlanCloseConfirmation();
    return false;
}

void BrowserWindow::ShowPlanCloseConfirmation() {
    if (!plan_tab_.has_value()) {
        return;
    }
    const std::optional<std::pair<std::size_t, std::size_t>> location = TabLocation(*plan_tab_);
    if (!location.has_value()) {
        return;
    }
    static_cast<void>(ActivateTabAt(location->first, location->second));
    const Tab* tab = active_tab();
    RunInMainFrame(tab != nullptr ? tab->browser() : nullptr,
                   "window.islandConfirmResearchClose&&window.islandConfirmResearchClose()",
                   PlanPageUrl());
}

void BrowserWindow::CompletePlanCloseConfirmation(bool stop) {
    if (!stop || !plan_tab_.has_value()) {
        return;
    }
    const TabId plan = *plan_tab_;
    CancelResearch();
    const std::optional<std::pair<std::size_t, std::size_t>> location = TabLocation(plan);
    if (!location.has_value()) {
        return;
    }
    closing_plan_after_confirmation_ = true;
    static_cast<void>(CloseTabAt(location->first, location->second));
    closing_plan_after_confirmation_ = false;
}

void BrowserWindow::ForgetPlanTabMapping(TabId id) {
    for (auto it = plan_node_tabs_.begin(); it != plan_node_tabs_.end();) {
        if (it->second == id) {
            it = plan_node_tabs_.erase(it);
        } else {
            ++it;
        }
    }
    if (plan_tab_.has_value() && *plan_tab_ == id) {
        plan_tab_.reset();
        plan_node_tabs_.clear();
    }
}

void BrowserWindow::RemoveStalePlanTabAt(std::size_t space_index, std::size_t tab_index) {
    if (space_index >= spaces_.size() || tab_index >= spaces_[space_index].tab_count()) {
        return;
    }
    Space& space = spaces_[space_index];
    const TabId stale = space.tabs()[tab_index].id();
    const bool active_space = space_index == active_space_index_;
    const bool active_tab = active_space && space.active_tab_id() == stale;
    if (active_tab) {
        DetachActiveTabObservers();
    }
    if (space.tab_count() == 1) {
        // Session restore can leave a duplicate Plan document as the only tab of an inactive
        // space. Keep the user's space and replace only that internal document.
        space.AppendTab(Tab{NextTabId()});
    }
    std::optional<Tab> removed = space.RemoveTab(stale);
    ForgetPlanTabMapping(stale);
    if (active_tab) {
        AttachActiveTabObservers();
    }
    if (removed.has_value() && removed->browser() != nullptr) {
        removed->browser()->GetHost()->CloseBrowser(true);
    }
    UpdateChromeCollections();
}

void BrowserWindow::OpenPlanFixtureTab(bool include_child_tabs) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    plan_entries_ = DevPlanFixture();
    plan_edits_ = {};
    plan_selected_.clear();
    plan_node_tabs_.clear();
    plan_view_.reset();
    plan_notice_.clear();
    if (!research_.has_value()) {
        research_ = ResearchTask{.question = "Sample plan",
                                 .space_name = {},
                                 .turns_ended_before = TurnsEnded(),
                                 .awaiting_approval = true};
    }
    research_status_.question = "Sample plan";
    plan_unapproved_ = true;
    SetResearchStatus("awaiting_approval", "Waiting for approval");
    OpenPlanTab();
    if (include_child_tabs && plan_tab_.has_value()) {
        // Background children: the Plan tab, activated once by OpenPlanTab, stays selected.
        static_cast<void>(AppendPlanChildTab("https://developer.apple.com/", "a", "Branch A",
                                             /*activate=*/false));
        static_cast<void>(AppendPlanChildTab("https://support.apple.com/", "b", "Branch B",
                                             /*activate=*/false));
    }
}

void BrowserWindow::OpenAgentPagesInQuickSuccessionForDev(int count, int gap_ms) {
    CEF_REQUIRE_UI_THREAD();
    if (gap_ms == 0) {
        // Every page queued at once.
        for (int i = 0; i < count; ++i) {
            OpenAgentPageStepForDev(i, i + 1, 0);
        }
        return;
    }
    OpenAgentPageStepForDev(0, count, gap_ms);
}

void BrowserWindow::OpenAgentPageStepForDev(int index, int count, int gap_ms) {
    static constexpr std::array<std::string_view, 4> kPages = {
        "https://example.com/", "https://example.org/", "https://example.net/",
        "https://www.iana.org/"};
    // One UI task per page: what an agent calling browser_open_tab in a row produces. Each task
    // takes the same OpenAgentTab path as an MCP call.
    const std::string address(kPages[static_cast<std::size_t>(index) % kPages.size()]);
    const bool chained = gap_ms != 0;
    CefPostTask(TID_UI,
                CefCreateClosureTask(base::BindOnce(
                    [](CefRefPtr<BrowserWindow> window, std::string address, int index, int count,
                       int gap_ms, bool chained) {
                        static_cast<void>(window->OpenAgentTab(address));
                        if (!chained || index + 1 >= count) {
                            return;
                        }
                        // Serial: the reply is written and the next call arrives afterwards.
                        auto next = base::BindOnce(&BrowserWindow::OpenAgentPageStepForDev, window,
                                                   index + 1, count, gap_ms);
                        if (gap_ms == kSerialAgentPages) {
                            CefPostTask(TID_UI, CefCreateClosureTask(std::move(next)));
                        } else {
                            CefPostDelayedTask(TID_UI, CefCreateClosureTask(std::move(next)),
                                               gap_ms);
                        }
                    },
                    CefRefPtr<BrowserWindow>(this), address, index, count, gap_ms, chained)));
}

void BrowserWindow::RefreshPlanFromTranscript() {
    if (agent_session_ == nullptr) {
        return;
    }
    using Kind = agent::TranscriptItem::Kind;
    const std::vector<agent::TranscriptItem>& items = agent_session_->transcript().items();
    for (auto it = items.rbegin(); it != items.rend(); ++it) {
        if (it->kind != Kind::kPlan) continue;
        plan_entries_ = it->plan;
        plan_edits_ = agent::CarryEditsOver(plan_edits_, plan_entries_);
        if (!plan_selected_.empty() && !HasPlanId(plan_entries_, plan_selected_)) {
            plan_selected_.clear();
        }
        return;
    }
}

void BrowserWindow::HandlePlanPageMessage(const json::Value& message,
                                          CefRefPtr<CefBrowser> source) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    const std::string_view type = message.StringOr("type", "");
    const std::string id(message.StringOr("id", ""));
    if (type == "ready") {
        RenderPlanFrame(source != nullptr ? source->GetMainFrame() : nullptr);
        return;
    }
    if (type == "view") {
        const std::string_view mode = message.StringOr("mode", "");
        if (mode == "graph" || mode == "list") {
            plan_view_ = std::string(mode);
        }
        RenderPlanPages();
        return;
    }
    if (type == "cancel") {
        CancelResearch();
        return;
    }
    if (type == "confirm_close") {
        CompletePlanCloseConfirmation(message.BoolOr("stop", false));
        return;
    }
    if (type == "run" || type == "run_branch") {
        std::optional<std::string> branch;
        if (type == "run_branch") {
            branch = std::string(message.StringOr("branch", ""));
        }
        SendPlanTurn(agent::BuildPlanRunMessage(
                         plan_entries_, agent::CarryEditsOver(plan_edits_, plan_entries_), branch),
                     /*approves_plan=*/true);
        return;
    }
    // Every other message names one node of the plan; an id the plan does not have is ignored.
    if (!HasPlanId(plan_entries_, id)) {
        return;
    }
    if (type == "select") {
        plan_selected_ = id;
        const auto opened = plan_node_tabs_.find(id);
        if (opened != plan_node_tabs_.end()) {
            if (const std::optional<std::pair<std::size_t, std::size_t>> location =
                    TabLocation(opened->second)) {
                static_cast<void>(ActivateTabAt(location->first, location->second));
            } else {
                plan_node_tabs_.erase(opened);
            }
        }
        // plan.html applies selection and inspector state in place so the pressed button
        // survives long enough to receive a second click. No native state changed that another
        // Plan page needs to render.
        return;
    } else if (type == "skip") {
        plan_edits_.skipped.insert(id);
    } else if (type == "restore") {
        plan_edits_.skipped.erase(id);
    } else if (type == "instruction") {
        const std::string text = ClipPlanText(std::string(Trimmed(message.StringOr("text", ""))),
                                              kMaxPlanInstructionBytes);
        if (text.empty()) {
            plan_edits_.instructions.erase(id);
        } else {
            plan_edits_.instructions[id] = text;
        }
    } else if (type == "branch_from") {
        const std::set<std::string> skipped = plan_edits_.skipped;
        if (PlanNodeDepth(plan_entries_, skipped, id) >= agent::kMaxBranchDepth) {
            plan_notice_ =
                "Plans branch at most " + std::to_string(agent::kMaxBranchDepth) + " levels deep";
            RenderPlanPages();
            return;
        }
        SendPlanTurn(agent::BuildBranchFromMessage(id));
        return;
    } else if (type == "expand") {
        SendPlanTurn(agent::BuildExpandMessage(id));
        return;
    } else if (type == "retry") {
        // Only a step that failed can be retried, and never before the plan is approved.
        const agent::PlanGraph graph = agent::BuildPlanGraph(plan_entries_, plan_edits_.skipped);
        const bool failed =
            std::any_of(graph.nodes.begin(), graph.nodes.end(), [&id](const agent::PlanNode& node) {
                return node.id == id && node.state == agent::PlanNodeState::kFailed;
            });
        // A cancelled wait leaves no task, so the plan's own approval state is checked too.
        if (!failed || plan_unapproved_ ||
            (research_.has_value() && research_->awaiting_approval)) {
            return;
        }
        SendPlanTurn(agent::BuildRetryMessage(id), /*approves_plan=*/false,
                     /*continues_run=*/true);
        return;
    }
    RenderPlanPages();
}

// --- Reports --------------------------------------------------------------------

void BrowserWindow::SetReportsFileForTest(std::filesystem::path path) {
    reports_path_ = std::move(path);
    reports_ = ReportStore::Load(reports_path_).state;
}

void BrowserWindow::SaveReports() const {
    if (reports_path_.empty()) {
        return;
    }
    static_cast<void>(ReportStore::Save(reports_path_, reports_));
}

const std::string& BrowserWindow::ReportPageUrl() const {
    if (report_page_url_.empty()) {
        report_page_url_ = LocalPage::PageUrl(LocalPageKind::kReport);
    }
    return report_page_url_;
}

std::string BrowserWindow::ReportPageUrlFor(std::string_view id) const {
    return ReportPageUrl() + "#" + std::string(id);
}

std::optional<std::string> BrowserWindow::ReportIdForUrl(std::string_view url) const {
    const std::string& base = ReportPageUrl();
    if (url.size() <= base.size() + 1 || url.compare(0, base.size(), base) != 0 ||
        url[base.size()] != '#') {
        return std::nullopt;
    }
    const std::string_view id = url.substr(base.size() + 1);
    if (!ReportStore::IsValidId(id)) {
        return std::nullopt;
    }
    return std::string(id);
}

bool BrowserWindow::OpenReportTab(std::string_view id, std::string_view space_name) {
    return OpenReportTabImpl(id, space_name, /*group_with_plan=*/false);
}

bool BrowserWindow::OpenReportTabImpl(std::string_view id, std::string_view space_name,
                                      bool group_with_plan) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || ReportStore::Find(reports_, id) == nullptr) {
        return false;
    }
    const std::optional<std::pair<std::size_t, std::size_t>> plan_location =
        group_with_plan && plan_tab_.has_value() ? TabLocation(*plan_tab_) : std::nullopt;
    if (!plan_location.has_value() && !space_name.empty()) {
        // The newest space with that name is the research space.
        for (std::size_t index = spaces_.size(); index > 0; --index) {
            if (spaces_[index - 1].name() == space_name) {
                // Model only: the append below attaches the view once.
                if (index - 1 != active_space_index_) {
                    DetachActiveTabObservers();
                    active_space_index_ = index - 1;
                }
                break;
            }
        }
    }
    if (group_with_plan && plan_tab_.has_value()) {
        std::string result_node;
        for (const agent::PlanNode& node :
             agent::BuildPlanGraph(plan_entries_, plan_edits_.skipped).nodes) {
            if (node.kind == agent::PlanNodeKind::kResult) {
                result_node = node.id;
                break;
            }
        }
        // The Plan's space becomes the active one in the same step that appends the child, so
        // the view is attached once even when the person is in another space.
        static_cast<void>(AppendChildOfPlan(
            plan_location.has_value() ? plan_location->first : active_space_index_,
            ReportPageUrlFor(id), std::move(result_node), "Result", /*activate=*/true,
            /*opened_by_agent=*/false));
    } else {
        AppendTabToActiveSpace(ReportPageUrlFor(id));
    }
    HideInternalPage();
    return true;
}

bool BrowserWindow::DeleteReport(std::string_view id) {
    CEF_REQUIRE_UI_THREAD();
    if (!ReportStore::Remove(reports_, id)) {
        return false;
    }
    SaveReports();
    if (report_in_session_ == id) {
        report_in_session_.clear();
    }
    if (research_status_.report_id == id) {
        research_status_.report_id.clear();
        research_status_.report_title.clear();
        if (research_status_.state == "done") {
            research_status_.state = "idle";
            research_status_.seq += 1;
        }
    }
    RenderStartPages();
    return true;
}

json::Value BrowserWindow::ReportsListJson() const {
    json::Value list = json::Value::MakeArray();
    for (const ResearchReport& report : reports_.reports) {
        list.Push(json::Value::MakeObject()
                      .Set("id", json::Value::String(report.id))
                      .Set("title", json::Value::String(report.title))
                      .Set("question", json::Value::String(report.question))
                      .Set("created_ms", json::Value::Int(report.created_ms)));
    }
    return list;
}

std::string BrowserWindow::ReportPageStateJson(std::string_view id) const {
    using json::Value;
    Value report = Value::Null();
    if (const ResearchReport* found = ReportStore::Find(reports_, id)) {
        report = Value::MakeObject()
                     .Set("id", Value::String(found->id))
                     .Set("title", Value::String(found->title))
                     .Set("question", Value::String(found->question))
                     .Set("markdown", Value::String(found->markdown))
                     .Set("created_ms", Value::Int(found->created_ms));
    }
    return json::Serialize(
        Value::MakeObject().Set("theme", ThemeJson()).Set("report", std::move(report)));
}

void BrowserWindow::RenderReportFrame(CefRefPtr<CefFrame> frame, std::string_view id) const {
    if (frame == nullptr) {
        return;
    }
    frame->ExecuteJavaScript(LocalPage::RenderScript(ReportPageStateJson(id)),
                             frame->GetURL().ToString(), 0);
}

void BrowserWindow::ReportPageResult(CefRefPtr<CefBrowser> source, std::string_view kind,
                                     std::string_view text) const {
    const CefRefPtr<CefFrame> frame = source != nullptr ? source->GetMainFrame() : nullptr;
    if (frame != nullptr) {
        frame->ExecuteJavaScript(ResultScript(kind, text), frame->GetURL().ToString(), 0);
    }
}

void BrowserWindow::HandleReportPageMessage(const json::Value& message, std::string_view id,
                                            CefRefPtr<CefBrowser> source) {
    CEF_REQUIRE_UI_THREAD();
    if (closing_) {
        return;
    }
    // Only these messages exist; the report is always the one the URL names.
    const std::string_view type = message.StringOr("type", "");
    if (type == "ready") {
        const CefRefPtr<CefFrame> frame = source != nullptr ? source->GetMainFrame() : nullptr;
        RenderReportFrame(frame, id);
        return;
    }
    if (type == "open_url") {
        const std::string_view url = message.StringOr("url", "");
        if (url.rfind("https://", 0) == 0 || url.rfind("http://", 0) == 0) {
            static_cast<void>(OpenNewTab(url));
        }
        return;
    }
    const ResearchReport* report = ReportStore::Find(reports_, id);
    if (report == nullptr) {
        ReportPageResult(source, "error", "This report is no longer in your Reports list.");
        return;
    }
    if (type == "copy_report") {
        if (clipboard_writer_ && clipboard_writer_(report->markdown)) {
            ReportPageResult(source, "copied", "");
        } else {
            ReportPageResult(source, "error", "The clipboard is not available.");
        }
    } else if (type == "save_report") {
        SaveReportToFile(*report, source);
    } else if (type == "continue_in_agent") {
        ContinueReportInAgent(*report, source);
    }
}

void BrowserWindow::SaveReportToFile(const ResearchReport& report, CefRefPtr<CefBrowser> source) {
    if (source == nullptr || source->GetHost() == nullptr) {
        return;
    }
    const std::filesystem::path suggested =
        DefaultDownloadsDirectory() / ReportFileName(report.title);
    const CefRefPtr<CefFrame> frame = source->GetMainFrame();
    source->GetHost()->RunFileDialog(
        FILE_DIALOG_SAVE, "Save report as Markdown", suggested.string(), {".md"},
        new SaveReportCallback(source, report.markdown,
                               frame != nullptr ? frame->GetURL().ToString() : std::string()));
}

void BrowserWindow::ContinueReportInAgent(const ResearchReport& report,
                                          CefRefPtr<CefBrowser> source) {
    EnsureAgentSession();
    SetAgentPanelOpen(true);
    const bool in_conversation =
        report_in_session_ == report.id && !agent_session_->transcript().items().empty();
    if (in_conversation) {
        return;
    }
    // Sent outside the queue, so it waits for the queue to be empty as well as the session.
    if (agent_session_->busy() || agent_queue_.current() != nullptr ||
        agent_queue_.queued_count() > 0) {
        ReportPageResult(source, "error",
                         "The agent is busy. The panel is open; send the report once it is free.");
        return;
    }
    std::string text =
        "We researched a question earlier and this is the finished report. Use it as the "
        "context for my follow-up questions and do not repeat it. Reply with one short "
        "sentence saying you are ready.\n\nQuestion: " +
        report.question + "\n\n" + report.markdown;
    if (agent_session_->Send(std::move(text), {})) {
        report_in_session_ = report.id;
    } else {
        ReportPageResult(source, "error",
                         agent_session_->error().empty() ? "The agent could not be started."
                                                         : agent_session_->error());
    }
    ScheduleLocalPagesRender();
}

}  // namespace island
