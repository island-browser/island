#ifndef ISLAND_MAIN_LIFECYCLE_CONFIG_H_
#define ISLAND_MAIN_LIFECYCLE_CONFIG_H_

#include <array>
#include <string_view>

namespace island {

inline constexpr std::string_view kWindowTitle = "Island";
inline constexpr int kWindowWidth = 1024;
inline constexpr int kWindowHeight = 768;
inline constexpr int kRemoteDebuggingPort = 9222;

// Chromium switches appended to the browser process's command line.
//
// use-mock-keychain (macOS): Chromium keeps the key that encrypts its stored
// data in the login keychain as "Chromium Safe Storage", and reaching it raises
// system dialogs ("Keychain Not Found" when no keychain is available, an access
// prompt otherwise). Island keeps its CEF state under a /tmp cache root and does
// not persist session cookies (app_runtime.cc), so it uses Chromium's
// in-process mock keychain and never touches the user's keychain.
#if defined(__APPLE__)
inline constexpr std::array<std::string_view, 1> kBrowserProcessSwitches = {"use-mock-keychain"};
#else
inline constexpr std::array<std::string_view, 0> kBrowserProcessSwitches = {};
#endif

}  // namespace island

#endif
