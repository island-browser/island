// Research mode and the Reports list: the BrowserWindow half that connects the
// start page, the agent session, the report store and the Report tab. The pure
// parts (prompt, extraction, outcome, store) live in research_prompt.* and
// report_store.*.
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
    while (end > begin && (text[end - 1] == ' ' || text[end - 1] == '\t' ||
                           text[end - 1] == '\n' || text[end - 1] == '\r')) {
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
    if (research_.has_value()) {
        return {ResearchStart::kBusy, "A research task is already running."};
    }
    if (agent_session_->busy()) {
        return {ResearchStart::kBusy,
                "The agent is busy with another task. Try again when it finishes."};
    }
    // ISLAND_AGENT_COMMAND pins the command, so its availability is not ours to judge.
    const agent::ResolvedAgentCommand resolved = ResolvedAgent();
    if (resolved.source != agent::AgentCommandSource::kEnvironment) {
        for (const agent::AgentProviderStatus& status : ProviderStatuses()) {
            if (status.provider == nullptr || status.provider->id != resolved.provider_id || status.available) {
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

    ResearchTask task;
    task.question = asked;
    task.space_name = ResearchSpaceName(asked);
    task.turns_ended_before = agent_session_->transcript().turns_ended();
    // The page context would only distract the researcher: it works from the question.
    if (!agent_session_->Send(BuildResearchPrompt(asked), {})) {
        const std::string message = agent_session_->error().empty()
                                        ? "The agent could not be started. Check Settings."
                                        : agent_session_->error();
        research_status_.question = asked;
        SetResearchStatus("failed", message);
        return {ResearchStart::kFailed, message};
    }
    research_ = std::move(task);
    research_status_.question = asked;
    SetResearchStatus("running", {});
    SetAgentPanelOpen(true);
    return {ResearchStart::kStarted, {}};
}

void BrowserWindow::CancelResearch() {
    CEF_REQUIRE_UI_THREAD();
    if (!research_.has_value() || agent_session_ == nullptr) {
        return;
    }
    agent_session_->Cancel();
}

void BrowserWindow::OnAgentSessionChanged() {
    ScheduleLocalPagesRender();
    RefreshAddressIndicators();
    if (!research_.has_value() || research_finish_posted_ || closing_ ||
        agent_session_ == nullptr || agent_session_->busy()) {
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
    if (closing_ || !research_.has_value() || agent_session_ == nullptr ||
        agent_session_->busy()) {
        return;
    }
    CompleteResearch(ClassifyResearchTurn(agent_session_->transcript(),
                                          research_->turns_ended_before,
                                          agent_session_->error()));
}

void BrowserWindow::CompleteResearch(const ResearchOutcome& outcome) {
    CEF_REQUIRE_UI_THREAD();
    if (!research_.has_value()) {
        return;
    }
    const ResearchTask task = std::move(*research_);
    research_.reset();
    research_finish_posted_ = false;
    research_status_.question = task.question;
    switch (outcome.kind) {
        case ResearchOutcome::Kind::kCancelled:
            SetResearchStatus("cancelled", outcome.message);
            return;
        case ResearchOutcome::Kind::kFailed:
            SetResearchStatus("failed", outcome.message);
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
    OpenReportTab(id, task.space_name);
    RenderStartPages();
    ScheduleLocalPagesRender();
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
    CEF_REQUIRE_UI_THREAD();
    if (closing_ || ReportStore::Find(reports_, id) == nullptr) {
        return false;
    }
    if (!space_name.empty()) {
        // The newest space with that name is the one the agent just created.
        for (std::size_t index = spaces_.size(); index > 0; --index) {
            if (spaces_[index - 1].name() == space_name) {
                static_cast<void>(SelectSpaceIndex(index - 1));
                break;
            }
        }
    }
    AppendTabToActiveSpace(ReportPageUrlFor(id));
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
    const bool in_conversation = report_in_session_ == report.id &&
                                 !agent_session_->transcript().items().empty();
    if (in_conversation) {
        return;
    }
    if (agent_session_->busy()) {
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
