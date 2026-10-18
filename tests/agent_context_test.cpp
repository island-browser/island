#include "agent_context.h"

#include <gtest/gtest.h>

#include <string>

namespace island {
namespace {

TEST(AgentContextTest, OmitsEmptySelection) {
    EXPECT_EQ(BuildAgentContext("https://example.com/", "Example Domain", ""),
              "Page: Example Domain\nURL: https://example.com/");
    EXPECT_EQ(BuildAgentContext("https://example.com/", "Example Domain", " \n\t"),
              "Page: Example Domain\nURL: https://example.com/");
}

TEST(AgentContextTest, KeepsAShortSelectionWhole) {
    EXPECT_EQ(BuildAgentContext("https://a.test/", "A", "quoted"),
              "Page: A\nURL: https://a.test/\nSelection:\nquoted");
}

TEST(AgentContextTest, ClipsSelectionOnUtf8Boundary) {
    // "é" is two bytes: 4095 ASCII bytes then one "é" straddle the 4096-byte limit.
    const std::string selection = std::string(4095, 'a') + "\xC3\xA9" + "tail";
    const std::string context = BuildAgentContext("u", "t", selection);
    const std::string prefix = "Page: t\nURL: u\nSelection:\n";
    EXPECT_EQ(context, prefix + std::string(4095, 'a') + "\xE2\x80\xA6");
    // Exactly at the limit nothing is cut.
    const std::string exact(kAgentSelectionLimit, 'b');
    EXPECT_EQ(BuildAgentContext("u", "t", exact), prefix + exact);
}

TEST(AgentContextTest, ContextMenuHasAgentItemsOnlyWithAnAgent) {
    EXPECT_TRUE(AgentContextMenuItems(false, true, true).empty());
    EXPECT_TRUE(AgentContextMenuItems(true, false, false).empty());
    const auto selection = AgentContextMenuItems(true, true, false);
    ASSERT_EQ(selection.size(), 2U);
    EXPECT_EQ(selection[0], (AgentMenuItem{kAgentMenuAsk, "Ask agent about selection"}));
    EXPECT_EQ(selection[1], (AgentMenuItem{kAgentMenuExplain, "Explain"}));
    const auto link = AgentContextMenuItems(true, false, true);
    ASSERT_EQ(link.size(), 1U);
    EXPECT_EQ(link[0], (AgentMenuItem{kAgentMenuSummarizeLink, "Summarize linked page"}));
    EXPECT_EQ(AgentContextMenuItems(true, true, true).size(), 3U);
}

}  // namespace
}  // namespace island
