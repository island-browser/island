#ifndef ISLAND_ADDRESS_INDICATORS_H_
#define ISLAND_ADDRESS_INDICATORS_H_

#include <cstdint>
#include <string>

namespace island {

// What the Native address field says about the page: the blocked-request shield, Reader and the
// agent dot (spec §3). CEF-free so the rules are unit-tested.

enum class AgentTabState : std::uint8_t { kIdle, kReading, kWaitingForYou };

struct AddressIndicators {
    bool show_shield = false;
    std::string shield_label;    // "14", "9999+"
    std::string shield_tooltip;  // "14 trackers blocked"
    bool show_reader = false;
    AgentTabState agent = AgentTabState::kIdle;

    bool operator==(const AddressIndicators&) const = default;
};

inline constexpr std::uint64_t kShieldLabelCap = 9999;

[[nodiscard]] inline AddressIndicators ComputeAddressIndicators(std::uint64_t blocked,
                                                                bool reader_available,
                                                                AgentTabState agent) {
    AddressIndicators indicators;
    indicators.show_reader = reader_available;
    indicators.agent = agent;
    if (blocked == 0) {
        return indicators;
    }
    indicators.show_shield = true;
    indicators.shield_label =
        blocked > kShieldLabelCap ? std::to_string(kShieldLabelCap) + "+" : std::to_string(blocked);
    indicators.shield_tooltip =
        std::to_string(blocked) + (blocked == 1 ? " tracker blocked" : " trackers blocked");
    return indicators;
}

}  // namespace island

#endif  // ISLAND_ADDRESS_INDICATORS_H_
