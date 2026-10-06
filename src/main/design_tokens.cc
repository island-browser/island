#include "design_tokens.h"

#include <algorithm>
#include <cmath>

namespace island {

ChromeTokens ChromeTokens::ForTheme(ChromeTheme theme) noexcept {
    if (theme == ChromeTheme::kDark) {
        return {
            .background = {.argb = 0xFF0D1B26U},
            .surface = {.argb = 0xFF142633U},
            .surface_secondary = {.argb = 0xFF1B3040U},
            .text = {.argb = 0xFFEAF3F3U},
            .text_secondary = {.argb = 0xFF9CB0B5U},
            .border = {.argb = 0xFF29414EU},
            .accent = {.argb = 0xFF168C99U},
        };
    }

    return {
        .background = {.argb = 0xFFF3F0E9U},
        .surface = {.argb = 0xFFFFFEFBU},
        .surface_secondary = {.argb = 0xFFECE9E2U},
        .text = {.argb = 0xFF18303AU},
        .text_secondary = {.argb = 0xFF687A7DU},
        .border = {.argb = 0xFFD8D8D0U},
        .accent = {.argb = 0xFF168C99U},
    };
}

namespace {

double Channel(std::uint32_t argb, int shift) {
    return static_cast<double>((argb >> static_cast<unsigned>(shift)) & 0xFFU);
}

double Linearize(double channel) {
    const double c = channel / 255.0;
    return c <= 0.04045 ? c / 12.92 : std::pow((c + 0.055) / 1.055, 2.4);
}

double RelativeLuminance(ArgbColor color) {
    return 0.2126 * Linearize(Channel(color.argb, 16)) +
           0.7152 * Linearize(Channel(color.argb, 8)) + 0.0722 * Linearize(Channel(color.argb, 0));
}

}  // namespace

ArgbColor MixColor(ArgbColor from, ArgbColor to, double amount) noexcept {
    const double t = std::clamp(amount, 0.0, 1.0);
    std::uint32_t mixed = 0xFF000000U;
    for (const int shift : {16, 8, 0}) {
        const double value = Channel(from.argb, shift) * (1.0 - t) + Channel(to.argb, shift) * t;
        mixed |= static_cast<std::uint32_t>(std::lround(value)) << static_cast<unsigned>(shift);
    }
    return {.argb = mixed};
}

double ContrastRatio(ArgbColor a, ArgbColor b) noexcept {
    const double la = RelativeLuminance(a);
    const double lb = RelativeLuminance(b);
    return (std::max(la, lb) + 0.05) / (std::min(la, lb) + 0.05);
}

ChromeTokens ChromeTokens::TintedForSpace(ArgbColor space_color, ChromeTheme theme) const noexcept {
    const ArgbColor space{.argb = space_color.argb | 0xFF000000U};
    const bool dark = theme == ChromeTheme::kDark;
    ChromeTokens tinted = *this;
    tinted.background = MixColor(background, space, dark ? 0.12 : 0.10);
    tinted.surface_secondary = MixColor(surface_secondary, space, dark ? 0.18 : 0.16);
    tinted.border = MixColor(border, space, dark ? 0.24 : 0.20);
    if (dark) {
        tinted.surface = MixColor(surface, space, 0.06);
    }
    // The accent marks actions and active state on the surface; walk it
    // toward black (light) or white (dark) until it reads at 3:1.
    const ArgbColor anchor{.argb = dark ? 0xFFFFFFFFU : 0xFF000000U};
    ArgbColor accent_color = space;
    for (int step = 0; step < 20 && ContrastRatio(accent_color, tinted.surface) < 3.0; ++step) {
        accent_color = MixColor(accent_color, anchor, 0.08);
    }
    tinted.accent = accent_color;
    return tinted;
}

}  // namespace island
