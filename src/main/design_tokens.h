#ifndef ISLAND_DESIGN_TOKENS_H_
#define ISLAND_DESIGN_TOKENS_H_

#include <array>
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

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

// Where the sidebar sits; the page card takes the other side.
enum class SidebarSide : std::uint8_t { kLeft, kRight };

// Control heights and spacing of the sidebar: comfortable is the Arc-like default (32 DIP rows),
// compact tightens rows to 26 DIP.
enum class Density : std::uint8_t { kComfortable, kCompact };

enum class LookPreset : std::uint8_t { kNative, kArc, kGraphite };

// The user-customizable look layered on the theme tokens (Settings > Appearance, persisted in
// prefs). CEF-free; every field is clamped/normalized by NormalizeAppearance before use.
struct AppearanceSettings {
    // The token base these knobs were derived from. Settings displays MatchingLook(), not this
    // stored identity, so changing any knob makes its label read Custom.
    LookPreset look = LookPreset::kNative;
    // How strongly the active space's color tints the sidebar and canvas, 0 (neutral graphite)
    // to 100 (vivid).
    int sidebar_tint_percent = 0;
    SidebarSide sidebar_side = SidebarSide::kLeft;
    // Page card corner radius in DIP: one of 0, 6, 10, 14.
    int card_radius_dip = 0;
    Density density = Density::kComfortable;
    // Interface font: "" or "geist" = bundled Geist, "system" = the platform UI font, anything
    // else is an installed family name.
    std::string font_family = "system";
    // The page card's single soft shadow.
    bool card_shadow = false;
    // Full-bleed: no inset, no radius, no shadow; the page fills its side edge to edge.
    bool full_bleed = true;

    bool operator==(const AppearanceSettings&) const = default;
};

// Applies every knob of a built-in look. Picking a preset after customization therefore restores
// the preset completely instead of changing only its identity.
[[nodiscard]] AppearanceSettings ApplyLookPreset(LookPreset look);
[[nodiscard]] std::string_view LookName(std::optional<LookPreset> look) noexcept;
[[nodiscard]] std::optional<LookPreset> ParseLook(std::string_view name) noexcept;
// Returns the preset whose knobs match `settings`; the stored look identity is deliberately
// ignored so changing any knob makes the Settings label read Custom.
[[nodiscard]] std::optional<LookPreset> MatchingLook(const AppearanceSettings& settings);

// Clamps tint to 0..100, snaps the radius to the nearest allowed value, and trims/limits the
// font family (control characters and quotes dropped, 64 bytes at most).
[[nodiscard]] AppearanceSettings NormalizeAppearance(AppearanceSettings settings);
// The allowed card radii.
[[nodiscard]] int SnapCardRadius(int radius_dip) noexcept;

struct ChromeTokens {
    ArgbColor background;
    ArgbColor surface;
    ArgbColor surface_secondary;
    ArgbColor fill{};
    ArgbColor text;
    ArgbColor text_secondary;
    ArgbColor text_tertiary{};
    ArgbColor border;
    ArgbColor border_strong{};
    ArgbColor accent;
    ArgbColor selection{};
    ArgbColor accent_soft{};
    ArgbColor accent_text{};
    ArgbColor warning_bg{};
    ArgbColor warning_border{};
    ArgbColor warning_text{};
    ArgbColor success{};
    ArgbColor danger{};
    std::array<ArgbColor, 4> branch{};
    // Arc-style runtime tokens (derived by TintedForSpace; neutral defaults from ForTheme).
    // `canvas` is the single surface of the sidebar and of the window margin around the page
    // card; `selected`/`hover` are the soft fills of the selected and hovered rows; `well` is
    // the address pill; `plate` is the light plate under favicons so dark icons stay visible.
    ArgbColor canvas;
    ArgbColor selected;
    ArgbColor hover;
    ArgbColor well;
    ArgbColor plate;
    // Every look: the neutral raised plate under a selected segmented-control option.
    ArgbColor segment_selected{};
    int rail_width_dip = 286;
    int card_radius_dip = 10;
    // Inset of the page card from the window edge on its three free sides.
    int card_inset_dip = 8;
    bool card_shadow = true;
    SidebarSide sidebar_side = SidebarSide::kLeft;
    int control_height_dip = 36;
    int row_gap_dip = 6;
    int sidebar_padding_dip = 16;
    // Frameless macOS window: the height of the sidebar's top strip that hosts the traffic lights
    // and drags the window (0 = the platform keeps its own title bar). Set by the window host.
    int titlebar_band_dip = 0;
    // Interface font family as handed to CEF font lists ("Geist" by default).
    std::string ui_font_family = "Geist";
    int radius_small_dip = 6;
    int radius_medium_dip = 10;
    int spacing_1_dip = 4;
    int spacing_2_dip = 8;
    int spacing_3_dip = 12;
    int spacing_4_dip = 16;
    int spacing_6_dip = 24;
    ChromeFont ui_font = ChromeFont::kGeist;
    ChromeFont mono_font = ChromeFont::kGeistMono;
    // The resolved look travels with the palette so CEF-free painters can keep Native-only
    // selection semantics without inferring the preset from color values.
    LookPreset look = LookPreset::kGraphite;

    [[nodiscard]] static ChromeTokens ForTheme(ChromeTheme theme) noexcept;
    [[nodiscard]] static ChromeTokens ForLook(LookPreset look, ChromeTheme theme) noexcept;

    // Arc-style space theming, applied at runtime on top of the contract
    // tokens: the canvas, rail, and hairlines take a faint cast of the active
    // space's color, and the accent becomes that color, darkened (light) or
    // lightened (dark) until it keeps 3:1 contrast against the surface. Text
    // tokens never change, and the wash is light enough to keep body text at
    // WCAG AA on the rail.
    [[nodiscard]] ChromeTokens TintedForSpace(ArgbColor space_color, ChromeTheme theme) const;
    // The full customized resolution: tint strength, density, radius, inset, shadow, side and
    // font from `appearance`, on top of the space color and theme.
    [[nodiscard]] ChromeTokens Customized(ArgbColor space_color, ChromeTheme theme,
                                          const AppearanceSettings& appearance) const;

    bool operator==(const ChromeTokens&) const = default;
};

// Linear blend of two opaque colors; `amount` 0 keeps `from`, 1 gives `to`.
[[nodiscard]] ArgbColor MixColor(ArgbColor from, ArgbColor to, double amount) noexcept;
// WCAG 2.x contrast ratio between two opaque colors (1..21).
[[nodiscard]] double ContrastRatio(ArgbColor a, ArgbColor b) noexcept;

// The CSS font-family stack the internal pages use for the resolved interface font: the bundled
// Geist stack, the platform system stack, or the installed custom family first (quoted) with the
// Geist/system stack behind it.
[[nodiscard]] std::string CssFontStack(const ChromeTokens& tokens);

}  // namespace island

#endif
