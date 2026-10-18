#include "window_agent_host.h"

#include <cstdlib>
#include <string_view>
#include <utility>

// The CEF view types must be complete before browser_window.h, whose delegate
// bases hold CefRefPtr<CefWindow>/<CefPanel>/<CefOverlayController> by value.
#include "browser_window.h"
#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_task.h"
#include "include/views/cef_overlay_controller.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

namespace island {

namespace {

// Island's own pages (start page, reports) are base64 data: documents; the
// agent gets a short stand-in instead of the blob.
std::string AgentVisibleUrl(const std::string& url) {
    return url.rfind("data:", 0) == 0 ? std::string("island://local-page") : url;
}

void RunIfAlive(std::shared_ptr<WindowAgentHost::Guard> guard, std::function<void()> task) {
    CEF_REQUIRE_UI_THREAD();
    if (guard->host != nullptr && task) task();
}

int ConfiguredPort() {
    const char* value = std::getenv("ISLAND_AGENT_PORT");
    if (value == nullptr || *value == '\0') return agent::AgentEndpoint::kDefaultPort;
    const int port = std::atoi(value);
    return port >= 0 && port <= 65535 ? port : agent::AgentEndpoint::kDefaultPort;
}

bool EndpointDisabled() {
    const char* value = std::getenv("ISLAND_AGENT_ENDPOINT");
    return value != nullptr && (std::string_view(value) == "off" || std::string_view(value) == "0");
}

// Names the tab the caller meant: the agent's own page, or the one it gave an index for.
std::string PlanTabError(std::optional<std::size_t> tab) {
    return std::string(tab.has_value() ? "That tab" : "The agent's page") +
           " is the research Plan tab, Island's own page. Open a page with browser_open_tab, or "
           "address a page tab.";
}

// Why an agent tool could not reach its tab. Indices are in the agent's space.
std::string AgentTabErrorText(BrowserWindow::AgentTabError error, std::optional<std::size_t> tab) {
    switch (error) {
        case BrowserWindow::AgentTabError::kNoAgentPage:
            return "The agent has no page yet (no live page to act on). Open one with "
                   "browser_open_tab, or pass a tab index.";
        case BrowserWindow::AgentTabError::kAgentPageClosed:
            return "The agent's page was closed (no live page). Open a new one with "
                   "browser_open_tab, or pass a tab index.";
        case BrowserWindow::AgentTabError::kNoSuchTab:
            return "No tab at index " + std::to_string(tab.value_or(0)) +
                   " in the agent's space (list tabs to see its indices).";
        case BrowserWindow::AgentTabError::kNone:
            break;
    }
    return "The agent's tab is unavailable.";
}

agent::HostStatus StatusFor(bool ok, std::string error) {
    return ok ? agent::HostStatus::Ok() : agent::HostStatus::Error(std::move(error));
}

}  // namespace

WindowAgentHost::WindowAgentHost(BrowserWindow& window)
    : window_(&window), toolbox_(*this), guard_(std::make_shared<Guard>()) {
    guard_->host = this;
}

WindowAgentHost::~WindowAgentHost() { Shutdown(); }

bool WindowAgentHost::StartEndpoint(const std::filesystem::path& discovery_file) {
    CEF_REQUIRE_UI_THREAD();
    if (EndpointDisabled() || guard_->host == nullptr) return false;
    if (endpoint_ != nullptr && endpoint_->running()) return true;
    // Tool calls arrive on HTTP threads; each one is posted to the UI thread
    // and runs only while the host is still alive.
    std::shared_ptr<Guard> guard = guard_;
    endpoint_ =
        std::make_unique<agent::AgentEndpoint>(toolbox_, [guard](std::function<void()> task) {
            CefPostTask(TID_UI,
                        CefCreateClosureTask(base::BindOnce(&RunIfAlive, guard, std::move(task))));
        });
    std::string error;
    if (!endpoint_->Start(ConfiguredPort(), &error) && !endpoint_->Start(0, &error)) {
        LOG(WARNING) << "Island agent endpoint unavailable: " << error;
        endpoint_.reset();
        return false;
    }
    if (!discovery_file.empty() && !endpoint_->WriteDiscoveryFile(discovery_file)) {
        LOG(WARNING) << "Could not write " << discovery_file.string();
    }
    return true;
}

void WindowAgentHost::Shutdown() {
    if (guard_->host == nullptr) return;
    guard_->host = nullptr;
    if (endpoint_ != nullptr) {
        endpoint_->Stop();
        endpoint_.reset();
    }
    devtools_.Shutdown();
}

void WindowAgentHost::OnBrowserClosed(CefRefPtr<CefBrowser> browser) {
    devtools_.ForgetBrowser(browser);
}

std::vector<agent::AgentSpaceInfo> WindowAgentHost::ListSpaces() {
    std::vector<agent::AgentSpaceInfo> spaces;
    const std::vector<Space>& model = window_->spaces();
    for (std::size_t index = 0; index < model.size(); ++index) {
        spaces.push_back({.index = index,
                          .name = model[index].name(),
                          .color_argb = model[index].color().argb,
                          .tab_count = model[index].tab_count(),
                          .active = index == window_->active_space_index()});
    }
    return spaces;
}

std::vector<agent::AgentTabInfo> WindowAgentHost::ListTabs() {
    std::vector<agent::AgentTabInfo> tabs;
    // The agent's space (the Plan's while a research runs), never the space on screen.
    const Space& space = window_->spaces()[window_->AgentSpaceIndex()];
    for (std::size_t index = 0; index < space.tab_count(); ++index) {
        const Tab& tab = space.tabs()[index];
        const NavigationSnapshot& nav = tab.navigation_state().snapshot();
        tabs.push_back({.index = index,
                        .title = tab.DisplayTitle(),
                        .url = AgentVisibleUrl(nav.url.empty() ? tab.startup_url() : nav.url),
                        .active = space.has_active_tab() && index == space.active_tab_index(),
                        .loading = nav.load_phase == LoadPhase::kLoading,
                        .pinned = tab.pinned()});
    }
    return tabs;
}

agent::HostStatus WindowAgentHost::OpenTab(std::string_view url) {
    return StatusFor(window_->OpenAgentTab(url), "Could not open that address.");
}

agent::HostStatus WindowAgentHost::Navigate(std::optional<std::size_t> tab, std::string_view url) {
    const BrowserWindow::AgentTabLookup lookup = window_->ResolveAgentTab(tab);
    if (!lookup.tab.has_value()) {
        return agent::HostStatus::Error(AgentTabErrorText(lookup.error, tab));
    }
    if (window_->IsAgentPlanTab(tab)) {
        return agent::HostStatus::Error(PlanTabError(tab));
    }
    return StatusFor(window_->NavigateAgentTab(tab, url),
                     "Could not navigate: unknown tab or an address Island does not allow.");
}

agent::HostStatus WindowAgentHost::ActivateTab(std::size_t tab) {
    const BrowserWindow::AgentTabLookup lookup = window_->ResolveAgentTab(tab);
    if (!lookup.tab.has_value()) {
        return agent::HostStatus::Error(AgentTabErrorText(lookup.error, tab));
    }
    return StatusFor(window_->ActivateAgentTab(tab), "Could not activate that tab.");
}

agent::HostStatus WindowAgentHost::CloseTab(std::size_t tab) {
    const BrowserWindow::AgentTabLookup lookup = window_->ResolveAgentTab(tab);
    if (!lookup.tab.has_value()) {
        return agent::HostStatus::Error(AgentTabErrorText(lookup.error, tab));
    }
    if (lookup.tab->space != window_->active_space_index()) {
        return agent::HostStatus::Error(
            "The agent's tab " + std::to_string(tab) +
            " is in a space the person is not viewing. Closing it is not allowed from here.");
    }
    return StatusFor(window_->CloseAgentTab(tab), "Could not close that tab.");
}

agent::HostStatus WindowAgentHost::PinTab(std::size_t tab, bool pinned) {
    const BrowserWindow::AgentTabLookup lookup = window_->ResolveAgentTab(tab);
    if (!lookup.tab.has_value()) {
        return agent::HostStatus::Error(AgentTabErrorText(lookup.error, tab));
    }
    return StatusFor(window_->SetAgentTabPinned(tab, pinned),
                     "Could not pin that tab.");
}

agent::HostStatus WindowAgentHost::SwitchSpace(std::size_t space) {
    const bool ok = window_->SelectSpaceIndex(space);
    if (ok) {
        window_->FollowAgentToActiveSpace();
    }
    return StatusFor(ok, "No space at index " + std::to_string(space) + ".");
}

agent::HostStatus WindowAgentHost::NewSpace(std::string_view name) {
    const bool ok = window_->CreateSpace(name);
    if (ok) {
        window_->FollowAgentToActiveSpace();
    }
    return StatusFor(ok, "Could not create a space.");
}

agent::HostStatus WindowAgentHost::RunAction(agent::AgentBrowserAction action) {
    const bool history = action == agent::AgentBrowserAction::kBack ||
                         action == agent::AgentBrowserAction::kForward ||
                         action == agent::AgentBrowserAction::kReload;
    if (history) {
        // History moves the agent's own page, never the page the person is viewing.
        const BrowserWindow::AgentTabLookup lookup = window_->ResolveAgentTab(std::nullopt);
        if (!lookup.tab.has_value()) {
            return agent::HostStatus::Error(AgentTabErrorText(lookup.error, std::nullopt));
        }
        if (window_->IsAgentPlanTab(std::nullopt)) {
            return agent::HostStatus::Error(PlanTabError(std::nullopt));
        }
        CefRefPtr<CefBrowser> browser = window_->AgentBrowser(*lookup.tab);
        if (browser == nullptr) {
            return agent::HostStatus::Error(
                "The agent's page has no live page yet: it is not shown in the window.");
        }
        switch (action) {
            case agent::AgentBrowserAction::kBack:
                browser->GoBack();
                break;
            case agent::AgentBrowserAction::kForward:
                browser->GoForward();
                break;
            case agent::AgentBrowserAction::kReload:
                browser->Reload();
                break;
            default:
                break;
        }
        return agent::HostStatus::Ok();
    }
    switch (action) {
        case agent::AgentBrowserAction::kBack:
        case agent::AgentBrowserAction::kForward:
        case agent::AgentBrowserAction::kReload:
            break;  // handled above, on the agent's own page
        case agent::AgentBrowserAction::kToggleSplit:
            window_->ExecuteCommand(BrowserCommand::kToggleSplit);
            break;
        case agent::AgentBrowserAction::kToggleSidebar:
            window_->ToggleSidebar();
            break;
    }
    return agent::HostStatus::Ok();
}

void WindowAgentHost::DevToolsCall(std::optional<std::size_t> tab, std::string_view method,
                                   json::Value params, agent::DevToolsCallback done) {
    // Every page tool (read, snapshot, click, type, evaluate, screenshot) comes through here.
    // The Plan tab is Island's own page: an agent could press Run on it and approve its own plan.
    const BrowserWindow::AgentTabLookup lookup = window_->ResolveAgentTab(tab);
    if (!lookup.tab.has_value() || window_->IsAgentPlanTab(tab)) {
        agent::DevToolsReply reply;
        reply.ok = false;
        reply.error = lookup.tab.has_value() ? PlanTabError(tab)
                                             : AgentTabErrorText(lookup.error, tab);
        done(std::move(reply));
        return;
    }
    CefRefPtr<CefBrowser> browser = window_->AgentBrowser(*lookup.tab);
    if (browser == nullptr) {
        // Never activates the page: that would move the person's view to the agent's space.
        agent::DevToolsReply reply;
        reply.ok = false;
        reply.error = std::string(tab.has_value() ? "Tab " + std::to_string(*tab)
                                                  : std::string("The agent's page")) +
                      " has no live page: it is not shown in the window yet.";
        done(std::move(reply));
        return;
    }
    devtools_.Call(browser, method, std::move(params), std::move(done));
}

}  // namespace island
