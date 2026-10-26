#include "space_menu.h"

namespace island {
namespace {

constexpr std::string_view kSpaceMenuPrefix = "space_menu:";
constexpr std::string_view kColorPrefix = "color:";
constexpr std::string_view kEllipsis = "\xE2\x80\xA6";

}  // namespace

std::vector<PaletteCommandEntry> SpaceMenuPaletteCommands(bool can_delete) {
    std::vector<PaletteCommandEntry> commands;
    commands.reserve(kSpaceMenuColorChoices.size() + 4);
    commands.push_back({.id = std::string(kSpaceMenuPrefix) + "rename",
                        .title = "Rename space" + std::string(kEllipsis)});
    commands.push_back({.id = std::string(kSpaceMenuPrefix) + "icon",
                        .title = "Change icon" + std::string(kEllipsis)});
    for (const SpaceMenuColorChoice& choice : kSpaceMenuColorChoices) {
        commands.push_back({.id = std::string(kSpaceMenuPrefix) + std::string(kColorPrefix) +
                                  std::string(choice.hex),
                            .title = "Colour: " + std::string(choice.name),
                            .icon = PaletteRowIcon::kPalette});
    }
    commands.push_back({.id = std::string(kSpaceMenuPrefix) + "new", .title = "New space"});
    if (can_delete) {
        commands.push_back(
            {.id = std::string(kSpaceMenuPrefix) + "delete", .title = "Delete space"});
    }
    return commands;
}

std::optional<SpaceMenuCommand> ParseSpaceMenuCommandId(std::string_view command_id) {
    if (!command_id.starts_with(kSpaceMenuPrefix)) {
        return std::nullopt;
    }
    const std::string_view verb = command_id.substr(kSpaceMenuPrefix.size());
    if (verb == "rename") {
        return SpaceMenuCommand{.action = SpaceMenuAction::kRename, .hex = {}};
    }
    if (verb == "icon") {
        return SpaceMenuCommand{.action = SpaceMenuAction::kChangeIcon, .hex = {}};
    }
    if (verb == "new") {
        return SpaceMenuCommand{.action = SpaceMenuAction::kNewSpace, .hex = {}};
    }
    if (verb == "delete") {
        return SpaceMenuCommand{.action = SpaceMenuAction::kDeleteSpace, .hex = {}};
    }
    if (verb.starts_with(kColorPrefix)) {
        const std::string_view hex = verb.substr(kColorPrefix.size());
        for (const SpaceMenuColorChoice& choice : kSpaceMenuColorChoices) {
            if (choice.hex == hex) {
                return SpaceMenuCommand{.action = SpaceMenuAction::kSetColor,
                                        .hex = std::string(hex)};
            }
        }
    }
    return std::nullopt;
}

}  // namespace island
