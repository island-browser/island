#include "address_indicators.h"

#include <gtest/gtest.h>

namespace island {
namespace {

TEST(AddressIndicatorsTest, HidesTheShieldAtZero) {
    EXPECT_FALSE(ComputeAddressIndicators(0, false, AgentTabState::kIdle).show_shield);
}

TEST(AddressIndicatorsTest, ShowsTheCountAndATooltipAboveZero) {
    const AddressIndicators a = ComputeAddressIndicators(14, false, AgentTabState::kIdle);
    EXPECT_TRUE(a.show_shield);
    EXPECT_EQ(a.shield_label, "14");
    EXPECT_EQ(a.shield_tooltip, "14 trackers blocked");
}

TEST(AddressIndicatorsTest, CapsHugeCounts) {
    const AddressIndicators a = ComputeAddressIndicators(10000, false, AgentTabState::kIdle);
    EXPECT_EQ(a.shield_label, "9999+");
    EXPECT_EQ(a.shield_tooltip, "10000 trackers blocked");
    EXPECT_EQ(ComputeAddressIndicators(9999, false, AgentTabState::kIdle).shield_label, "9999");
}

TEST(AddressIndicatorsTest, SingularTooltip) {
    EXPECT_EQ(ComputeAddressIndicators(1, false, AgentTabState::kIdle).shield_tooltip,
              "1 tracker blocked");
}

TEST(AddressIndicatorsTest, ReaderAndAgentPassThrough) {
    const AddressIndicators a = ComputeAddressIndicators(0, true, AgentTabState::kWaitingForYou);
    EXPECT_TRUE(a.show_reader);
    EXPECT_EQ(a.agent, AgentTabState::kWaitingForYou);
}

}  // namespace
}  // namespace island
