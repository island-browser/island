#include "design_tokens.h"

#include <algorithm>
#include <cmath>
#include <cstdlib>
#include <string>

namespace island {

ChromeTokens ChromeTokens::ForTheme(ChromeTheme theme) noexcept {
    if (theme == ChromeTheme::kDark) {
        return {
            .background = {.argb = 0xFF0A0A0AU},
            .surface = {.argb = 0xFF111111U},
            .surface_secondary = {.argb = 0xFF1A1A1AU},
            .text = {.argb = 0xFFEDEDEDU},
            .text_secondary = {.argb = 0xFFA1A1A1U},
            .border = {.argb = 0xFF2A2A2AU},
            .accent = {.argb = 0xFF3291FFU},
            .canvas = {.argb = 0xFF1A1A1AU},
            .selected = {.argb = 0xFF2A2A2AU},
            .hover = {.argb = 0xFF222222U},
            .well = {.argb = 0xFF242424U},
            .plate = {.argb = 0xFFEDEDEDU},
        };
    }

    return {
        .background = {.argb = 0xFFFAFAFAU},
        .surface = {.argb = 0xFFFFFFFFU},
        .surface_secondary = {.argb = 0xFFF2F2F2U},
        .text = {.argb = 0xFF0A0A0AU},
        .text_secondary = {.argb = 0xFF666666U},
        .border = {.argb = 0xFFE5E5E5U},
        .accent = {.argb = 0xFF0068D6U},
        .canvas = {.argb = 0xFFF2F2F2U},
        .selected = {.argb = 0xFFFFFFFFU},
        .hover = {.argb = 0xFFEAEAEAU},
        .well = {.argb = 0xFFFAFAFAU},
        .plate = {.argb = 0xFFFFFFFFU},
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

namespace {

// Walks `fg` toward `anchor` until it keeps `target` contrast against every background.
ArgbColor EnsureContrast(ArgbColor fg, ArgbColor anchor, ArgbColor bg_a, ArgbColor bg_b,
                         double target) {
    for (int step = 0; step < 24; ++step) {
        if (ContrastRatio(fg, bg_a) >= target && ContrastRatio(fg, bg_b) >= target) {
            break;
        }
        fg = MixColor(fg, anchor, 0.08);
    }
    return fg;
}

ChromeTokens Tint(const ChromeTokens& base, ArgbColor space_color, ChromeTheme theme,
                  double strength) {
    const ArgbColor space{.argb = space_color.argb | 0xFF000000U};
    const bool dark = theme == ChromeTheme::kDark;
    const ArgbColor white{.argb = 0xFFFFFFFFU};
    const ArgbColor black{.argb = 0xFF000000U};
    ChromeTokens tinted = base;
    // Arc-style: the sidebar and the window margin are one surface in the space's color. The
    // palettes, welcome card and textfields keep the neutral background/surface tokens.
    double amount = std::clamp(strength, 0.0, 1.0) * (dark ? 0.42 : 0.62);
    const auto derive = [&](double mix) {
        tinted.canvas = MixColor(dark ? base.background : base.surface_secondary, space, mix);
        tinted.selected = MixColor(tinted.canvas, white, dark ? 0.14 : 0.62);
        tinted.hover = MixColor(tinted.canvas, white, dark ? 0.07 : 0.34);
        tinted.well = MixColor(tinted.canvas, white, dark ? 0.09 : 0.46);
    };
    const auto readable = [&]() {
        for (const ArgbColor surface :
             {tinted.canvas, tinted.selected, tinted.hover, tinted.well}) {
            if (ContrastRatio(base.text, surface) < 4.5) {
                return false;
            }
        }
        return true;
    };
    // A vivid or extreme space color (pure white or black, full intensity) must never cost the
    // body text its 4.5:1 on the sidebar: ease the cast until it holds.
    derive(amount);
    for (int step = 0; step < 24 && !readable(); ++step) {
        amount *= 0.85;
        derive(amount);
    }
    tinted.plate = dark ? MixColor(tinted.canvas, white, 0.9) : white;
    tinted.background = MixColor(base.background, space, amount * 0.25);
    tinted.surface_secondary = tinted.canvas;
    tinted.border = MixColor(tinted.canvas, dark ? white : black, dark ? 0.12 : 0.10);
    // Secondary text must stay readable on the canvas and on every row fill.
    ArgbColor secondary = base.text_secondary;
    for (int step = 0; step < 24; ++step) {
        bool ok = true;
        for (const ArgbColor surface :
             {tinted.canvas, tinted.selected, tinted.hover, tinted.well}) {
            ok = ok && ContrastRatio(secondary, surface) >= 4.5;
        }
        if (ok) {
            break;
        }
        secondary = MixColor(secondary, base.text, 0.08);
    }
    tinted.text_secondary = secondary;
    // The accent marks actions and active state on the canvas; walk it toward black (light) or
    // white (dark) until it reads at 3:1 on the canvas and on the selected row.
    tinted.accent =
        EnsureContrast(space, dark ? white : black, tinted.canvas, tinted.selected, 3.0);
    return tinted;
}

}  // namespace

ChromeTokens ChromeTokens::TintedForSpace(ArgbColor space_color, ChromeTheme theme) const {
    // Default strength matches AppearanceSettings::sidebar_tint_percent.
    return Tint(*this, space_color, theme, 0.60);
}

int SnapCardRadius(int radius_dip) noexcept {
    constexpr int kAllowed[] = {0, 6, 10, 14};
    int best = kAllowed[0];
    for (const int candidate : kAllowed) {
        if (std::abs(candidate - radius_dip) < std::abs(best - radius_dip)) {
            best = candidate;
        }
    }
    return best;
}

AppearanceSettings NormalizeAppearance(AppearanceSettings settings) {
    settings.sidebar_tint_percent = std::clamp(settings.sidebar_tint_percent, 0, 100);
    settings.card_radius_dip = SnapCardRadius(settings.card_radius_dip);
    std::string family;
    for (const char ch : settings.font_family) {
        const auto byte = static_cast<unsigned char>(ch);
        if (byte >= 0x20 && byte != 0x7F && ch != '"' && ch != '\\' && ch != ',') {
            family.push_back(ch);
        }
    }
    while (!family.empty() && family.front() == ' ') {
        family.erase(family.begin());
    }
    while (!family.empty() && family.back() == ' ') {
        family.pop_back();
    }
    if (family.size() > 64) {
        family.resize(64);
    }
    settings.font_family = family;
    return settings;
}

ChromeTokens ChromeTokens::Customized(ArgbColor space_color, ChromeTheme theme,
                                      const AppearanceSettings& raw) const {
    const AppearanceSettings appearance = NormalizeAppearance(raw);
    ChromeTokens tokens = Tint(*this, space_color, theme, appearance.sidebar_tint_percent / 100.0);
    tokens.sidebar_side = appearance.sidebar_side;
    const bool bleed = appearance.full_bleed;
    tokens.card_radius_dip = bleed ? 0 : appearance.card_radius_dip;
    tokens.card_inset_dip = bleed ? 0 : 8;
    tokens.card_shadow = !bleed && appearance.card_shadow;
    const bool compact = appearance.density == Density::kCompact;
    tokens.control_height_dip = compact ? 26 : 32;
    tokens.row_gap_dip = compact ? 0 : 2;
    const std::string& family = appearance.font_family;
    if (family.empty() || family == "geist") {
        tokens.ui_font_family = "Geist";
    } else if (family == "system") {
        tokens.ui_font_family = "system";
    } else {
        tokens.ui_font_family = family;
    }
    return tokens;
}

}  // namespace island
