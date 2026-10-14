#include "dev_capture.h"

#include <cstdlib>

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

}  // namespace island
