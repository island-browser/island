#include "startup_options.h"

namespace island {

StartupOptions StartupOptions::Parse(std::span<const std::string_view> arguments) {
    for (const std::string_view argument : arguments) {
        if (argument == "--island-smoke-test") {
            return StartupOptions(true);
        }
    }

    return StartupOptions(false);
}

}  // namespace island
