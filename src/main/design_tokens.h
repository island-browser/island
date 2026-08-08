#ifndef ISLAND_DESIGN_TOKENS_H_
#define ISLAND_DESIGN_TOKENS_H_

#include <cstdint>

namespace island {

enum class ChromeTheme : std::uint8_t {
    kLight,
    kDark,
};

struct ArgbColor {
    std::uint32_t argb = 0;

    bool operator==(const ArgbColor&) const = default;
};

enum class ChromeFont : std::uint8_t {
    kGeist,
    kGeistMono,
};

struct ChromeTokens {
    ArgbColor background;
    ArgbColor surface;
    ArgbColor surface_secondary;
    ArgbColor text;
    ArgbColor text_secondary;
    ArgbColor border;
    ArgbColor accent;
    int rail_width_dip = 286;
    int radius_small_dip = 8;
    int radius_medium_dip = 12;
    int spacing_1_dip = 4;
    int spacing_2_dip = 8;
    int spacing_3_dip = 12;
    int spacing_4_dip = 16;
    int spacing_6_dip = 24;
    ChromeFont ui_font = ChromeFont::kGeist;
    ChromeFont mono_font = ChromeFont::kGeistMono;

    [[nodiscard]] static ChromeTokens ForTheme(ChromeTheme theme) noexcept;

    bool operator==(const ChromeTokens&) const = default;
};

}  // namespace island

#endif
