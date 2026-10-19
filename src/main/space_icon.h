#ifndef ISLAND_SPACE_ICON_H_
#define ISLAND_SPACE_ICON_H_

#include <array>
#include <string>
#include <string_view>

namespace island {

struct SpaceIconChoice {
    std::string_view id;
    std::string_view label;
};

inline constexpr std::array<SpaceIconChoice, 24> kSpaceIconChoices = {{
    {"home", "Home"},
    {"briefcase", "Briefcase"},
    {"book-open", "Book open"},
    {"code", "Code"},
    {"globe", "Globe"},
    {"plane", "Plane"},
    {"shopping-bag", "Shopping bag"},
    {"music", "Music"},
    {"film", "Film"},
    {"heart", "Heart"},
    {"star", "Star"},
    {"flask", "Flask"},
    {"graduation-cap", "Graduation cap"},
    {"coffee", "Coffee"},
    {"camera", "Camera"},
    {"gamepad", "Gamepad"},
    {"newspaper", "Newspaper"},
    {"palette", "Palette"},
    {"dumbbell", "Dumbbell"},
    {"wallet", "Wallet"},
    {"bug", "Bug"},
    {"rocket", "Rocket"},
    {"leaf", "Leaf"},
    {"folder", "Folder"},
}};

[[nodiscard]] inline bool IsKnownSpaceIconId(std::string_view id) noexcept {
    for (const SpaceIconChoice& choice : kSpaceIconChoices) {
        if (choice.id == id) {
            return true;
        }
    }
    return false;
}

// Empty means no icon. Unknown ids deliberately normalize to none so old or edited session files
// never manufacture an untrusted resource path.
[[nodiscard]] inline std::string NormalizeSpaceIconId(std::string_view id) {
    return IsKnownSpaceIconId(id) ? std::string(id) : std::string();
}

}  // namespace island

#endif  // ISLAND_SPACE_ICON_H_
