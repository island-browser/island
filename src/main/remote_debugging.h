#ifndef ISLAND_MAIN_REMOTE_DEBUGGING_H_
#define ISLAND_MAIN_REMOTE_DEBUGGING_H_

#include <optional>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace island {

// The Chrome DevTools Protocol port is closed unless a developer asks for it with
// --island-dev-remote-debugging[=PORT]. Chromium also reads --remote-debugging-port and
// --remote-debugging-pipe from the command line, so the browser process strips both when no
// port was requested.
struct RemoteDebuggingSwitchPlan {
    std::vector<std::string> remove;
    std::vector<std::pair<std::string, std::string>> append;
};

[[nodiscard]] RemoteDebuggingSwitchPlan PlanRemoteDebuggingSwitches(std::optional<int> port);

// Applies the plan to a command line (CefCommandLine in the app; a fake in unit tests, because
// island_tests does not load the CEF library).
template <typename CommandLine>
void ApplyRemoteDebuggingPlan(const RemoteDebuggingSwitchPlan& plan, CommandLine& command_line) {
    for (const std::string& name : plan.remove) {
        command_line.RemoveSwitch(name);
    }
    for (const auto& [name, value] : plan.append) {
        command_line.AppendSwitchWithValue(name, value);
    }
}

// Process-wide, set once in RunIslandMainProcess; read by the About page.
void SetActiveRemoteDebuggingPort(std::optional<int> port);
[[nodiscard]] std::optional<int> ActiveRemoteDebuggingPort();

inline constexpr std::string_view kRemoteDebuggingWarningFormat =
    "WARNING: remote debugging is on at 127.0.0.1:%d; any local process can control your tabs";

}  // namespace island

#endif  // ISLAND_MAIN_REMOTE_DEBUGGING_H_
