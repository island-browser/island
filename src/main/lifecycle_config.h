#ifndef ISLAND_MAIN_LIFECYCLE_CONFIG_H_
#define ISLAND_MAIN_LIFECYCLE_CONFIG_H_

#include <string_view>

namespace island {

inline constexpr std::string_view kWindowTitle = "Island";
inline constexpr int kWindowWidth = 1024;
inline constexpr int kWindowHeight = 768;
inline constexpr int kRemoteDebuggingPort = 9222;

}  // namespace island

#endif
