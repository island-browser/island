#ifndef ISLAND_MAIN_RELAUNCH_ARGS_H_
#define ISLAND_MAIN_RELAUNCH_ARGS_H_

#include <span>
#include <string>
#include <vector>

namespace island {

inline constexpr std::size_t kMaxRelaunchUrls = 20;

// The addresses a second Island launch hands to the running one: the non-switch arguments that
// look like `http:`, `https:` or a bare host (with an optional port and path), in order, capped
// at kMaxRelaunchUrls. Switches (`--x`, `-psn_...`) and every other scheme (`file:`) are dropped.
[[nodiscard]] std::vector<std::string> RelaunchUrls(std::span<const std::string> arguments);

}  // namespace island

#endif  // ISLAND_MAIN_RELAUNCH_ARGS_H_
