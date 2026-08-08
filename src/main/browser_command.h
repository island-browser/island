#ifndef ISLAND_BROWSER_COMMAND_H_
#define ISLAND_BROWSER_COMMAND_H_

#include <cstdint>

namespace island {

enum class BrowserCommand : std::uint8_t {
    kBack,
    kForward,
    kReload,
};

}

#endif
