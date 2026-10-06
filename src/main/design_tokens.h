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

    // Arc-style space theming, applied at runtime on top of the contract
    // tokens: the canvas, rail, and hairlines take a soft wash of the active
    // space's color, and the accent becomes that color, darkened (light) or
    // lightened (dark) until it keeps 3:1 contrast against the surface. Text
    // tokens never change, and the wash is light enough to keep body text at
    // WCAG AA on the rail.
    [[nodiscard]] ChromeTokens TintedForSpace(ArgbColor space_color,
                                              ChromeTheme theme) const noexcept;

    bool operator==(const ChromeTokens&) const = default;
};

// Linear blend of two opaque colors; `amount` 0 keeps `from`, 1 gives `to`.
[[nodiscard]] ArgbColor MixColor(ArgbColor from, ArgbColor to, double amount) noexcept;
// WCAG 2.x contrast ratio between two opaque colors (1..21).
[[nodiscard]] double ContrastRatio(ArgbColor a, ArgbColor b) noexcept;

}  // namespace island

#endif
