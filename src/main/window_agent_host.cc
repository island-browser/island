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
    const Space& space = window_->spaces()[window_->active_space_index()];
    for (std::size_t index = 0; index < space.tab_count(); ++index) {
        const Tab& tab = space.tabs()[index];
        const NavigationSnapshot& nav = tab.navigation_state().snapshot();
        tabs.push_back({.index = index,
                        .title = nav.page_title.empty() ? std::string("New Tab") : nav.page_title,
                        .url = nav.url.empty() ? tab.startup_url() : nav.url,
                        .active = space.has_active_tab() && index == space.active_tab_index(),
                        .loading = nav.load_phase == LoadPhase::kLoading,
                        .pinned = tab.pinned()});
    }
    return tabs;
}

agent::HostStatus WindowAgentHost::OpenTab(std::string_view url) {
    return StatusFor(window_->OpenNewTab(url), "Could not open that address.");
}

agent::HostStatus WindowAgentHost::Navigate(std::optional<std::size_t> tab, std::string_view url) {
    return StatusFor(window_->NavigateTab(tab, url),
                     "Could not navigate: unknown tab or an address Island does not allow.");
}

agent::HostStatus WindowAgentHost::ActivateTab(std::size_t tab) {
    return StatusFor(window_->SelectActiveSpaceTabIndex(tab),
                     "No tab at index " + std::to_string(tab) + ".");
}

agent::HostStatus WindowAgentHost::CloseTab(std::size_t tab) {
    return StatusFor(window_->CloseActiveSpaceTabIndex(tab),
                     "No tab at index " + std::to_string(tab) + ".");
}

agent::HostStatus WindowAgentHost::SwitchSpace(std::size_t space) {
    return StatusFor(window_->SelectSpaceIndex(space),
                     "No space at index " + std::to_string(space) + ".");
}

agent::HostStatus WindowAgentHost::NewSpace(std::string_view name) {
    return StatusFor(window_->CreateSpace(name), "Could not create a space.");
}

agent::HostStatus WindowAgentHost::RunAction(agent::AgentBrowserAction action) {
    switch (action) {
        case agent::AgentBrowserAction::kBack:
            window_->ExecuteCommand(BrowserCommand::kBack);
            break;
        case agent::AgentBrowserAction::kForward:
            window_->ExecuteCommand(BrowserCommand::kForward);
            break;
        case agent::AgentBrowserAction::kReload:
            window_->ExecuteCommand(BrowserCommand::kReload);
            break;
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
    CefRefPtr<CefBrowser> browser = window_->BrowserForTab(tab);
    if (browser == nullptr) {
        agent::DevToolsReply reply;
        reply.ok = false;
        reply.error = tab ? "Tab " + std::to_string(*tab) +
                                " has no live page. Activate it with browser_activate_tab first."
                          : std::string("The active tab has no live page yet.");
        done(std::move(reply));
        return;
    }
    devtools_.Call(browser, method, std::move(params), std::move(done));
}

}  // namespace island
