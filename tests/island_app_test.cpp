#include "island_app.h"

#include <gtest/gtest.h>

#include <map>
#include <set>
#include <string>
#include <vector>

#include "remote_debugging.h"
#include "startup_options.h"

namespace island {
namespace {

// island_tests does not load the CEF library, so CefCommandLine cannot be created here; the real
// IslandApp::OnBeforeCommandLineProcessing path is covered by the launch checks in the report.
struct FakeCommandLine {
    std::set<std::string> switches{"remote-debugging-port", "remote-debugging-pipe",
                                   "use-mock-keychain"};
    std::map<std::string, std::string> values;
    void RemoveSwitch(const std::string& name) { switches.erase(name); }
    void AppendSwitchWithValue(const std::string& name, const std::string& value) {
        switches.insert(name);
        values[name] = value;
    }
};

TEST(RemoteDebuggingPlan, RemovesBothSwitchesWithoutAPortAndAllowsOneOriginWithAPort) {
    const RemoteDebuggingSwitchPlan off = PlanRemoteDebuggingSwitches(std::nullopt);
    EXPECT_EQ(off.remove,
              (std::vector<std::string>{"remote-debugging-port", "remote-debugging-pipe"}));
    EXPECT_TRUE(off.append.empty());
    const RemoteDebuggingSwitchPlan on = PlanRemoteDebuggingSwitches(9333);
    EXPECT_TRUE(on.remove.empty());
    ASSERT_EQ(on.append.size(), 1u);
    EXPECT_EQ(on.append[0].first, "remote-allow-origins");
    EXPECT_EQ(on.append[0].second, "http://127.0.0.1:9333");
}

TEST(IslandAppTest, StripsRemoteDebuggingSwitchesWithoutDevFlag) {
    FakeCommandLine command_line;
    ApplyRemoteDebuggingPlan(PlanRemoteDebuggingSwitches(std::nullopt), command_line);
    EXPECT_EQ(command_line.switches.count("remote-debugging-port"), 0u);
    EXPECT_EQ(command_line.switches.count("remote-debugging-pipe"), 0u);
    EXPECT_EQ(command_line.switches.count("use-mock-keychain"), 1u);
}

TEST(IslandAppTest, AllowsOnlyTheLoopbackOriginWithDevFlag) {
    FakeCommandLine command_line;
    ApplyRemoteDebuggingPlan(PlanRemoteDebuggingSwitches(9333), command_line);
    EXPECT_EQ(command_line.values["remote-allow-origins"], "http://127.0.0.1:9333");
    EXPECT_EQ(command_line.switches.count("remote-debugging-port"), 1u);
}

}  // namespace
}  // namespace island
