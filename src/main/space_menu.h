#ifndef ISLAND_SPACE_MENU_H_
#define ISLAND_SPACE_MENU_H_

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "command_palette.h"
#include "design_tokens.h"

namespace island {

// The per-space colour presets, shared by New Space (cycled in creation order) and the space
// menu's colour rows. Hex strings use the "#RRGGBB" form SetSpaceColorFromHex accepts.
struct SpaceMenuColorChoice {
    std::string_view name;
    std::string_view hex;
    ArgbColor color;
};

inline constexpr std::array<SpaceMenuColorChoice, 5> kSpaceMenuColorChoices = {{
    {.name = "Blue", .hex = "#0090FF", .color = {.argb = 0xFF0090FFU}},
    {.name = "Green", .hex = "#30A46C", .color = {.argb = 0xFF30A46CU}},
    {.name = "Amber", .hex = "#FFB224", .color = {.argb = 0xFFFFB224U}},
    {.name = "Red", .hex = "#E5484D", .color = {.argb = 0xFFE5484DU}},
    {.name = "Violet", .hex = "#8E4EC6", .color = {.argb = 0xFF8E4EC6U}},
}};

// What a row of the space menu asks for. The menu acts on the active space, the only space whose
// menu can open (see BrowserChrome: a click on the active segment opens it).
enum class SpaceMenuAction : std::uint8_t {
    kRename,
    kChangeIcon,
    kSetColor,
    kNewSpace,
    kDeleteSpace,
};

struct SpaceMenuCommand {
    SpaceMenuAction action = SpaceMenuAction::kRename;
    // The "#RRGGBB" value of a kSetColor row; empty otherwise.
    std::string hex;
};

// The palette rows of the space menu, in order: Rename, Change icon, the colour presets, New
// space, Delete space. Row ids carry the `space_menu:` prefix so the window can tell them from
// other palette commands.
[[nodiscard]] std::vector<PaletteCommandEntry> SpaceMenuPaletteCommands(bool can_delete);

// The action a menu row stands for, or nullopt when `command_id` is not a space-menu row.
[[nodiscard]] std::optional<SpaceMenuCommand> ParseSpaceMenuCommandId(std::string_view command_id);

}  // namespace island

#endif  // ISLAND_SPACE_MENU_H_
