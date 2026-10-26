#include "dev_capture.h"

#include <array>
#include <charconv>
#include <cstdlib>
#include <optional>
#include <string>
#include <utility>

#include "space_icon.h"

namespace island {
namespace {

constexpr std::string_view kActionsFlag = "--island-dev-actions=";
constexpr std::string_view kCaptureFlag = "--island-dev-capture=";
constexpr std::string_view kDelayFlag = "--island-dev-delay=";
constexpr std::string_view kKeepOpenFlag = "--island-dev-keep-open";
constexpr double kMaxDelaySeconds = 60.0;

std::vector<std::string> SplitActions(std::string_view list) {
    std::vector<std::string> actions;
    while (!list.empty()) {
        const std::size_t comma = list.find(',');
        const std::string_view action = list.substr(0, comma);
        if (!action.empty()) {
            actions.emplace_back(action);
        }
        if (comma == std::string_view::npos) {
            break;
        }
        list.remove_prefix(comma + 1);
    }
    return actions;
}

}  // namespace

DevCaptureOptions DevCaptureOptions::Parse(std::span<const std::string_view> arguments) {
    DevCaptureOptions options;
    for (const std::string_view argument : arguments) {
        if (argument.starts_with(kActionsFlag)) {
            options.actions = SplitActions(argument.substr(kActionsFlag.size()));
        } else if (argument.starts_with(kCaptureFlag)) {
            options.capture_path = std::string(argument.substr(kCaptureFlag.size()));
        } else if (argument.starts_with(kDelayFlag)) {
            const std::string value(argument.substr(kDelayFlag.size()));
            char* end = nullptr;
            const double seconds = std::strtod(value.c_str(), &end);
            if (end != value.c_str() && *end == '\0' && seconds >= 0.0 &&
                seconds <= kMaxDelaySeconds) {
                options.delay_seconds = seconds;
            }
        } else if (argument == kKeepOpenFlag) {
            options.keep_open = true;
        }
    }
    return options;
}

std::string_view DevActionSelectorName(std::string_view action) {
    static constexpr std::array<std::pair<std::string_view, std::string_view>, 19> kActions = {{
        {"welcome", "showWelcome:"},
        {"theme:system", "useSystemTheme:"},
        {"theme:light", "useLightTheme:"},
        {"theme:dark", "useDarkTheme:"},
        {"command_palette", "openCommandPalette:"},
        {"search_palette", "openSearchPalette:"},
        {"focus_address", "focusAddress:"},
        {"toggle_sidebar", "toggleSidebar:"},
        {"toggle_agent", "toggleAgentPanel:"},
        {"settings", "openSettings:"},
        {"tab_overview", "openTabOverview:"},
        {"new_tab", "newTab:"},
        {"next_tab", "selectNextTab:"},
        {"previous_tab", "selectPreviousTab:"},
        {"toggle_split", "toggleSplit:"},
        {"new_space", "newSpace:"},
        {"rename_space", "beginSpaceRenaming:"},
        {"space_menu", "openSpaceMenu:"},
        {"cheatsheet", "showCheatsheet:"},
    }};
    for (const auto& [name, selector] : kActions) {
        if (action == name) {
            return selector;
        }
    }
    return {};
}

std::string_view DevSettingsSectionName(std::string_view action) {
    constexpr std::string_view kPrefix = "settings:";
    if (!action.starts_with(kPrefix)) {
        return {};
    }
    return action.substr(kPrefix.size());
}

std::string_view DevAgentFixtureName(std::string_view action) {
    constexpr std::string_view kPrefix = "agent_fixture:";
    if (!action.starts_with(kPrefix)) {
        return {};
    }
    return action.substr(kPrefix.size());
}

std::optional<DevSpaceIconAction> ParseDevSpaceIconAction(std::string_view action) {
    constexpr std::string_view kPrefix = "space_icon:";
    if (!action.starts_with(kPrefix)) {
        return std::nullopt;
    }
    const std::string_view value = action.substr(kPrefix.size());
    const std::size_t equals = value.find('=');
    if (equals == std::string_view::npos || equals == 0 || equals + 1 >= value.size()) {
        return std::nullopt;
    }
    std::size_t index = 0;
    const char* const begin = value.data();
    const char* const end = value.data() + equals;
    const auto [parsed, error] = std::from_chars(begin, end, index);
    if (error != std::errc() || parsed != end) {
        return std::nullopt;
    }
    const std::string_view icon = value.substr(equals + 1);
    if (icon == "none") {
        return DevSpaceIconAction{.index = index, .icon_id = ""};
    }
    if (!IsKnownSpaceIconId(icon)) {
        return std::nullopt;
    }
    return DevSpaceIconAction{.index = index, .icon_id = std::string(icon)};
}

}  // namespace island
