#include "relaunch_args.h"

#include <gtest/gtest.h>

#include <string>
#include <vector>

namespace island {
namespace {

TEST(RelaunchArgs, ExtractsWebUrlsAndIgnoresSwitches) {
    const std::vector<std::string> arguments = {"--island-smoke-test", "https://a.test/x",
                                                "-psn_0_1", "b.test", "file:///etc/passwd"};
    EXPECT_EQ(RelaunchUrls(arguments), (std::vector<std::string>{"https://a.test/x", "b.test"}));
}

TEST(RelaunchArgs, CapsTheListAtTwentyUrls) {
    std::vector<std::string> arguments;
    for (int index = 0; index < 30; ++index) {
        arguments.push_back("https://a.test/" + std::to_string(index));
    }
    EXPECT_EQ(RelaunchUrls(arguments).size(), 20u);
}

}  // namespace
}  // namespace island
