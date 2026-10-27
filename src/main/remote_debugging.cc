#include "remote_debugging.h"

namespace island {
namespace {

std::optional<int> g_active_port;

}  // namespace

RemoteDebuggingSwitchPlan PlanRemoteDebuggingSwitches(std::optional<int> port) {
    RemoteDebuggingSwitchPlan plan;
    if (!port.has_value()) {
        plan.remove = {"remote-debugging-port", "remote-debugging-pipe"};
        return plan;
    }
    plan.append.emplace_back("remote-allow-origins", "http://127.0.0.1:" + std::to_string(*port));
    return plan;
}

void SetActiveRemoteDebuggingPort(std::optional<int> port) { g_active_port = port; }

std::optional<int> ActiveRemoteDebuggingPort() { return g_active_port; }

}  // namespace island
