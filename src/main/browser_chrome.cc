#include "browser_chrome.h"

#include <cmath>
#include <cstdint>
#include <memory>
#include <optional>
#include <string>
#include <utility>
#include <vector>

#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_task.h"
#include "include/cef_values.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_scroll_view.h"
#include "include/views/cef_textfield.h"
#include "include/views/cef_textfield_delegate.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

// allow: SIZE_OK — the CEF delegate callbacks must share one composition owner.
namespace island {
namespace {

constexpr int kReturnKeyCode = 0x0D;
constexpr int kEscapeKeyCode = 0x1B;

// Flex weights run on a 0..100 budget: the first pane gets the ratio's share,
// clamped so neither pane can fully collapse while the split is attached.
int SplitFlexForRatio(double ratio) {
    const double clamped =
        std::clamp(ratio, BrowserChrome::SplitRatioMin(), BrowserChrome::SplitRatioMax());
    return std::clamp(static_cast<int>(std::lround(clamped * 100.0)), 1, 99);
}

// Advances to the byte offset just past the UTF-8 code point starting at
// |byte_index|; continuation bytes never start a new character, so truncation
// and character counts never split a sequence.
std::size_t AdvanceUtf8Character(const std::string& text, std::size_t byte_index) {
    ++byte_index;
    while (byte_index < text.size() &&
           (static_cast<unsigned char>(text[byte_index]) & 0xC0) == 0x80) {
        ++byte_index;
    }
    return byte_index;
}

// Every rail control (icon button, address pill, tab row, space chip, footer
// button) shares this height so the column reads on one rhythm: 32 DIP.
int ControlHeight(const ChromeTokens& tokens) { return tokens.control_height_dip; }

// Outline thickness of a hairline-defined surface (address pill, active tab,
// active space chip). CEF views have no border or corner-radius API, so an
// outlined control is a panel painted with the hairline color that insets its
// single child by this many DIP.
constexpr int kHairlineDip = 1;
// Native paints focus as a 2 DIP accent ring; the row inset is that ring.
constexpr int kNativeFocusRingDip = 2;

int DividerHeight(const ChromeTokens& tokens) {
    return tokens.spacing_1_dip / tokens.spacing_1_dip;
}

DipRect ToDipRect(const CefRect& bounds) {
    return {.x = bounds.x, .y = bounds.y, .width = bounds.width, .height = bounds.height};
}

void SelectAllWhenFocused(CefRefPtr<CefTextfield> textfield) {
    CEF_REQUIRE_UI_THREAD();
    if (textfield != nullptr && textfield->HasFocus()) {
        textfield->SelectAll(false);
    }
}

// A horizontal run of controls with no inside border: the rail owns the gutter, so
// rows of controls never add their own padding.
CefBoxLayoutSettings HorizontalLayout(const ChromeTokens& tokens) {
    CefBoxLayoutSettings settings;
    settings.horizontal = static_cast<int>(true);
    settings.between_child_spacing = tokens.spacing_1_dip / 2;
    settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_CENTER;
    return settings;
}

// The navigation row: back/forward start to the right of the traffic lights when the frameless
// window puts them over the left sidebar.
CefBoxLayoutSettings NavigationRowLayout(const ChromeTokens& tokens) {
    CefBoxLayoutSettings settings = HorizontalLayout(tokens);
    if (tokens.look != LookPreset::kNative && tokens.titlebar_band_dip > 0 &&
        tokens.sidebar_side == SidebarSide::kLeft) {
        settings.inside_border_insets = CefInsets(0, BrowserChrome::NavigationLeadDip(), 0, 0);
    }
    return settings;
}

// A hairline-outlined control: the panel paints the outline color and insets its
// children by kHairlineDip, so a child painted with the surface color reads as a
// bordered field.
CefBoxLayoutSettings OutlinedLayout(int outline_dip) {
    CefBoxLayoutSettings settings;
    settings.horizontal = static_cast<int>(true);
    settings.between_child_spacing = 0;
    settings.inside_border_insets = CefInsets(outline_dip, outline_dip, outline_dip, outline_dip);
    return settings;
}

CefBoxLayoutSettings ToolbarRowLayout(const ChromeTokens& tokens, int leading_inset) {
    CefBoxLayoutSettings settings;
    settings.horizontal = static_cast<int>(true);
    settings.between_child_spacing = tokens.spacing_2_dip;
    settings.inside_border_insets = CefInsets(0, leading_inset, 0, tokens.spacing_3_dip);
    settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_CENTER;
    return settings;
}

// The browser_content panel holds the single CefBrowserView with the card insets of
// BrowserChrome::ContentInsets: the sidebar-colored canvas shows through the gap and the page
// reads as a raised card. No spacing between the panel and the view beyond the insets.
CefBoxLayoutSettings FloatingCanvasLayout(const BrowserChrome::CardInsets& insets) {
    CefBoxLayoutSettings settings;
    settings.horizontal = static_cast<int>(true);
    settings.between_child_spacing = 0;
    settings.inside_border_insets = CefInsets(insets.top, insets.left, insets.bottom, insets.right);
    return settings;
}

// The rail uses a dedicated layout: a top inset (RailTopInsetDip), the shared
// between-section step (RailSectionSpacingDip), and spacing_3 side and bottom
// insets so the whole column sits on one gutter.
CefBoxLayoutSettings RailLayout(const ChromeTokens& tokens) {
    CefBoxLayoutSettings settings;
    settings.horizontal = static_cast<int>(false);
    settings.between_child_spacing = BrowserChrome::RailSectionSpacingDip();
    const int top = tokens.look == LookPreset::kNative && tokens.titlebar_band_dip > 0
                        ? tokens.titlebar_band_dip
                        : BrowserChrome::RailTopInsetDip(tokens);
    settings.inside_border_insets =
        CefInsets(top, tokens.spacing_3_dip, tokens.spacing_3_dip, tokens.spacing_3_dip);
    return settings;
}

// The icon rail starts below the traffic lights when the window is frameless.
int CollapsedRailTopInset(const ChromeTokens& tokens) {
    return tokens.titlebar_band_dip > 0 ? tokens.titlebar_band_dip
                                        : BrowserChrome::RailTopInsetDip();
}

// Collection regions (tab strip, space switcher) are empty until Phase 3 wires
// entries; zero inside borders keep an empty collection from injecting phantom
// padding into the rail rhythm. Entry separation stays on spacing_2.
CefBoxLayoutSettings CollectionLayout(const ChromeTokens& tokens) {
    const int zero_dip = tokens.spacing_1_dip - tokens.spacing_1_dip;
    CefBoxLayoutSettings settings;
    settings.horizontal = static_cast<int>(false);
    settings.between_child_spacing = tokens.spacing_2_dip;
    settings.inside_border_horizontal_spacing = zero_dip;
    settings.inside_border_vertical_spacing = zero_dip;
    return settings;
}

// The tab list packs rows 2 DIP apart so the selected row reads as one outlined
// row in a calm column (Arc's rhythm).
CefBoxLayoutSettings TabListLayout(const ChromeTokens& tokens) {
    CefBoxLayoutSettings settings = CollectionLayout(tokens);
    settings.between_child_spacing = tokens.row_gap_dip;
    return settings;
}

// The collapsed icon rail stacks centered square buttons; the rail owns the vertical
// gutter, the buttons own the horizontal one.
CefBoxLayoutSettings CollapsedColumnLayout(int spacing) {
    CefBoxLayoutSettings settings;
    settings.horizontal = static_cast<int>(false);
    settings.between_child_spacing = spacing;
    settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_CENTER;
    return settings;
}

// A transparent-looking wrapper (rail colored by its delegate) that centers a child.
CefBoxLayoutSettings CenteredLayout() {
    CefBoxLayoutSettings settings;
    settings.horizontal = static_cast<int>(false);
    settings.main_axis_alignment = CEF_AXIS_ALIGNMENT_CENTER;
    settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_CENTER;
    return settings;
}

// Multiplies a premultiplied BGRA bitmap by |alpha| (0..1): the dimmed look of a sleeping tab.
void DimPremultipliedBgra(std::vector<std::uint8_t>& pixels, double alpha) {
    for (std::uint8_t& channel : pixels) {
        channel = static_cast<std::uint8_t>(static_cast<double>(channel) * alpha + 0.5);
    }
}

// A reduced-alpha copy of |source| (the icon of a sleeping tab); the original when the image
// has no bitmap representation to copy.
CefRefPtr<CefImage> DimmedImage(const CefRefPtr<CefImage>& source, double alpha) {
    if (source == nullptr) {
        return source;
    }
    CefRefPtr<CefImage> dimmed = CefImage::CreateImage();
    bool copied = false;
    for (const float scale : {1.0F, 2.0F}) {
        if (!source->HasRepresentation(scale)) {
            continue;
        }
        int width = 0;
        int height = 0;
        const CefRefPtr<CefBinaryValue> data = source->GetAsBitmap(
            scale, CEF_COLOR_TYPE_BGRA_8888, CEF_ALPHA_TYPE_PREMULTIPLIED, width, height);
        if (data == nullptr || data->GetSize() == 0) {
            continue;
        }
        std::vector<std::uint8_t> pixels(data->GetSize());
        data->GetData(pixels.data(), pixels.size(), 0);
        DimPremultipliedBgra(pixels, alpha);
        copied = dimmed->AddBitmap(scale, width, height, CEF_COLOR_TYPE_BGRA_8888,
                                   CEF_ALPHA_TYPE_PREMULTIPLIED, pixels.data(), pixels.size()) ||
                 copied;
    }
    return copied ? dimmed : source;
}

constexpr double kSleepingIconAlpha = 0.42;
constexpr double kSleepingTitleMix = 0.55;

// The title color of a sleeping row: faded toward the row fill.
ArgbColor SleepingTitleColor(ArgbColor title, ArgbColor fill) {
    return MixColor(title, fill, kSleepingTitleMix);
}

// The round dot a collapsed-rail space button shows: 16 DIP at 1x and 2x.
CefRefPtr<CefImage> MakeSpaceDotImage(ArgbColor fill, ArgbColor ring, bool active) {
    constexpr int kDotCanvasDip = 16;
    CefRefPtr<CefImage> image = CefImage::CreateImage();
    for (const int scale : {1, 2}) {
        const SpaceDotBitmap bitmap = RenderSpaceDot(fill, ring, active, kDotCanvasDip, scale);
        image->AddBitmap(static_cast<float>(scale), bitmap.size_px, bitmap.size_px,
                         CEF_COLOR_TYPE_BGRA_8888, CEF_ALPHA_TYPE_PREMULTIPLIED, bitmap.bgra.data(),
                         bitmap.bgra.size());
    }
    return image;
}

constexpr int kIndicatorDip = 20;
constexpr int kShieldGlyphDip = 14;
constexpr int kAgentDotDip = 8;

// Even-odd point-in-polygon test for the shield outline.
bool InsidePolygon(double x, double y, const std::vector<std::pair<double, double>>& polygon) {
    bool inside = false;
    for (std::size_t i = 0, j = polygon.size() - 1; i < polygon.size(); j = i++) {
        const auto& [xi, yi] = polygon[i];
        const auto& [xj, yj] = polygon[j];
        if (((yi > y) != (yj > y)) && (x < (xj - xi) * (y - yi) / (yj - yi) + xi)) {
            inside = !inside;
        }
    }
    return inside;
}

// A filled shield glyph in `color`, drawn in software because the icon set has none.
CefRefPtr<CefImage> MakeShieldImage(ArgbColor color) {
    const std::vector<std::pair<double, double>> outline = {{7.0, 0.8},   {12.6, 2.8}, {12.6, 7.0},
                                                            {10.8, 10.2}, {7.0, 13.2}, {3.2, 10.2},
                                                            {1.4, 7.0},   {1.4, 2.8}};
    CefRefPtr<CefImage> image = CefImage::CreateImage();
    for (const int scale : {1, 2}) {
        RasterBitmap bitmap = NewRasterBitmap(kShieldGlyphDip, kShieldGlyphDip, scale);
        constexpr int kSamples = 4;
        for (int py = 0; py < bitmap.height_px; ++py) {
            for (int px = 0; px < bitmap.width_px; ++px) {
                int hits = 0;
                for (int sy = 0; sy < kSamples; ++sy) {
                    for (int sx = 0; sx < kSamples; ++sx) {
                        const double x = (px + (sx + 0.5) / kSamples) / scale;
                        const double y = (py + (sy + 0.5) / kSamples) / scale;
                        hits += InsidePolygon(x, y, outline) ? 1 : 0;
                    }
                }
                const double coverage = static_cast<double>(hits) / (kSamples * kSamples);
                const std::size_t at = (static_cast<std::size_t>(py) * bitmap.width_px + px) * 4;
                const auto channel = [&](std::uint32_t shift) {
                    return static_cast<std::uint8_t>(((color.argb >> shift) & 0xFFU) * coverage +
                                                     0.5);
                };
                bitmap.bgra[at] = channel(0);
                bitmap.bgra[at + 1] = channel(8);
                bitmap.bgra[at + 2] = channel(16);
                bitmap.bgra[at + 3] = static_cast<std::uint8_t>(255.0 * coverage + 0.5);
            }
        }
        image->AddBitmap(static_cast<float>(scale), bitmap.width_px, bitmap.height_px,
                         CEF_COLOR_TYPE_BGRA_8888, CEF_ALPHA_TYPE_PREMULTIPLIED, bitmap.bgra.data(),
                         bitmap.bgra.size());
    }
    return image;
}

// The agent dot: a filled circle (`kAgentDotDip` wide) in `color`.
CefRefPtr<CefImage> MakeAgentDotImage(ArgbColor color) {
    CefRefPtr<CefImage> image = CefImage::CreateImage();
    for (const int scale : {1, 2}) {
        RasterBitmap bitmap = NewRasterBitmap(kAgentDotDip, kAgentDotDip, scale);
        FillRoundedRect(bitmap, 0, 0, kAgentDotDip, kAgentDotDip,
                        {kAgentDotDip / 2, kAgentDotDip / 2, kAgentDotDip / 2, kAgentDotDip / 2},
                        color);
        image->AddBitmap(static_cast<float>(scale), bitmap.width_px, bitmap.height_px,
                         CEF_COLOR_TYPE_BGRA_8888, CEF_ALPHA_TYPE_PREMULTIPLIED, bitmap.bgra.data(),
                         bitmap.bgra.size());
    }
    return image;
}

// Pixels of a CefImage at `scale`, premultiplied BGRA.
struct ImagePixels {
    int width = 0;
    int height = 0;
    std::vector<std::uint8_t> data;
};

ImagePixels PixelsAt(const CefRefPtr<CefImage>& image, int scale) {
    ImagePixels pixels;
    if (image == nullptr) {
        return pixels;
    }
    int width = 0;
    int height = 0;
    CefRefPtr<CefBinaryValue> binary =
        image->GetAsBitmap(static_cast<float>(scale), CEF_COLOR_TYPE_BGRA_8888,
                           CEF_ALPHA_TYPE_PREMULTIPLIED, width, height);
    if (binary == nullptr || width <= 0 || height <= 0 ||
        binary->GetSize() != static_cast<std::size_t>(width) * height * 4) {
        return pixels;
    }
    pixels.width = width;
    pixels.height = height;
    pixels.data.resize(binary->GetSize());
    binary->GetData(pixels.data.data(), pixels.data.size(), 0);
    return pixels;
}

// One edge button's image: an optional rounded fill (the row's selected/hover state), an
// optional light plate under a page favicon (so dark favicons stay visible on a dark sidebar),
// and the glyph, all rendered into a w x h DIP bitmap at 1x and 2x.
struct RowImageSpec {
    int width_dip = 32;
    int height_dip = 32;
    CornerRadii radii;
    std::optional<ArgbColor> fill;
    std::optional<ArgbColor> plate;
    CefRefPtr<CefImage> glyph;
};

constexpr int kFaviconPlateDip = 22;
// Native's bottom controls (2026-10-16 owner request): the Agent entry is 30-36 DIP tall. The
// switcher row's sizes come from BrowserChrome::NativeSwitcherSizes().
constexpr int kNativeAgentHeightDip = 32;
constexpr int kNativeSegmentRadiusDip = 6;
constexpr int kFaviconPlateRadiusDip = 6;
constexpr int kRowFillRadiusDip = 8;
constexpr int kPatchPollMs = 150;

CefRefPtr<CefImage> BuildRowImage(const RowImageSpec& spec) {
    CefRefPtr<CefImage> image = CefImage::CreateImage();
    for (const int scale : {1, 2}) {
        RasterBitmap bitmap = NewRasterBitmap(spec.width_dip, spec.height_dip, scale);
        if (spec.fill.has_value()) {
            FillRoundedRect(bitmap, 0, 0, spec.width_dip, spec.height_dip, spec.radii, *spec.fill);
        }
        if (spec.plate.has_value()) {
            const int size = kFaviconPlateDip;
            FillRoundedRect(bitmap, (spec.width_dip - size) / 2, (spec.height_dip - size) / 2, size,
                            size,
                            {kFaviconPlateRadiusDip, kFaviconPlateRadiusDip, kFaviconPlateRadiusDip,
                             kFaviconPlateRadiusDip},
                            *spec.plate);
        }
        const ImagePixels glyph = PixelsAt(spec.glyph, scale);
        if (glyph.width > 0) {
            CompositePremultiplied(bitmap, glyph.data.data(), glyph.width, glyph.height,
                                   (bitmap.width_px - glyph.width) / 2,
                                   (bitmap.height_px - glyph.height) / 2);
        }
        image->AddBitmap(static_cast<float>(scale), bitmap.width_px, bitmap.height_px,
                         CEF_COLOR_TYPE_BGRA_8888, CEF_ALPHA_TYPE_PREMULTIPLIED, bitmap.bgra.data(),
                         bitmap.bgra.size());
    }
    return image;
}

void SetAllStateImages(const CefRefPtr<CefLabelButton>& button, const CefRefPtr<CefImage>& image) {
    for (const cef_button_state_t state : {CEF_BUTTON_STATE_NORMAL, CEF_BUTTON_STATE_HOVERED,
                                           CEF_BUTTON_STATE_PRESSED, CEF_BUTTON_STATE_DISABLED}) {
        button->SetImage(state, image);
    }
}

// The favicon on its small rounded plate (page favicons only; the globe fallback stays bare), so
// dark favicons such as GitHub's stay visible on a dark sidebar.
CefRefPtr<CefImage> BuildFaviconImage(const CefRefPtr<CefImage>& glyph, bool on_plate,
                                      ArgbColor plate) {
    // The bare globe sits on the same canvas size as a plated favicon so every title lines up.
    return BuildRowImage({.width_dip = kFaviconPlateDip + 2,
                          .height_dip = kFaviconPlateDip + 2,
                          .radii = {},
                          .fill = std::nullopt,
                          .plate = on_plate ? std::optional<ArgbColor>(plate) : std::nullopt,
                          .glyph = glyph});
}

std::string AddressErrorMessage(const std::optional<AddressError>& error) {
    if (!error.has_value()) {
        return "";
    }

    switch (*error) {
        case AddressError::kNotAbsolute:
            return "Enter an absolute http or https address.";
        case AddressError::kUnsupportedScheme:
            return "Only http and https addresses are supported.";
        case AddressError::kCredentialsNotAllowed:
            return "Addresses cannot include credentials.";
        case AddressError::kInvalidCharacter:
            return "Address contains an invalid character.";
        case AddressError::kInvalidHost:
            return "Enter an allowed address host.";
        case AddressError::kInvalidPort:
            return "Enter a valid address port.";
    }
    return "";
}

}  // namespace

AddressPlateStyle BrowserChrome::AddressPlateStyleFor(const ChromeTokens& tokens) {
    if (tokens.look == LookPreset::kNative) {
        return {.fill = tokens.fill,
                .outside = tokens.surface_secondary,
                .outline_dip = kNativeFocusRingDip,
                .radius_dip = NativeAddressRadiusDip()};
    }
    return {.fill = tokens.well,
            .outside = tokens.canvas,
            .outline_dip = kHairlineDip,
            .radius_dip = kRowFillRadiusDip};
}

ArgbColor BrowserChrome::AddressOutlineColor(const ChromeTokens& tokens, bool focused) {
    if (tokens.look == LookPreset::kNative) {
        // The ring is the row's inset: plate-colored while idle, the accent while focused.
        return focused ? tokens.accent : AddressPlateStyleFor(tokens).fill;
    }
    return focused ? tokens.accent : tokens.border;
}

std::optional<CornerPatch> BrowserChrome::AddressCornerPatch(const ChromeTokens& tokens,
                                                             const DipRect& bounds,
                                                             bool sidebar_expanded) {
    if (bounds.width <= 0 || bounds.height <= 0 ||
        (tokens.look != LookPreset::kNative && !sidebar_expanded)) {
        return std::nullopt;
    }
    const AddressPlateStyle style = AddressPlateStyleFor(tokens);
    // Only Native's toolbar differs from the canvas the patches are applied with; the other
    // looks leave `outside` empty so a theme reset repaints them in the current canvas.
    return CornerPatch{.x = bounds.x,
                       .y = bounds.y,
                       .width = bounds.width,
                       .height = bounds.height,
                       .radius = style.radius_dip,
                       .outside = tokens.look == LookPreset::kNative ? style.outside : ArgbColor{}};
}

std::string BrowserChrome::FontListForTokens(const ChromeTokens& tokens, int size_px,
                                             ChromeFontWeight weight) {
    std::string family = tokens.ui_font_family;
    if (family == "system") {
#if defined(__APPLE__)
        family = ".AppleSystemUIFont, Helvetica Neue";
#elif defined(_WIN32)
        family = "Segoe UI";
#else
        family = "sans-serif";
#endif
    } else if (family != "Geist") {
        family += ", Geist";
    }
    std::string style;
    switch (weight) {
        case ChromeFontWeight::kRegular:
            break;
        case ChromeFontWeight::kMedium:
            style = "Medium ";
            break;
        case ChromeFontWeight::kSemiBold:
            style = "Semi-Bold ";
            break;
        case ChromeFontWeight::kBold:
            style = "Bold ";
            break;
    }
    return family + ", " + style + std::to_string(size_px) + "px";
}

namespace {

struct LayoutColumns {
    DipRect rail;
    DipRect content;
    DipRect agent;
};

LayoutColumns ComputeColumns(DipRect root, const ChromeTokens& tokens, bool sidebar_expanded,
                             bool agent_panel_open) {
    const int rail_width =
        std::min(sidebar_expanded ? tokens.rail_width_dip : kCollapsedRailWidthDip, root.width);
    const int panel_width =
        agent_panel_open
            ? std::clamp(root.width - rail_width - BrowserChrome::MinimumBrowserContentWidthDip(),
                         0, BrowserChrome::AgentPanelWidthDip())
            : 0;
    const int content_width = std::max(0, root.width - rail_width - panel_width);
    LayoutColumns columns;
    if (tokens.sidebar_side == SidebarSide::kRight) {
        // The agent panel takes the opposite edge from the sidebar.
        columns.agent = {.x = root.x, .y = root.y, .width = panel_width, .height = root.height};
        columns.content = {
            .x = root.x + panel_width, .y = root.y, .width = content_width, .height = root.height};
        columns.rail = {.x = root.x + root.width - rail_width,
                        .y = root.y,
                        .width = rail_width,
                        .height = root.height};
    } else {
        columns.rail = {.x = root.x, .y = root.y, .width = rail_width, .height = root.height};
        columns.content = {
            .x = root.x + rail_width, .y = root.y, .width = content_width, .height = root.height};
        columns.agent = {.x = columns.content.x + content_width,
                         .y = root.y,
                         .width = panel_width,
                         .height = root.height};
    }
    return columns;
}

// A card whose left edge sits under the frameless window's traffic lights must start below them.
int TopInsetFor(const ChromeTokens& tokens, int card_x, int base_inset) {
    if (tokens.titlebar_band_dip > 0 && card_x < BrowserChrome::TrafficLightClearDip()) {
        return std::max(base_inset, tokens.titlebar_band_dip);
    }
    return base_inset;
}

}  // namespace

ChromeGeometrySnapshot BrowserChrome::LayoutForBounds(DipRect root_bounds,
                                                      const ChromeTokens& tokens,
                                                      bool sidebar_expanded,
                                                      bool agent_panel_open) {
    LayoutColumns columns = ComputeColumns(root_bounds, tokens, sidebar_expanded, agent_panel_open);
    DipRect toolbar_bounds;
    int toolbar_leading_inset = 0;
    if (tokens.look == LookPreset::kNative) {
        const int toolbar = std::min(NativeToolbarHeightDip(), columns.content.height);
        const int toolbar_x = tokens.sidebar_side == SidebarSide::kRight
                                  ? root_bounds.x
                                  : columns.rail.x + columns.rail.width;
        toolbar_bounds = {.x = toolbar_x,
                          .y = root_bounds.y,
                          .width = std::max(0, root_bounds.width - columns.rail.width),
                          .height = toolbar};
        toolbar_leading_inset = tokens.spacing_3_dip;
        if (tokens.titlebar_band_dip > 0) {
            toolbar_leading_inset = std::max(
                toolbar_leading_inset, TrafficLightClearDip() - (toolbar_bounds.x - root_bounds.x));
        }
        columns.content.y += toolbar;
        columns.content.height -= toolbar;
        if (columns.agent.width > 0) {
            columns.agent.y += toolbar;
            columns.agent.height = std::max(0, columns.agent.height - toolbar);
        }
    }
    const CardInsets insets =
        ContentInsets(root_bounds, tokens, sidebar_expanded, agent_panel_open);
    // The insets shrink the content region and are clamped so the card never inverts on very
    // narrow windows.
    const int left = std::min(insets.left, columns.content.width / 2);
    const int right = std::min(insets.right, columns.content.width / 2);
    const int top = std::min(insets.top, columns.content.height / 2);
    const int bottom = std::min(insets.bottom, columns.content.height / 2);
    const DipRect browser_view_bounds = {
        .x = columns.content.x + left,
        .y = columns.content.y + top,
        .width = std::max(0, columns.content.width - left - right),
        .height = std::max(0, columns.content.height - top - bottom),
    };
    return {
        .root_bounds = root_bounds,
        .rail_bounds = columns.rail,
        .toolbar_bounds = toolbar_bounds,
        .toolbar_leading_inset_dip = toolbar_leading_inset,
        .browser_content_bounds = columns.content,
        .browser_view_bounds = browser_view_bounds,
        .agent_panel_bounds = columns.agent,
    };
}

BrowserChrome::CardInsets BrowserChrome::ContentInsets(DipRect root_bounds,
                                                       const ChromeTokens& tokens,
                                                       bool sidebar_expanded,
                                                       bool agent_panel_open) {
    if (tokens.look == LookPreset::kNative) {
        return {};
    }
    const LayoutColumns columns =
        ComputeColumns(root_bounds, tokens, sidebar_expanded, agent_panel_open);
    const int inset = tokens.card_inset_dip;
    CardInsets insets{.left = inset,
                      .top = TopInsetFor(tokens, columns.content.x - root_bounds.x, inset),
                      .right = inset,
                      .bottom = inset};
    // Flush with the sidebar: the sidebar is the card's neighbor on that side.
    if (tokens.sidebar_side == SidebarSide::kRight) {
        insets.right = 0;
    } else {
        insets.left = 0;
    }
    return insets;
}

BrowserChrome::CardInsets BrowserChrome::AgentPanelInsets(DipRect root_bounds,
                                                          const ChromeTokens& tokens,
                                                          bool sidebar_expanded) {
    if (tokens.look == LookPreset::kNative) {
        return {};
    }
    const LayoutColumns columns = ComputeColumns(root_bounds, tokens, sidebar_expanded, true);
    const int inset = tokens.card_inset_dip;
    // The panel keeps an inset on the window edge only; the page card's own inset separates the
    // two.
    CardInsets insets{.left = 0,
                      .top = TopInsetFor(tokens, columns.agent.x - root_bounds.x, inset),
                      .right = 0,
                      .bottom = inset};
    if (tokens.sidebar_side == SidebarSide::kRight) {
        insets.left = inset;
    } else {
        insets.right = inset;
    }
    return insets;
}

std::vector<BrowserChrome::DragRegion> BrowserChrome::DragRegionsForBounds(
    DipRect root_bounds, const ChromeTokens& tokens, bool sidebar_expanded, bool agent_panel_open) {
    std::vector<DragRegion> regions;
    if (tokens.titlebar_band_dip <= 0) {
        return regions;
    }
    const LayoutColumns columns =
        ComputeColumns(root_bounds, tokens, sidebar_expanded, agent_panel_open);
    const int band = tokens.titlebar_band_dip;
    regions.push_back(
        {{.x = columns.rail.x, .y = 0, .width = columns.rail.width, .height = band}, true});
    // The canvas margin above each card drags too.
    const CardInsets content =
        ContentInsets(root_bounds, tokens, sidebar_expanded, agent_panel_open);
    regions.push_back(
        {{.x = columns.content.x, .y = 0, .width = columns.content.width, .height = content.top},
         true});
    if (columns.agent.width > 0) {
        const CardInsets agent = AgentPanelInsets(root_bounds, tokens, sidebar_expanded);
        regions.push_back(
            {{.x = columns.agent.x, .y = 0, .width = columns.agent.width, .height = agent.top},
             true});
    }
    if (sidebar_expanded) {
        // The navigation buttons stay clickable: carve them out of the band.
        const int lead =
            tokens.sidebar_side == SidebarSide::kLeft ? BrowserChrome::NavigationLeadDip() : 0;
        const int top = RailTopInsetDip(tokens);
        regions.push_back({{.x = columns.rail.x + tokens.spacing_3_dip + lead,
                            .y = top,
                            .width = 2 * tokens.control_height_dip + 2,
                            .height = tokens.control_height_dip},
                           false});
    }
    return regions;
}

// CEF resets every view's custom background (and the text colors derived from the
// theme) whenever CefViewDelegate::OnThemeChanged fires, and CefWindow::ThemeChanged()
// fires it after the chrome has painted. A view cannot know its own intended color, so
// every delegate reports the reset to the chrome, which coalesces the reports into one
// ReapplyThemeColors() pass that repaints every view from the resolved tokens and each
// entry's current state (selected tab, open agent panel, address focus).
class BrowserChrome::PanelDelegate final : public CefPanelDelegate {
  public:
    PanelDelegate(BrowserChrome* chrome, CefSize preferred_size, CefSize minimum_size = CefSize(),
                  CefSize maximum_size = CefSize())
        : alive_(chrome->alive_),
          preferred_size_(preferred_size),
          minimum_size_(minimum_size),
          maximum_size_(maximum_size) {}

    void OnThemeChanged(CefRefPtr<CefView>) override {
        if (BrowserChrome* chrome = *alive_) {
            chrome->HandleViewThemeReset();
        }
    }

    void SetFixedWidth(int width) {
        preferred_size_.width = width;
        minimum_size_.width = width;
        maximum_size_.width = width;
    }

    void SetPreferredSize(CefSize size) { preferred_size_ = size; }

    CefSize GetPreferredSize(CefRefPtr<CefView>) override { return preferred_size_; }
    CefSize GetMinimumSize(CefRefPtr<CefView>) override { return minimum_size_; }
    CefSize GetMaximumSize(CefRefPtr<CefView>) override { return maximum_size_; }

  private:
    std::shared_ptr<BrowserChrome*> alive_;
    CefSize preferred_size_;
    CefSize minimum_size_;
    CefSize maximum_size_;

    IMPLEMENT_REFCOUNTING(PanelDelegate);
};

// CefView resets any custom background color when CefViewDelegate::OnThemeChanged is
// called, and CefWindow::ThemeChanged() fires that reset asynchronously after the
// chrome applies its colors. The rail and its panels therefore re-assert their chrome
// surface in OnThemeChanged so the tinted rail survives the reset instead of falling
// back to the window's primary background.
class BrowserChrome::SurfacePanelDelegate final : public CefPanelDelegate {
  public:
    SurfacePanelDelegate(BrowserChrome* chrome, BrowserChrome::SurfaceSlot slot,
                         CefSize preferred_size = CefSize(), CefSize minimum_size = CefSize(),
                         CefSize maximum_size = CefSize())
        : chrome_(chrome),
          slot_(slot),
          preferred_size_(preferred_size),
          minimum_size_(minimum_size),
          maximum_size_(maximum_size) {}

    void Detach() { chrome_ = nullptr; }

    // The rail's fixed width becomes 0 while the sidebar is hidden, so the
    // box layout stops reserving space for it.
    void SetFixedWidth(int width) {
        preferred_size_.width = width;
        minimum_size_.width = width;
        maximum_size_.width = width;
    }

    void SetPreferredSize(CefSize size) { preferred_size_ = size; }
    // Pins a row's height in the box layout; a zero width means unbounded.
    void SetMinimumSize(CefSize size) { minimum_size_ = size; }
    void SetMaximumSize(CefSize size) { maximum_size_ = size; }

    CefSize GetPreferredSize(CefRefPtr<CefView>) override { return preferred_size_; }
    CefSize GetMinimumSize(CefRefPtr<CefView>) override { return minimum_size_; }
    CefSize GetMaximumSize(CefRefPtr<CefView>) override { return maximum_size_; }

    void OnThemeChanged(CefRefPtr<CefView> view) override {
        if (chrome_ != nullptr) {
            view->SetBackgroundColor(chrome_->ResolveSurfaceColor(slot_).argb);
            chrome_->HandleViewThemeReset();
        }
    }

  private:
    BrowserChrome* chrome_;
    BrowserChrome::SurfaceSlot slot_;
    CefSize preferred_size_;
    CefSize minimum_size_;
    CefSize maximum_size_;

    IMPLEMENT_REFCOUNTING(SurfacePanelDelegate);
};

class BrowserChrome::RootPanelDelegate final : public CefPanelDelegate {
  public:
    RootPanelDelegate(BrowserChrome* chrome, ChromeTokens tokens)
        : chrome_(chrome), tokens_(tokens) {}

    void Detach() { chrome_ = nullptr; }

    void SetChildren(CefRefPtr<CefPanel> rail_host, CefRefPtr<PanelDelegate> rail_host_delegate,
                     CefRefPtr<CefPanel> content_column, CefRefPtr<CefPanel> rail,
                     CefRefPtr<CefPanel> collapsed_rail, CefRefPtr<CefPanel> toolbar,
                     CefRefPtr<CefPanel> toolbar_content, CefRefPtr<CefPanel> address_row,
                     CefRefPtr<CefPanel> browser_content,
                     CefRefPtr<PanelDelegate> browser_content_delegate,
                     CefRefPtr<CefPanel> agent_panel,
                     CefRefPtr<PanelDelegate> agent_panel_delegate) {
        rail_host_ = rail_host;
        rail_host_delegate_ = rail_host_delegate;
        content_column_ = content_column;
        rail_ = rail;
        collapsed_rail_ = collapsed_rail;
        toolbar_ = toolbar;
        toolbar_content_ = toolbar_content;
        address_row_ = address_row;
        browser_content_ = browser_content;
        browser_content_delegate_ = browser_content_delegate;
        agent_panel_ = agent_panel;
        agent_panel_delegate_ = agent_panel_delegate;
    }

    void SetTokens(ChromeTokens tokens) {
        tokens_ = tokens;
        applied_geometry_.reset();
    }

    void SetSidebarExpanded(bool expanded) {
        sidebar_expanded_ = expanded;
        applied_geometry_.reset();
    }
    void SetAgentPanelOpen(bool open) {
        agent_panel_open_ = open;
        applied_geometry_.reset();
    }

    void OnLayoutChanged(CefRefPtr<CefView> view, const CefRect& new_bounds) override {
        if (applying_layout_ || rail_host_ == nullptr || rail_ == nullptr ||
            content_column_ == nullptr || browser_content_ == nullptr) {
            return;
        }

        const ChromeGeometrySnapshot geometry =
            BrowserChrome::LayoutForBounds({.x = new_bounds.x,
                                            .y = new_bounds.y,
                                            .width = new_bounds.width,
                                            .height = new_bounds.height},
                                           tokens_, sidebar_expanded_, agent_panel_open_);
        const DipRect column_bounds = ToDipRect(content_column_->GetBounds());
        const auto in_root = [&column_bounds](CefRefPtr<CefPanel> child) {
            if (child == nullptr) {
                return DipRect{};
            }
            const DipRect bounds = ToDipRect(child->GetBounds());
            if (bounds.width == 0 && bounds.height == 0) {
                return DipRect{};
            }
            return DipRect{.x = column_bounds.x + bounds.x,
                           .y = column_bounds.y + bounds.y,
                           .width = bounds.width,
                           .height = bounds.height};
        };
        const ManagedChromeBounds actual = {
            .rail = ToDipRect(rail_host_->GetBounds()),
            .toolbar = in_root(toolbar_),
            .browser_content = in_root(browser_content_),
            .agent_panel =
                agent_panel_ == nullptr ? DipRect{} : ToDipRect(agent_panel_->GetBounds()),
        };
        if (!BrowserChrome::NeedsRelayout(applied_geometry_, geometry, actual)) {
            return;
        }
        // Layout() below can synchronously re-enter this delegate. The explicit guard makes that
        // nested callback a no-op. The root's BoxLayout then owns the geometry; unlike the old
        // default FillLayout, it never stretches every chrome region over the whole window.
        applying_layout_ = true;
        applied_geometry_ = geometry;
        rail_host_delegate_->SetFixedWidth(geometry.rail_bounds.width);
        browser_content_delegate_->SetPreferredSize(
            CefSize(geometry.browser_content_bounds.width, geometry.browser_content_bounds.height));
        agent_panel_delegate_->SetFixedWidth(geometry.agent_panel_bounds.width);

        CefRefPtr<CefPanel> root = view->AsPanel();
        if (tokens_.sidebar_side == SidebarSide::kRight) {
            if (!root->GetChildViewAt(0)->IsSame(agent_panel_)) {
                root->ReorderChildView(agent_panel_, 0);
                root->ReorderChildView(content_column_, 1);
                root->ReorderChildView(rail_host_, 2);
            }
        } else {
            if (!root->GetChildViewAt(0)->IsSame(rail_host_)) {
                root->ReorderChildView(rail_host_, 0);
                root->ReorderChildView(content_column_, 1);
                root->ReorderChildView(agent_panel_, 2);
            }
        }
        root->Layout();
        if (CefRefPtr<CefBoxLayout> column_layout = content_column_->GetLayout()->AsBoxLayout()) {
            // CEF 150 applies the nested column's remaining main-axis space through its leading
            // flex slot. The page's exact preferred size below keeps the toolbar at 52 DIP and
            // gives the page the rest of the column without an ancestor FillLayout.
            column_layout->SetFlexForView(toolbar_, 1);
            column_layout->SetFlexForView(browser_content_, 0);
        }
        content_column_->Layout();
        rail_host_->Layout();
        if (toolbar_ != nullptr && !toolbar_->IsVisible()) {
            ApplyBounds(toolbar_, {});
        }
        if (agent_panel_ != nullptr && !agent_panel_->IsVisible()) {
            ApplyBounds(agent_panel_, geometry.agent_panel_bounds);
        }

        const CefRefPtr<CefPanel>& shown_rail =
            sidebar_expanded_ || collapsed_rail_ == nullptr ? rail_ : collapsed_rail_;
        shown_rail->Layout();
        browser_content_->Layout();
        if (toolbar_ != nullptr) {
            // The leading inset clears the traffic lights when the sidebar does not (right side,
            // or the collapsed rail); the row layout is replaced only when its settings change.
            const ToolbarRowKey key = {geometry.toolbar_leading_inset_dip, tokens_.spacing_2_dip,
                                       tokens_.spacing_3_dip, tokens_.look};
            if (toolbar_content_ != nullptr && key != applied_toolbar_row_) {
                applied_toolbar_row_ = key;
                CefRefPtr<CefBoxLayout> layout = toolbar_content_->SetToBoxLayout(
                    ToolbarRowLayout(tokens_, geometry.toolbar_leading_inset_dip));
                if (address_row_ != nullptr && tokens_.look == LookPreset::kNative) {
                    layout->SetFlexForView(address_row_, 1);
                }
            }
            toolbar_->Layout();
        }
        if (agent_panel_ != nullptr) {
            agent_panel_->Layout();
        }
        applying_layout_ = false;
        if (chrome_ != nullptr) {
            chrome_->RecordRelayout();
        }
    }

  private:
    static void ApplyBounds(CefRefPtr<CefPanel> panel, const DipRect& bounds) {
        const CefRect target(bounds.x, bounds.y, bounds.width, bounds.height);
        if (panel->GetBounds() != target) {
            panel->SetBounds(target);
        }
    }

    struct ToolbarRowKey {
        int leading_inset = -1;
        int spacing_2 = 0;
        int spacing_3 = 0;
        LookPreset look = LookPreset::kArc;

        bool operator==(const ToolbarRowKey&) const = default;
    };

    BrowserChrome* chrome_;
    ChromeTokens tokens_;
    bool sidebar_expanded_ = kSidebarExpandedByDefault;
    bool agent_panel_open_ = false;
    bool applying_layout_ = false;
    std::optional<ChromeGeometrySnapshot> applied_geometry_;
    ToolbarRowKey applied_toolbar_row_;
    CefRefPtr<CefPanel> rail_host_;
    CefRefPtr<PanelDelegate> rail_host_delegate_;
    CefRefPtr<CefPanel> content_column_;
    CefRefPtr<CefPanel> rail_;
    CefRefPtr<CefPanel> collapsed_rail_;
    CefRefPtr<CefPanel> toolbar_;
    CefRefPtr<CefPanel> toolbar_content_;
    CefRefPtr<CefPanel> address_row_;
    CefRefPtr<CefPanel> browser_content_;
    CefRefPtr<PanelDelegate> browser_content_delegate_;
    CefRefPtr<CefPanel> agent_panel_;
    CefRefPtr<PanelDelegate> agent_panel_delegate_;

    IMPLEMENT_REFCOUNTING(RootPanelDelegate);
};

class BrowserChrome::ButtonDelegate final : public CefButtonDelegate {
  public:
    explicit ButtonDelegate(BrowserChrome* chrome) : chrome_(chrome) {}

    void Detach() { chrome_ = nullptr; }

    void OnButtonPressed(CefRefPtr<CefButton> button) override {
        CEF_REQUIRE_UI_THREAD();
        if (chrome_ != nullptr) {
            chrome_->HandleButtonPressed(static_cast<ChromeViewId>(button->GetID()));
        }
    }

    void OnButtonStateChanged(CefRefPtr<CefButton>) override {
        if (chrome_ != nullptr) {
            chrome_->RefreshRowHover();
        }
    }

    void OnThemeChanged(CefRefPtr<CefView>) override {
        if (chrome_ != nullptr) {
            chrome_->HandleViewThemeReset();
        }
    }

  private:
    BrowserChrome* chrome_;

    IMPLEMENT_REFCOUNTING(ButtonDelegate);
};

// Per-entry button delegate for the collection regions. Entry rows share the
// contract ChromeViewIds, so the entry index travels with the delegate instead
// of the view id.
class BrowserChrome::CollectionButtonDelegate final : public CefButtonDelegate {
  public:
    CollectionButtonDelegate(BrowserChrome* chrome, CollectionButtonAction action,
                             std::size_t index)
        : chrome_(chrome), action_(action), index_(index) {}

    void Detach() { chrome_ = nullptr; }

    void OnButtonPressed(CefRefPtr<CefButton>) override {
        CEF_REQUIRE_UI_THREAD();
        if (chrome_ != nullptr) {
            chrome_->HandleCollectionButtonPressed(action_, index_);
        }
    }

    void OnButtonStateChanged(CefRefPtr<CefButton>) override {
        if (chrome_ != nullptr) {
            chrome_->RefreshRowHover();
        }
    }

    void OnThemeChanged(CefRefPtr<CefView>) override {
        if (chrome_ != nullptr) {
            chrome_->HandleViewThemeReset();
        }
    }

  private:
    BrowserChrome* chrome_;
    CollectionButtonAction action_;
    std::size_t index_;

    IMPLEMENT_REFCOUNTING(CollectionButtonDelegate);
};

class BrowserChrome::TextfieldDelegate final : public CefTextfieldDelegate {
  public:
    TextfieldDelegate(BrowserChrome* chrome, CefSize preferred_size)
        : chrome_(chrome), preferred_size_(preferred_size) {}

    void Detach() { chrome_ = nullptr; }
    void SetPreferredSize(CefSize size) { preferred_size_ = size; }

    CefSize GetPreferredSize(CefRefPtr<CefView>) override { return preferred_size_; }

    void OnThemeChanged(CefRefPtr<CefView>) override {
        if (chrome_ != nullptr) {
            chrome_->HandleViewThemeReset();
        }
    }

    bool OnKeyEvent(CefRefPtr<CefTextfield> textfield, const CefKeyEvent& event) override {
        CEF_REQUIRE_UI_THREAD();
        return chrome_ != nullptr && chrome_->HandleAddressKeyEvent(textfield, event);
    }

    void OnAfterUserAction(CefRefPtr<CefTextfield> textfield) override {
        CEF_REQUIRE_UI_THREAD();
        if (chrome_ != nullptr) {
            chrome_->HandleAddressUserAction(textfield);
        }
    }

    void OnFocus(CefRefPtr<CefView>) override {
        CEF_REQUIRE_UI_THREAD();
        if (chrome_ != nullptr) {
            chrome_->HandleAddressFocus();
        }
    }

    void OnBlur(CefRefPtr<CefView>) override {
        CEF_REQUIRE_UI_THREAD();
        if (chrome_ != nullptr) {
            chrome_->HandleAddressBlur();
        }
    }

  private:
    BrowserChrome* chrome_;
    CefSize preferred_size_;

    IMPLEMENT_REFCOUNTING(TextfieldDelegate);
};

BrowserChrome::BrowserChrome(BrowserChromeHost& host, CefRefPtr<CefBrowserView> browser_view,
                             ChromeTokens tokens, std::filesystem::path icon_resource_root)
    : host_(&host),
      tokens_(tokens),
      icon_catalog_(std::move(icon_resource_root)),
      alive_(std::make_shared<BrowserChrome*>(this)),
      browser_view_(browser_view) {
    CEF_REQUIRE_UI_THREAD();

    const int control_height = ControlHeight(tokens_);
    applied_control_height_ = control_height;
    root_delegate_ = new RootPanelDelegate(this, tokens_);
    root_ = CefPanel::CreatePanel(root_delegate_);
    root_->SetID(static_cast<int>(ChromeViewId::kRoot));
    CefBoxLayoutSettings root_settings;
    root_settings.horizontal = static_cast<int>(true);
    root_settings.between_child_spacing = 0;
    root_settings.default_flex = 0;
    root_layout_ = root_->SetToBoxLayout(root_settings);

    // A zero height reads as "no preference" to CEF, which then asks the layout manager: the
    // rail's preferred width followed its tab list's (a scroll view grows its content to fill
    // the viewport, the rail's preferred width followed, and the window-wide result stuck).
    // One DIP of height makes the 286 DIP width authoritative.
    const CefSize rail_size(tokens_.rail_width_dip, 1);
    CefRefPtr<SurfacePanelDelegate> rail_delegate =
        new SurfacePanelDelegate(this, SurfaceSlot::kRail, rail_size, rail_size, rail_size);
    surface_delegates_.push_back(rail_delegate);
    rail_delegate_ = rail_delegate;
    sidebar_ = CefPanel::CreatePanel(rail_delegate);
    sidebar_->SetID(static_cast<int>(ChromeViewId::kRail));
    rail_layout_ = sidebar_->SetToBoxLayout(RailLayout(tokens_));

    CefRefPtr<SurfacePanelDelegate> navigation_row_delegate =
        new SurfacePanelDelegate(this, SurfaceSlot::kRail, CefSize(0, control_height));
    surface_delegates_.push_back(navigation_row_delegate);
    navigation_row_delegate_ = navigation_row_delegate;
    CefRefPtr<CefPanel> navigation_row = CefPanel::CreatePanel(navigation_row_delegate);
    navigation_row->SetID(static_cast<int>(ChromeViewId::kNavigationRow));
    navigation_row_ = navigation_row;
    navigation_row_layout_ = navigation_row->SetToBoxLayout(NavigationRowLayout(tokens_));

    button_delegate_ = new ButtonDelegate(this);
    back_button_ = CefLabelButton::CreateLabelButton(button_delegate_, "");
    back_button_->SetID(static_cast<int>(ChromeViewId::kBack));
    back_button_->SetAccessibleName("Back");
    back_button_->SetTooltipText("Back");
    back_button_->SetFocusable(true);
    back_button_->SetMinimumSize(CefSize(control_height, control_height));
    navigation_row->AddChildView(back_button_);

    forward_button_ = CefLabelButton::CreateLabelButton(button_delegate_, "");
    forward_button_->SetID(static_cast<int>(ChromeViewId::kForward));
    forward_button_->SetAccessibleName("Forward");
    forward_button_->SetTooltipText("Forward");
    forward_button_->SetFocusable(true);
    forward_button_->SetMinimumSize(CefSize(control_height, control_height));
    navigation_row->AddChildView(forward_button_);
    sidebar_->AddChildView(navigation_row);

    CefRefPtr<SurfacePanelDelegate> address_row_delegate =
        new SurfacePanelDelegate(this, SurfaceSlot::kAddressOutline, CefSize(0, control_height));
    surface_delegates_.push_back(address_row_delegate);
    address_row_delegate_ = address_row_delegate;
    CefRefPtr<CefPanel> address_row = CefPanel::CreatePanel(address_row_delegate);
    address_row->SetID(static_cast<int>(ChromeViewId::kAddressRow));
    address_row_ = address_row;
    // The address row is the one integrated pill: it paints the hairline color (the
    // accent while the field has focus) and insets the location glyph, the field,
    // and the reload control, all painted with the surface color, so together they
    // read as a single outlined field.
    address_layout_ = address_row->SetToBoxLayout(OutlinedLayout(kHairlineDip));
    CefRefPtr<CefBoxLayout> address_layout = address_layout_;

    address_location_icon_ = CefLabelButton::CreateLabelButton(button_delegate_, "");
    address_location_icon_->SetID(static_cast<int>(ChromeViewId::kAddressLocationIcon));
    address_location_icon_->SetAccessibleName("Location");
    address_location_icon_->SetTooltipText("Location");
    address_location_icon_->SetFocusable(false);
    address_location_icon_->SetMinimumSize(
        CefSize(control_height - kHairlineDip * 2, control_height - kHairlineDip * 2));
    address_row->AddChildView(address_location_icon_);

    // Native address indicators (spec §3): the shield sits at the left edge, the agent dot at the
    // right. Both stay hidden until SetAddressIndicators has something to show.
    address_shield_button_ = CefLabelButton::CreateLabelButton(button_delegate_, "");
    address_shield_button_->SetID(static_cast<int>(ChromeViewId::kAddressShield));
    address_shield_button_->SetAccessibleName("Trackers blocked");
    address_shield_button_->SetTooltipText("Trackers blocked");
    address_shield_button_->SetFocusable(true);
    address_shield_button_->SetMinimumSize(CefSize(kIndicatorDip, kIndicatorDip));
    address_shield_button_->SetVisible(false);
    address_row->AddChildView(address_shield_button_);

    textfield_delegate_ = new TextfieldDelegate(
        this, CefSize(tokens_.spacing_6_dip * 4, control_height - kHairlineDip * 2));
    address_field_ = CefTextfield::CreateTextfield(textfield_delegate_);
    address_field_->SetID(static_cast<int>(ChromeViewId::kAddress));
    address_field_->SetAccessibleName("Address");
    address_field_->SetPlaceholderText("Enter address");
    address_field_->SetFocusable(true);
    // The native textfield paints its own rounded corners; the surface-colored holder
    // behind it fills those corners so the field reads as one flat, square-edged cell
    // of the pill instead of showing the hairline through four small notches.
    address_field_holder_ = CefPanel::CreatePanel(new PanelDelegate(this, CefSize()));
    {
        CefBoxLayoutSettings holder_settings;
        holder_settings.horizontal = static_cast<int>(true);
        CefRefPtr<CefBoxLayout> holder_layout =
            address_field_holder_->SetToBoxLayout(holder_settings);
        address_field_holder_->AddChildView(address_field_);
        holder_layout->SetFlexForView(address_field_, 1);
        // CEF 150's CefTextfield has no horizontal-alignment API. Native therefore projects the
        // resting address through a centered label and swaps the real field back in for editing.
        address_idle_label_ = CefLabelButton::CreateLabelButton(button_delegate_, "");
        address_idle_label_->SetID(static_cast<int>(ChromeViewId::kAddressIdleLabel));
        address_idle_label_->SetAccessibleName("Address");
        address_idle_label_->SetTooltipText("Edit address");
        address_idle_label_->SetFocusable(true);
        address_idle_label_->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_CENTER);
        address_idle_label_->SetVisible(false);
        address_field_holder_->AddChildView(address_idle_label_);
        holder_layout->SetFlexForView(address_idle_label_, 1);
    }
    address_row->AddChildView(address_field_holder_);
    address_layout->SetFlexForView(address_field_holder_, 1);

    reload_button_ = CefLabelButton::CreateLabelButton(button_delegate_, "");
    reload_button_->SetID(static_cast<int>(ChromeViewId::kReload));
    reload_button_->SetAccessibleName("Reload");
    reload_button_->SetTooltipText("Reload");
    reload_button_->SetFocusable(true);
    reload_button_->SetMinimumSize(
        CefSize(control_height - kHairlineDip * 2, control_height - kHairlineDip * 2));
    address_row->AddChildView(reload_button_);

    // Reader view toggle: a quiet "Aa" cell at the end of the pill (no icon asset is needed, and
    // the glyph is what every reader mode uses). It is disabled on pages that are not web pages.
    reader_button_ = CefLabelButton::CreateLabelButton(button_delegate_, "Aa");
    reader_button_->SetID(static_cast<int>(ChromeViewId::kReaderButton));
    reader_button_->SetAccessibleName("Reader view");
    reader_button_->SetTooltipText("Reader view");
    reader_button_->SetFocusable(true);
    reader_button_->SetEnabled(false);
    reader_button_->SetMinimumSize(
        CefSize(control_height - kHairlineDip * 2, control_height - kHairlineDip * 2));
    address_row->AddChildView(reader_button_);

    address_agent_dot_ = CefLabelButton::CreateLabelButton(button_delegate_, "");
    address_agent_dot_->SetID(static_cast<int>(ChromeViewId::kAddressAgentDot));
    address_agent_dot_->SetAccessibleName("Agent");
    address_agent_dot_->SetTooltipText("Agent");
    address_agent_dot_->SetFocusable(true);
    address_agent_dot_->SetMinimumSize(CefSize(kIndicatorDip, kIndicatorDip));
    address_agent_dot_->SetVisible(false);
    address_row->AddChildView(address_agent_dot_);
    sidebar_->AddChildView(address_row);

    validation_message_ = CefLabelButton::CreateLabelButton(button_delegate_, "");
    validation_message_->SetID(static_cast<int>(ChromeViewId::kValidationMessage));
    validation_message_->SetFocusable(false);
    validation_message_->SetEnabled(false);
    validation_message_->SetVisible(false);
    sidebar_->AddChildView(validation_message_);

    // Arc-style "New Tab" row: a quiet full-width affordance above the list.
    new_tab_button_ = CefLabelButton::CreateLabelButton(button_delegate_, "New Tab");
    new_tab_button_->SetID(static_cast<int>(ChromeViewId::kRailNewTab));
    new_tab_button_->SetAccessibleName("New Tab");
    new_tab_button_->SetTooltipText("New Tab");
    new_tab_button_->SetFocusable(true);
    new_tab_button_->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_LEFT);
    new_tab_button_->SetMinimumSize(CefSize(0, RowHeight()));
    sidebar_->AddChildView(new_tab_button_);

    // The tab list scrolls inside the rail so a long list never pushes the
    // space bar and footer out of the window. The scroll view is a layout
    // wrapper only: kTabStrip stays the rail's contract child.
    tab_strip_delegate_ = new SurfacePanelDelegate(this, SurfaceSlot::kRail);
    surface_delegates_.push_back(tab_strip_delegate_);
    tab_strip_ = CefPanel::CreatePanel(tab_strip_delegate_);
    tab_strip_->SetID(static_cast<int>(ChromeViewId::kTabStrip));
    tab_strip_->SetToBoxLayout(TabListLayout(tokens_));
    CefRefPtr<SurfacePanelDelegate> tab_list_scroll_delegate =
        new SurfacePanelDelegate(this, SurfaceSlot::kRail, CefSize(0, 0), CefSize(0, 0));
    surface_delegates_.push_back(tab_list_scroll_delegate);
    CefRefPtr<CefScrollView> tab_list_scroll =
        CefScrollView::CreateScrollView(tab_list_scroll_delegate);
    tab_list_scroll->SetContentView(tab_strip_);
    tab_list_scroll_ = tab_list_scroll;
    sidebar_->AddChildView(tab_list_scroll);
    rail_layout_->SetFlexForView(tab_list_scroll, 1);
    UpdateTabStripPreferredSize();

    // The spacer keeps its contract slot but is inert: the scrolling tab list
    // takes the flex, so the bottom group (divider, space bar, footer) always
    // sits at the bottom of the rail.
    CefRefPtr<SurfacePanelDelegate> spacer_delegate =
        new SurfacePanelDelegate(this, SurfaceSlot::kRail);
    surface_delegates_.push_back(spacer_delegate);
    CefRefPtr<CefPanel> spacer = CefPanel::CreatePanel(spacer_delegate);
    spacer->SetID(static_cast<int>(ChromeViewId::kSpacer));
    spacer_ = spacer;
    sidebar_->AddChildView(spacer);

    CefRefPtr<CefPanel> divider = CefPanel::CreatePanel(
        new PanelDelegate(this, CefSize(tokens_.rail_width_dip, DividerHeight(tokens_))));
    divider->SetID(static_cast<int>(ChromeViewId::kDivider));
    divider_ = divider;
    sidebar_->AddChildView(divider);

    // The space bar: one horizontal row of pills, Arc's bottom switcher.
    CefRefPtr<SurfacePanelDelegate> space_switcher_delegate =
        new SurfacePanelDelegate(this, SurfaceSlot::kRail);
    surface_delegates_.push_back(space_switcher_delegate);
    space_switcher_ = CefPanel::CreatePanel(space_switcher_delegate);
    space_switcher_->SetID(static_cast<int>(ChromeViewId::kSpaceSwitcher));
    {
        CefBoxLayoutSettings settings = CollectionLayout(tokens_);
        settings.horizontal = static_cast<int>(true);
        settings.between_child_spacing = tokens_.spacing_1_dip;
        space_switcher_->SetToBoxLayout(settings);
    }
    sidebar_->AddChildView(space_switcher_);

    // Native's bottom switcher row: the segment track, then New Space, overview and settings as
    // 24 DIP buttons. ApplyLookStructure parents it inside the footer for Native only.
    CefRefPtr<SurfacePanelDelegate> switcher_row_delegate =
        new SurfacePanelDelegate(this, SurfaceSlot::kRail);
    surface_delegates_.push_back(switcher_row_delegate);
    switcher_row_ = CefPanel::CreatePanel(switcher_row_delegate);
    switcher_row_->SetID(static_cast<int>(ChromeViewId::kRailSwitcherRow));
    CefBoxLayoutSettings switcher_row_settings;
    switcher_row_settings.horizontal = static_cast<int>(true);
    switcher_row_settings.between_child_spacing = tokens_.spacing_1_dip;
    switcher_row_->SetToBoxLayout(switcher_row_settings);

    // Footer: the agent toggle on the left, New Space on the right.
    CefRefPtr<SurfacePanelDelegate> footer_delegate =
        new SurfacePanelDelegate(this, SurfaceSlot::kRail);
    surface_delegates_.push_back(footer_delegate);
    rail_footer_delegate_ = footer_delegate;
    rail_footer_ = CefPanel::CreatePanel(footer_delegate);
    rail_footer_->SetID(static_cast<int>(ChromeViewId::kRailFooter));
    CefBoxLayoutSettings footer_settings;
    footer_settings.horizontal = static_cast<int>(true);
    footer_settings.between_child_spacing = tokens_.spacing_1_dip / 2;
    CefRefPtr<CefBoxLayout> footer_layout = rail_footer_->SetToBoxLayout(footer_settings);
    agent_button_ = CefLabelButton::CreateLabelButton(button_delegate_, "Agent");
    agent_button_->SetID(static_cast<int>(ChromeViewId::kRailAgentButton));
    agent_button_->SetAccessibleName("Agent panel");
    agent_button_->SetTooltipText("Ask the agent (Ctrl/Cmd+J)");
    agent_button_->SetFocusable(true);
    agent_button_->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_LEFT);
    agent_button_->SetMinimumSize(CefSize(0, RowHeight()));
    rail_footer_->AddChildView(agent_button_);
    footer_layout->SetFlexForView(agent_button_, 1);
    tabs_button_ = CefLabelButton::CreateLabelButton(button_delegate_, "");
    tabs_button_->SetID(static_cast<int>(ChromeViewId::kRailTabsButton));
    tabs_button_->SetAccessibleName("All tabs");
    tabs_button_->SetTooltipText("All tabs");
    tabs_button_->SetFocusable(true);
    tabs_button_->SetMinimumSize(CefSize(RowHeight(), RowHeight()));
    rail_footer_->AddChildView(tabs_button_);
    settings_button_ = CefLabelButton::CreateLabelButton(button_delegate_, "");
    settings_button_->SetID(static_cast<int>(ChromeViewId::kRailSettingsButton));
    settings_button_->SetAccessibleName("Settings");
    settings_button_->SetTooltipText("Settings");
    settings_button_->SetFocusable(true);
    settings_button_->SetMinimumSize(CefSize(RowHeight(), RowHeight()));
    rail_footer_->AddChildView(settings_button_);
    new_space_button_ = CefLabelButton::CreateLabelButton(button_delegate_, "");
    new_space_button_->SetID(static_cast<int>(ChromeViewId::kRailNewSpaceButton));
    new_space_button_->SetAccessibleName("New Space");
    new_space_button_->SetTooltipText("New Space");
    new_space_button_->SetFocusable(true);
    new_space_button_->SetMinimumSize(CefSize(RowHeight(), RowHeight()));
    rail_footer_->AddChildView(new_space_button_);
    sidebar_->AddChildView(rail_footer_);

    // The active-page card is a compact pill: a bounded single-row height so it reads
    // as a raised chip against the tinted rail, not a full-height stretched row.
    CefRefPtr<SurfacePanelDelegate> active_page_delegate = new SurfacePanelDelegate(
        this, SurfaceSlot::kActivePage, CefSize(0, control_height + tokens_.spacing_2_dip),
        CefSize(0, 0), CefSize(tokens_.rail_width_dip, control_height + tokens_.spacing_2_dip));
    surface_delegates_.push_back(active_page_delegate);
    active_page_ = CefPanel::CreatePanel(active_page_delegate);
    active_page_->SetID(static_cast<int>(ChromeViewId::kActivePage));
    CefRefPtr<CefBoxLayout> active_page_layout =
        active_page_->SetToBoxLayout(HorizontalLayout(tokens_));

    active_page_fallback_favicon_ = CefLabelButton::CreateLabelButton(button_delegate_, "");
    active_page_fallback_favicon_->SetID(
        static_cast<int>(ChromeViewId::kActivePageFallbackFavicon));
    active_page_fallback_favicon_->SetAccessibleName("Current page favicon placeholder");
    active_page_fallback_favicon_->SetTooltipText("Current page favicon placeholder");
    active_page_fallback_favicon_->SetFocusable(false);
    active_page_fallback_favicon_->SetMinimumSize(CefSize(control_height, control_height));
    active_page_->AddChildView(active_page_fallback_favicon_);

    active_tab_ = CefLabelButton::CreateLabelButton(button_delegate_, "Island");
    active_tab_->SetID(static_cast<int>(ChromeViewId::kActiveTab));
    active_tab_->SetAccessibleName("Current page");
    active_tab_->SetTooltipText("Focus current page");
    active_tab_->SetFocusable(true);
    active_tab_->SetMinimumSize(CefSize(tokens_.spacing_6_dip * 4, control_height));
    active_page_->AddChildView(active_tab_);
    active_page_layout->SetFlexForView(active_tab_, 1);

    active_page_indicator_ = CefPanel::CreatePanel(
        new PanelDelegate(this, CefSize(ActivePageIndicatorWidthDip(), control_height)));
    active_page_indicator_->SetID(static_cast<int>(ChromeViewId::kActivePageIndicator));
    active_page_->AddChildView(active_page_indicator_);
    // Arc's sidebar has no separate current-page card: the active tab row is
    // the raised card. The node keeps its contract slot (and its accessible
    // focus target) but takes no space in the rail.
    active_page_->SetVisible(false);
    sidebar_->AddChildView(active_page_);

    // The collapsed icon rail: same column as the sidebar, shown instead of it. Every part
    // is a surface-slot panel so a theme reset repaints it like the rest of the rail.
    const CefSize collapsed_size(kCollapsedRailWidthDip, 1);
    collapsed_rail_delegate_ = new SurfacePanelDelegate(this, SurfaceSlot::kRail, collapsed_size,
                                                        collapsed_size, collapsed_size);
    surface_delegates_.push_back(collapsed_rail_delegate_);
    collapsed_rail_ = CefPanel::CreatePanel(collapsed_rail_delegate_);
    collapsed_rail_->SetID(static_cast<int>(ChromeViewId::kCollapsedRail));
    CefBoxLayoutSettings collapsed_settings;
    collapsed_settings.horizontal = static_cast<int>(false);
    collapsed_settings.between_child_spacing = RailSectionSpacingDip();
    collapsed_settings.inside_border_insets =
        CefInsets(CollapsedRailTopInset(tokens_), 0, tokens_.spacing_3_dip, 0);
    collapsed_layout_ = collapsed_rail_->SetToBoxLayout(collapsed_settings);

    const auto make_surface_panel = [this](SurfaceSlot slot, CefSize preferred, int id) {
        CefRefPtr<SurfacePanelDelegate> delegate =
            new SurfacePanelDelegate(this, slot, preferred, preferred, preferred);
        surface_delegates_.push_back(delegate);
        CefRefPtr<CefPanel> panel = CefPanel::CreatePanel(delegate);
        panel->SetID(id);
        return panel;
    };

    // The tab list scrolls like the expanded one: the scroll view is a layout wrapper and
    // the list inside it keeps the buttons centered whatever width the viewport leaves.
    collapsed_tab_list_delegate_ = new SurfacePanelDelegate(this, SurfaceSlot::kRail);
    surface_delegates_.push_back(collapsed_tab_list_delegate_);
    collapsed_tab_list_ = CefPanel::CreatePanel(collapsed_tab_list_delegate_);
    collapsed_tab_list_->SetID(static_cast<int>(ChromeViewId::kCollapsedRailTabList));
    collapsed_tab_list_->SetToBoxLayout(CollapsedColumnLayout(kCollapsedRailButtonSpacingDip));
    CefRefPtr<SurfacePanelDelegate> collapsed_scroll_delegate =
        new SurfacePanelDelegate(this, SurfaceSlot::kRail, CefSize(0, 0), CefSize(0, 0));
    surface_delegates_.push_back(collapsed_scroll_delegate);
    CefRefPtr<CefScrollView> collapsed_scroll =
        CefScrollView::CreateScrollView(collapsed_scroll_delegate);
    collapsed_scroll->SetContentView(collapsed_tab_list_);
    collapsed_tab_scroll_ = collapsed_scroll;
    collapsed_rail_->AddChildView(collapsed_scroll);
    collapsed_layout_->SetFlexForView(collapsed_scroll, 1);

    // Bottom group: the "+" button, a hairline, then the space dots.
    collapsed_bottom_group_ = make_surface_panel(SurfaceSlot::kRail, CefSize(), /*id=*/0);
    collapsed_bottom_group_->SetToBoxLayout(CollapsedColumnLayout(kCollapsedRailButtonSpacingDip));
    collapsed_new_tab_button_ = CefLabelButton::CreateLabelButton(button_delegate_, "");
    collapsed_new_tab_button_->SetID(static_cast<int>(ChromeViewId::kCollapsedRailNewTab));
    collapsed_new_tab_button_->SetAccessibleName("New Tab");
    collapsed_new_tab_button_->SetTooltipText("New Tab");
    collapsed_new_tab_button_->SetFocusable(true);
    collapsed_new_tab_button_->SetMinimumSize(
        CefSize(kCollapsedRailButtonDip, kCollapsedRailButtonDip));
    collapsed_bottom_group_->AddChildView(collapsed_new_tab_button_);
    collapsed_divider_wrapper_ = make_surface_panel(
        SurfaceSlot::kRail, CefSize(kCollapsedRailButtonDip, kCollapsedRailSeparatorHeightDip),
        static_cast<int>(ChromeViewId::kCollapsedRailSeparator));
    collapsed_divider_wrapper_->SetToBoxLayout(CenteredLayout());
    collapsed_divider_ = make_surface_panel(
        SurfaceSlot::kHairline, CefSize(kCollapsedRailSeparatorWidthDip, DividerHeight(tokens_)),
        static_cast<int>(ChromeViewId::kCollapsedRailSeparator));
    collapsed_divider_wrapper_->AddChildView(collapsed_divider_);
    collapsed_bottom_group_->AddChildView(collapsed_divider_wrapper_);
    collapsed_space_list_ = make_surface_panel(
        SurfaceSlot::kRail, CefSize(), static_cast<int>(ChromeViewId::kCollapsedRailSpaceList));
    collapsed_space_list_->SetToBoxLayout(CollapsedColumnLayout(0));
    collapsed_bottom_group_->AddChildView(collapsed_space_list_);
    collapsed_rail_->AddChildView(collapsed_bottom_group_);

    browser_content_delegate_ = new PanelDelegate(this, CefSize(control_height, control_height),
                                                  CefSize(control_height, control_height));
    browser_content_ = CefPanel::CreatePanel(browser_content_delegate_);
    browser_content_->SetID(static_cast<int>(ChromeViewId::kBrowserContent));
    browser_content_layout_ = browser_content_->SetToBoxLayout(FloatingCanvasLayout(ContentInsets(
        {.x = 0, .y = 0, .width = 1200, .height = 800}, tokens_, sidebar_expanded_, false)));
    browser_view_->SetID(static_cast<int>(ChromeViewId::kBrowserView));
    browser_content_->AddChildView(browser_view_);
    browser_content_layout_->SetFlexForView(browser_view_, 1);

    // The agent panel column floats on the same canvas as the page card: the
    // gutter on its top, right, and bottom matches the content gutter, and the
    // content's own right gutter separates the two.
    agent_panel_delegate_ = new PanelDelegate(this, CefSize(0, 0));
    agent_panel_ = CefPanel::CreatePanel(agent_panel_delegate_);
    agent_panel_->SetID(static_cast<int>(ChromeViewId::kAgentPanel));
    {
        const CardInsets insets = AgentPanelInsets({.x = 0, .y = 0, .width = 1200, .height = 800},
                                                   tokens_, sidebar_expanded_);
        CefBoxLayoutSettings settings;
        settings.horizontal = static_cast<int>(true);
        settings.inside_border_insets =
            CefInsets(insets.top, insets.left, insets.bottom, insets.right);
        agent_panel_layout_ = agent_panel_->SetToBoxLayout(settings);
    }
    agent_panel_->SetVisible(false);

    // Native puts navigation in a 52 DIP toolbar above the page. Arc and Graphite keep the
    // controls in the sidebar; ApplyLookStructure reparents the same views when the preset
    // changes so their state, delegates and stable IDs are never duplicated.
    const CefSize toolbar_size(0, NativeToolbarHeightDip());
    CefRefPtr<SurfacePanelDelegate> toolbar_delegate = new SurfacePanelDelegate(
        this, SurfaceSlot::kToolbar, toolbar_size, toolbar_size, toolbar_size);
    surface_delegates_.push_back(toolbar_delegate);
    toolbar_ = CefPanel::CreatePanel(toolbar_delegate);
    toolbar_->SetID(static_cast<int>(ChromeViewId::kToolbar));
    CefBoxLayoutSettings toolbar_stack;
    toolbar_stack.horizontal = static_cast<int>(false);
    toolbar_stack.between_child_spacing = 0;
    const int toolbar_content_inset = NativeToolbarHeightDip() - NativeAddressHeightDip() - 1;
    toolbar_stack.inside_border_insets = CefInsets(
        toolbar_content_inset / 2, 0, toolbar_content_inset - toolbar_content_inset / 2, 0);
    CefRefPtr<CefBoxLayout> toolbar_stack_layout = toolbar_->SetToBoxLayout(toolbar_stack);

    toolbar_content_ = CefPanel::CreatePanel(new PanelDelegate(this, CefSize()));
    const CefBoxLayoutSettings toolbar_row = ToolbarRowLayout(tokens_, tokens_.spacing_3_dip);
    toolbar_content_->SetToBoxLayout(toolbar_row);

    share_button_ = CefLabelButton::CreateLabelButton(button_delegate_, "Share");
    share_button_->SetID(static_cast<int>(ChromeViewId::kToolbarShare));
    share_button_->SetAccessibleName("Copy tab URL");
    share_button_->SetTooltipText("Copy tab URL");
    share_button_->SetFocusable(true);
    share_button_->SetMinimumSize(CefSize(52, NativeToolbarButtonDip()));
    toolbar_content_->AddChildView(share_button_);

    toolbar_->AddChildView(toolbar_content_);
    toolbar_stack_layout->SetFlexForView(toolbar_content_, 1);

    CefRefPtr<SurfacePanelDelegate> toolbar_hairline_delegate =
        new SurfacePanelDelegate(this, SurfaceSlot::kHairline, CefSize(0, kHairlineDip),
                                 CefSize(0, kHairlineDip), CefSize(0, kHairlineDip));
    surface_delegates_.push_back(toolbar_hairline_delegate);
    toolbar_hairline_ = CefPanel::CreatePanel(toolbar_hairline_delegate);
    toolbar_->AddChildView(toolbar_hairline_);
    toolbar_->SetVisible(false);

    // The root uses one horizontal BoxLayout instead of the default FillLayout. Its middle
    // column owns the toolbar and page vertically, so no layout pass can stretch those regions
    // over the sidebar. The rail host still isolates the scroll view's preferred width.
    rail_host_delegate_ = new PanelDelegate(this, CefSize(tokens_.rail_width_dip, 0));
    rail_host_delegate_->SetFixedWidth(tokens_.rail_width_dip);
    rail_host_ = CefPanel::CreatePanel(rail_host_delegate_);
    {
        CefBoxLayoutSettings host_settings;
        host_settings.horizontal = static_cast<int>(true);
        host_settings.between_child_spacing = 0;
        rail_host_->SetToBoxLayout(host_settings);
    }
    rail_host_->AddChildView(sidebar_);
    rail_host_->AddChildView(collapsed_rail_);

    content_column_ = CefPanel::CreatePanel(new PanelDelegate(this, CefSize()));
    CefBoxLayoutSettings content_column_settings;
    content_column_settings.horizontal = static_cast<int>(false);
    content_column_settings.between_child_spacing = 0;
    content_column_settings.default_flex = 0;
    content_column_layout_ = content_column_->SetToBoxLayout(content_column_settings);
    content_column_->AddChildView(toolbar_);
    content_column_->AddChildView(browser_content_);
    content_column_layout_->SetFlexForView(toolbar_, 1);
    content_column_layout_->SetFlexForView(browser_content_, 0);

    root_->AddChildView(rail_host_);
    root_->AddChildView(content_column_);
    root_->AddChildView(agent_panel_);
    root_layout_->SetFlexForView(content_column_, 1);
    root_delegate_->SetChildren(rail_host_, rail_host_delegate_, content_column_, sidebar_,
                                collapsed_rail_, toolbar_, toolbar_content_, address_row_,
                                browser_content_, browser_content_delegate_, agent_panel_,
                                agent_panel_delegate_);
    ApplyLookStructure();
    ApplyControlTheme();
    // Apply the expand state before the first layout so no frame ever shows a rail the
    // sidebar state says is not on screen.
    SetSidebarExpanded(sidebar_expanded_);
}

BrowserChrome::~BrowserChrome() { Detach(); }

CefRefPtr<CefPanel> BrowserChrome::root() const {
    CEF_REQUIRE_UI_THREAD();
    return root_;
}

CefRefPtr<CefPanel> BrowserChrome::sidebar() const {
    CEF_REQUIRE_UI_THREAD();
    return sidebar_;
}

std::vector<CefRefPtr<CefBrowserView>> BrowserChrome::visible_browser_views() const {
    std::vector<CefRefPtr<CefBrowserView>> views;
    if (browser_view_ != nullptr) {
        views.push_back(browser_view_);
    }
    if (split_view_ != nullptr) {
        views.push_back(split_view_);
    }
    return views;
}

void BrowserChrome::RecordRelayout() {
    ++relayout_count_;
    std::vector<CefRect> bounds;
    for (const CefRefPtr<CefBrowserView>& view : visible_browser_views()) {
        bounds.push_back(view->GetBounds());
    }
    if (!last_browser_view_bounds_.empty() && bounds != last_browser_view_bounds_) {
        ++browser_view_size_change_count_;
    }
    last_browser_view_bounds_ = std::move(bounds);
}

ChromeViewTreeNode BrowserChrome::view_tree_snapshot() const {
    // The live projection: fixed regions from the contract, collection entries
    // from the snapshots the window last pushed, so the snapshot always matches
    // the rows actually in the view tree.
    return LiveViewTree(tab_entries_, space_entries_, tokens_.look, split_view_ != nullptr);
}

ChromeViewTreeNode BrowserChrome::LiveViewTree(
    const std::vector<TabStripEntrySnapshot>& tabs,
    const std::vector<SpaceSwitcherEntrySnapshot>& spaces, LookPreset look, bool split_active) {
    ChromeViewTreeNode tree = CollectionCountContract(tabs, spaces, look);
    if (!split_active) {
        return tree;
    }
    // An active split extends kBrowserContent with the divider and the second pane after the
    // first. Native puts kToolbar before it, so the region is found by id, not by index.
    for (ChromeViewTreeNode& region : tree.children) {
        if (region.id == ChromeViewId::kBrowserContent) {
            region.children.push_back({ChromeViewId::kSplitDivider, {}});
            region.children.push_back({ChromeViewId::kBrowserView, {}});
        }
    }
    return tree;
}

ChromeGeometrySnapshot BrowserChrome::view_bounds_snapshot() const {
    CEF_REQUIRE_UI_THREAD();
    return {
        .root_bounds = ToDipRect(root_->GetBounds()),
        .rail_bounds = ToDipRect((sidebar_expanded_ ? sidebar_ : collapsed_rail_)->GetBounds()),
        .browser_content_bounds = ToDipRect(browser_content_->GetBounds()),
        .browser_view_bounds = ToDipRect(browser_view_->GetBounds()),
    };
}

void BrowserChrome::SetReaderState(bool available, bool active) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || (available == reader_available_ && active == reader_active_)) {
        return;
    }
    reader_available_ = available;
    reader_active_ = active;
    reader_button_->SetEnabled(available);
    reader_button_->SetEnabledTextColors((active ? tokens_.accent : tokens_.text_secondary).argb);
    reader_button_->SetTooltipText(active ? "Exit reader view" : "Reader view");
    reader_button_->SetAccessibleName(active ? "Exit reader view" : "Reader view");
    ApplyAddressIndicators();
}

void BrowserChrome::SetAddressIndicators(const AddressIndicators& indicators) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || indicators == address_indicators_) {
        return;
    }
    address_indicators_ = indicators;
    ApplyAddressIndicators();
}

// Native shows an indicator only while it has something to say; the other looks keep the always
// present Reader cell and have no shield or dot.
void BrowserChrome::ApplyAddressIndicators() {
    const bool native = tokens_.look == LookPreset::kNative;
    const AddressIndicators& indicators = address_indicators_;
    const ArgbColor well = AddressPlateStyleFor(tokens_).fill;

    const bool shield = native && indicators.show_shield;
    address_shield_button_->SetVisible(shield);
    if (shield) {
        address_shield_button_->SetText(indicators.shield_label);
        address_shield_button_->SetTooltipText(indicators.shield_tooltip);
        address_shield_button_->SetAccessibleName(indicators.shield_tooltip);
        SetAllStateImages(address_shield_button_, MakeShieldImage(tokens_.text_secondary));
        address_shield_button_->SetEnabledTextColors(tokens_.text_secondary.argb);
        address_shield_button_->SetFontList(FontList(11, ChromeFontWeight::kMedium));
        address_shield_button_->SetBackgroundColor(well.argb);
    }

    reader_button_->SetVisible(!native || indicators.show_reader);

    const bool dot = native && indicators.agent != AgentTabState::kIdle;
    address_agent_dot_->SetVisible(dot);
    if (dot) {
        const bool waiting = indicators.agent == AgentTabState::kWaitingForYou;
        const char* name = waiting ? "Agent is waiting for you" : "Agent is reading this tab";
        SetAllStateImages(address_agent_dot_,
                          MakeAgentDotImage(waiting ? tokens_.warning_text : tokens_.accent));
        address_agent_dot_->SetTooltipText(name);
        address_agent_dot_->SetAccessibleName(name);
        address_agent_dot_->SetBackgroundColor(well.argb);
    }
}

AddressSelectionSnapshot BrowserChrome::address_selection_snapshot() const {
    CEF_REQUIRE_UI_THREAD();
    return {
        .has_focus = address_field_->HasFocus(),
        .has_selection = address_field_->HasSelection(),
    };
}

std::string BrowserChrome::TruncateCollectionTitle(std::string title) {
    const std::size_t limit = CollectionTitleCharacterLimit();
    std::size_t characters = 0;
    std::size_t index = 0;
    while (index < title.size()) {
        index = AdvanceUtf8Character(title, index);
        ++characters;
    }
    if (characters <= limit) {
        return title;
    }
    // Keep the first limit-1 characters and append an ellipsis so the displayed
    // text never exceeds the limit and never splits a UTF-8 sequence. The
    // accessible names below still carry the full untruncated text.
    constexpr std::string_view kEllipsis = "\xE2\x80\xA6";
    std::size_t cut = 0;
    for (std::size_t kept = 1; kept < limit; ++kept) {
        cut = AdvanceUtf8Character(title, cut);
    }
    return title.substr(0, cut) + std::string(kEllipsis);
}

std::string BrowserChrome::TabEntryAccessibleName(const TabStripEntrySnapshot& entry) {
    return "Tab: " + (entry.group_label.empty() ? std::string() : entry.group_label + ", ") +
           entry.title + (entry.active ? ", active" : ", inactive") +
           (entry.sleeping ? ", sleeping" : "");
}

std::string BrowserChrome::TabCloseAccessibleName(const TabStripEntrySnapshot& entry) {
    return "Close tab: " + entry.title;
}

std::string BrowserChrome::SpaceEntryAccessibleName(const SpaceSwitcherEntrySnapshot& entry) {
    return "Space: " + entry.name + (entry.active ? ", active" : ", inactive");
}

CollapsedRailTabLayout BrowserChrome::CollapsedRailLayout(
    const std::vector<TabStripEntrySnapshot>& tabs) {
    CollapsedRailTabLayout layout;
    layout.order.reserve(tabs.size());
    for (std::size_t index = 0; index < tabs.size(); ++index) {
        if (tabs[index].pinned) {
            layout.order.push_back(index);
        }
    }
    const std::size_t pinned = layout.order.size();
    for (std::size_t index = 0; index < tabs.size(); ++index) {
        if (!tabs[index].pinned) {
            layout.order.push_back(index);
        }
    }
    if (pinned > 0 && pinned < tabs.size()) {
        layout.separator_before = pinned;
    }
    return layout;
}

std::string BrowserChrome::CollapsedRailTabTooltip(const TabStripEntrySnapshot& entry) {
    return entry.sleeping ? entry.title + " (sleeping)" : entry.title;
}

std::string BrowserChrome::CollapsedRailSpaceTooltip(const SpaceSwitcherEntrySnapshot& entry) {
    return entry.name;
}

std::string BrowserChrome::SpacePillLabel(const SpaceSwitcherEntrySnapshot& entry) {
    if (entry.active) {
        return TruncateCollectionTitle(entry.name);
    }
    if (entry.name.empty()) {
        return "?";
    }
    // The first character (a whole UTF-8 sequence), upper-cased when ASCII.
    std::string initial = entry.name.substr(0, AdvanceUtf8Character(entry.name, 0));
    if (initial.size() == 1 && initial[0] >= 'a' && initial[0] <= 'z') {
        initial[0] = static_cast<char>(initial[0] - 'a' + 'A');
    }
    return initial;
}

std::string BrowserChrome::ActiveSpaceSectionTitle() const {
    for (const SpaceSwitcherEntrySnapshot& entry : space_entries_) {
        if (entry.active && !entry.name.empty()) {
            return TruncateCollectionTitle(entry.name) + " \xC2\xB7 Tabs";
        }
    }
    return "Tabs";
}

void BrowserChrome::UpdateTabStripPreferredSize() {
    // Native's New Tab row is a row of the list too.
    const int count =
        static_cast<int>(tab_entries_.size()) + (tokens_.look == LookPreset::kNative ? 1 : 0);
    const int headers =
        (favorites_section_header_ != nullptr ? 1 : 0) + (tabs_section_header_ != nullptr ? 1 : 0);
    const int kickers = static_cast<int>(group_kickers_.size());
    const int spacing = TabListLayout(tokens_).between_child_spacing;
    const int children = count + headers + kickers;
    const int height = count * RowHeight() + headers * NativeSectionHeaderHeightDip() +
                       kickers * GroupedTabKickerHeightDip() + std::max(0, children - 1) * spacing;
    const int width =
        std::max(0, tokens_.rail_width_dip - 2 * tokens_.spacing_3_dip - TabListScrollGutterDip());
    if (tab_strip_delegate_ != nullptr) {
        tab_strip_delegate_->SetPreferredSize(CefSize(width, height));
    }
    if (tab_strip_ != nullptr) {
        tab_strip_->SetSize(CefSize(width, height));
        tab_strip_->Layout();
    }
}

void BrowserChrome::SetTabStripEntries(const std::vector<TabStripEntrySnapshot>& entries) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    for (const CefRefPtr<CollectionButtonDelegate>& delegate : tab_button_delegates_) {
        delegate->Detach();
    }
    tab_button_delegates_.clear();
    tab_entry_views_.clear();
    tab_entries_ = entries;
    group_kicker_panels_.clear();
    group_kickers_.clear();
    favorites_section_header_ = nullptr;
    tabs_section_header_ = nullptr;
    tab_strip_->RemoveAllChildViews();

    const auto add_section_header = [this](std::string text, ChromeViewId id) {
        CefRefPtr<CefLabelButton> header =
            CefLabelButton::CreateLabelButton(button_delegate_, std::move(text));
        header->SetID(static_cast<int>(id));
        header->SetFocusable(false);
        header->SetInkDropEnabled(false);
        header->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_LEFT);
        header->SetMinimumSize(CefSize(0, NativeSectionHeaderHeightDip()));
        header->SetAccessibleName(header->GetText().ToString() + " section");
        header->SetFontList(SectionHeaderFontList(tokens_));
        tab_strip_->AddChildView(header);
        return header;
    };
    const bool native = tokens_.look == LookPreset::kNative;
    const bool has_favorites =
        native &&
        std::any_of(entries.begin(), entries.end(), [](const auto& entry) { return entry.pinned; });
    if (has_favorites) {
        favorites_section_header_ =
            add_section_header("Favorites", ChromeViewId::kFavoritesSectionHeader);
    }
    bool tabs_header_added = false;

    const int row_height = RowHeight();
    const std::optional<CefRefPtr<CefImage>> fallback_icon =
        icon_catalog_.Load(ChromeIcon::kLocation, FallbackFaviconIconTone(), ChromeIconSize::k16);
    const std::optional<CefRefPtr<CefImage>> close_icon =
        icon_catalog_.Load(ChromeIcon::kClose, ChromeIconTone::kSecondary, ChromeIconSize::k13);
    const std::optional<CefRefPtr<CefImage>> pin_icon =
        icon_catalog_.Load(ChromeIcon::kPin, ChromeIconTone::kSecondary, ChromeIconSize::k13);
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const TabStripEntrySnapshot& entry = entries[index];
        if (native && !entry.pinned && !tabs_header_added) {
            tabs_section_header_ =
                add_section_header(ActiveSpaceSectionTitle(), ChromeViewId::kTabsSectionHeader);
            tabs_header_added = true;
        }
        if (native && NeedsGroupKicker(entries, index)) {
            // The branch label is its own line above the run, indented like the child rows.
            CefRefPtr<CefPanel> panel = CefPanel::CreatePanel(new PanelDelegate(this, CefSize()));
            panel->SetID(static_cast<int>(ChromeViewId::kTabGroupKicker));
            CefBoxLayoutSettings panel_settings;
            panel_settings.horizontal = static_cast<int>(true);
            panel_settings.between_child_spacing = 0;
            panel_settings.inside_border_insets = CefInsets(0, GroupedTabIndentDip(), 0, 0);
            CefRefPtr<CefBoxLayout> panel_layout = panel->SetToBoxLayout(panel_settings);
            CefRefPtr<CefLabelButton> kicker =
                CefLabelButton::CreateLabelButton(button_delegate_, entry.group_label);
            kicker->SetFocusable(false);
            kicker->SetEnabled(false);
            kicker->SetInkDropEnabled(false);
            kicker->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_LEFT);
            kicker->SetMinimumSize(CefSize(0, GroupedTabKickerHeightDip()));
            kicker->SetAccessibleName(entry.group_label + " branch");
            kicker->SetFontList(FontList(GroupedTabKickerFontPx(), ChromeFontWeight::kRegular));
            panel->AddChildView(kicker);
            panel_layout->SetFlexForView(kicker, 1);
            tab_strip_->AddChildView(panel);
            group_kicker_panels_.push_back(panel);
            group_kickers_.push_back(kicker);
        }
        TabEntryViews views;
        // The row is the outline (hairline when selected, rail otherwise); the inner
        // panel is the fill that carries the favicon, title, and trailing control.
        views.row = CefPanel::CreatePanel(new PanelDelegate(this, CefSize(0, row_height)));
        views.row->SetID(static_cast<int>(ChromeViewId::kTabStripEntry));
        CefRefPtr<CefBoxLayout> outline_layout = views.row->SetToBoxLayout(OutlinedLayout(0));
        views.inner = CefPanel::CreatePanel(new PanelDelegate(this, CefSize()));
        CefBoxLayoutSettings row_settings;
        row_settings.horizontal = static_cast<int>(true);
        row_settings.between_child_spacing = 0;
        row_settings.inside_border_insets =
            CefInsets(0, entry.group_parent.has_value() ? GroupedTabIndentDip() : 0, 0, 0);
        CefRefPtr<CefBoxLayout> row_layout = views.inner->SetToBoxLayout(row_settings);
        views.row->AddChildView(views.inner);
        outline_layout->SetFlexForView(views.inner, 1);
        const int slot = row_height;

        views.favicon = CefLabelButton::CreateLabelButton(button_delegate_, "");
        views.favicon->SetID(static_cast<int>(ChromeViewId::kTabStripEntryFavicon));
        views.favicon->SetAccessibleName(entry.favicon != nullptr ? "Tab favicon"
                                                                  : "Tab favicon placeholder");
        views.favicon->SetFocusable(false);
        views.favicon->SetMinimumSize(CefSize(slot, slot));
        views.favicon_is_page = entry.favicon != nullptr;
        if (entry.favicon != nullptr) {
            views.favicon_source = entry.favicon;
        } else if (fallback_icon.has_value()) {
            views.favicon_source = *fallback_icon;
        }
        views.inner->AddChildView(views.favicon);

        CefRefPtr<CollectionButtonDelegate> title_delegate =
            new CollectionButtonDelegate(this, CollectionButtonAction::kActivateTab, index);
        tab_button_delegates_.push_back(title_delegate);
        views.title =
            CefLabelButton::CreateLabelButton(title_delegate, TruncateCollectionTitle(entry.title));
        views.title->SetID(static_cast<int>(ChromeViewId::kTabStripEntryTitle));
        views.title->SetAccessibleName(TabEntryAccessibleName(entry));
        views.title->SetTooltipText(entry.title);
        views.title->SetFocusable(true);
        views.title->SetHorizontalAlignment(CEF_HORIZONTAL_ALIGNMENT_LEFT);
        views.title->SetMinimumSize(CefSize(tokens_.spacing_6_dip * 2, slot));
        views.title->SetFontList(
            FontList(13, entry.active ? ChromeFontWeight::kMedium : ChromeFontWeight::kRegular));
        views.inner->AddChildView(views.title);
        row_layout->SetFlexForView(views.title, 1);

        // Regular tabs close; pinned tabs unpin (Arc keeps pinned tabs until
        // they are explicitly released).
        CefRefPtr<CollectionButtonDelegate> close_delegate = new CollectionButtonDelegate(
            this,
            entry.pinned ? CollectionButtonAction::kUnpinTab : CollectionButtonAction::kCloseTab,
            index);
        tab_button_delegates_.push_back(close_delegate);
        views.close = CefLabelButton::CreateLabelButton(close_delegate, "");
        views.close->SetID(static_cast<int>(ChromeViewId::kTabStripEntryClose));
        views.close->SetAccessibleName(entry.pinned ? "Unpin tab: " + entry.title
                                                    : TabCloseAccessibleName(entry));
        views.close->SetTooltipText(entry.pinned ? "Unpin tab" : "Close tab");
        views.close->SetFocusable(true);
        views.close->SetMinimumSize(CefSize(slot, slot));
        const std::optional<CefRefPtr<CefImage>>& trailing = entry.pinned ? pin_icon : close_icon;
        if (trailing.has_value()) {
            views.trailing_source = *trailing;
        } else {
            views.close->SetText(entry.pinned ? "-" : "\xC3\x97");
        }
        views.inner->AddChildView(views.close);

        for (const CefRefPtr<CefLabelButton>& button : {views.favicon, views.title, views.close}) {
            // Hover is drawn into the edge images (RefreshRowHover); the square ink drop would
            // show through the rounded fill.
            button->SetInkDropEnabled(false);
        }

        tab_entry_views_.push_back(views);
        tab_strip_->AddChildView(views.row);
    }
    if (native && !tabs_header_added) {
        tabs_section_header_ =
            add_section_header(ActiveSpaceSectionTitle(), ChromeViewId::kTabsSectionHeader);
    }
    // Native's New Tab row is the list's last row, right after the last tab.
    if (native) {
        tab_strip_->AddChildView(new_tab_button_);
    }
    UpdateTabStripPreferredSize();
    RebuildCollapsedTabs();
    ApplyCollectionTheme();
}

void BrowserChrome::RebuildCollapsedTabs() {
    for (const CefRefPtr<CollectionButtonDelegate>& delegate : collapsed_tab_delegates_) {
        delegate->Detach();
    }
    collapsed_tab_delegates_.clear();
    collapsed_tab_views_.clear();
    collapsed_tab_list_->RemoveAllChildViews();
    collapsed_pinned_separator_ = nullptr;
    collapsed_pinned_separator_wrapper_ = nullptr;

    const CollapsedRailTabLayout layout = CollapsedRailLayout(tab_entries_);
    const std::optional<CefRefPtr<CefImage>> fallback_icon =
        icon_catalog_.Load(ChromeIcon::kLocation, FallbackFaviconIconTone(), ChromeIconSize::k16);
    const int button = kCollapsedRailButtonDip;
    for (std::size_t position = 0; position < layout.order.size(); ++position) {
        if (position == layout.separator_before) {
            // Pinned tabs end here; the same hairline as the bottom group's.
            CefRefPtr<SurfacePanelDelegate> wrapper_delegate = new SurfacePanelDelegate(
                this, SurfaceSlot::kRail, CefSize(button, kCollapsedRailSeparatorHeightDip),
                CefSize(button, kCollapsedRailSeparatorHeightDip),
                CefSize(button, kCollapsedRailSeparatorHeightDip));
            surface_delegates_.push_back(wrapper_delegate);
            collapsed_pinned_separator_wrapper_ = CefPanel::CreatePanel(wrapper_delegate);
            collapsed_pinned_separator_wrapper_->SetID(
                static_cast<int>(ChromeViewId::kCollapsedRailSeparator));
            collapsed_pinned_separator_wrapper_->SetToBoxLayout(CenteredLayout());
            const CefSize line(kCollapsedRailSeparatorWidthDip, DividerHeight(tokens_));
            CefRefPtr<SurfacePanelDelegate> line_delegate =
                new SurfacePanelDelegate(this, SurfaceSlot::kHairline, line, line, line);
            surface_delegates_.push_back(line_delegate);
            collapsed_pinned_separator_ = CefPanel::CreatePanel(line_delegate);
            collapsed_pinned_separator_->SetID(
                static_cast<int>(ChromeViewId::kCollapsedRailSeparator));
            collapsed_pinned_separator_wrapper_->AddChildView(collapsed_pinned_separator_);
            collapsed_tab_list_->AddChildView(collapsed_pinned_separator_wrapper_);
        }
        const std::size_t source = layout.order[position];
        const TabStripEntrySnapshot& entry = tab_entries_[source];
        CollapsedTabViews views;
        views.source_index = source;
        // The row is the outline (hairline when active, rail otherwise); the inner panel is
        // the fill: the same language as the expanded selected tab row.
        views.row = CefPanel::CreatePanel(new PanelDelegate(this, CefSize(button, button)));
        views.row->SetID(static_cast<int>(ChromeViewId::kCollapsedRailTab));
        CefRefPtr<CefBoxLayout> outline_layout = views.row->SetToBoxLayout(OutlinedLayout(0));
        views.inner = CefPanel::CreatePanel(new PanelDelegate(this, CefSize()));
        CefBoxLayoutSettings inner_settings;
        inner_settings.horizontal = static_cast<int>(true);
        inner_settings.between_child_spacing = 0;
        CefRefPtr<CefBoxLayout> inner_layout = views.inner->SetToBoxLayout(inner_settings);
        views.row->AddChildView(views.inner);
        outline_layout->SetFlexForView(views.inner, 1);

        CefRefPtr<CollectionButtonDelegate> delegate =
            new CollectionButtonDelegate(this, CollectionButtonAction::kActivateTab, source);
        collapsed_tab_delegates_.push_back(delegate);
        views.button = CefLabelButton::CreateLabelButton(delegate, "");
        views.button->SetID(static_cast<int>(ChromeViewId::kCollapsedRailTab));
        views.button->SetAccessibleName(TabEntryAccessibleName(entry));
        views.button->SetTooltipText(CollapsedRailTabTooltip(entry));
        views.button->SetFocusable(true);
        views.button->SetInkDropEnabled(false);
        views.button->SetMinimumSize(CefSize(button, button));
        views.favicon_is_page = entry.favicon != nullptr;
        if (entry.favicon != nullptr) {
            views.favicon_source = entry.favicon;
        } else if (fallback_icon.has_value()) {
            views.favicon_source = *fallback_icon;
        }
        views.inner->AddChildView(views.button);
        inner_layout->SetFlexForView(views.button, 1);

        collapsed_tab_views_.push_back(views);
        collapsed_tab_list_->AddChildView(views.row);
    }
    UpdateCollapsedTabListSize();
}

void BrowserChrome::UpdateCollapsedTabListSize() {
    const bool separator = collapsed_pinned_separator_wrapper_ != nullptr;
    const int height = CollapsedRailTabListHeightDip(tab_entries_.size(), separator);
    if (collapsed_tab_list_delegate_ != nullptr) {
        collapsed_tab_list_delegate_->SetPreferredSize(CefSize(kCollapsedRailButtonDip, height));
    }
    if (collapsed_tab_list_ != nullptr) {
        collapsed_tab_list_->SetSize(CefSize(kCollapsedRailButtonDip, height));
        collapsed_tab_list_->Layout();
    }
}

void BrowserChrome::RebuildCollapsedSpaces() {
    for (const CefRefPtr<CollectionButtonDelegate>& delegate : collapsed_space_delegates_) {
        delegate->Detach();
    }
    collapsed_space_delegates_.clear();
    collapsed_space_views_.clear();
    collapsed_space_list_->RemoveAllChildViews();
    for (std::size_t index = 0; index < space_entries_.size(); ++index) {
        const SpaceSwitcherEntrySnapshot& entry = space_entries_[index];
        CollapsedSpaceViews views;
        views.source_index = index;
        CefRefPtr<CollectionButtonDelegate> delegate =
            new CollectionButtonDelegate(this, CollectionButtonAction::kActivateSpace, index);
        collapsed_space_delegates_.push_back(delegate);
        views.button = CefLabelButton::CreateLabelButton(delegate, "");
        views.button->SetID(static_cast<int>(ChromeViewId::kCollapsedRailSpace));
        views.button->SetAccessibleName(SpaceEntryAccessibleName(entry));
        views.button->SetTooltipText(CollapsedRailSpaceTooltip(entry));
        views.button->SetFocusable(true);
        views.button->SetInkDropEnabled(true);
        views.button->SetMinimumSize(
            CefSize(kCollapsedRailButtonDip, kCollapsedRailSpaceButtonHeightDip));
        collapsed_space_views_.push_back(views);
        collapsed_space_list_->AddChildView(views.button);
    }
}

void BrowserChrome::SetSpaceSwitcherEntries(
    const std::vector<SpaceSwitcherEntrySnapshot>& entries) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    for (const CefRefPtr<CollectionButtonDelegate>& delegate : space_button_delegates_) {
        delegate->Detach();
    }
    space_button_delegates_.clear();
    space_entry_views_.clear();
    space_entries_ = entries;
    space_switcher_->RemoveAllChildViews();

    const bool native = tokens_.look == LookPreset::kNative;
    const BrowserChrome::NativeSwitcherMetrics switcher = BrowserChrome::NativeSwitcherSizes();
    const int pill_height = native ? switcher.segment_dip : RowHeight();
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const SpaceSwitcherEntrySnapshot& entry = entries[index];
        SpaceEntryViews views;
        views.row = CefPanel::CreatePanel(new PanelDelegate(this, CefSize(0, pill_height)));
        views.row->SetID(static_cast<int>(ChromeViewId::kSpaceSwitcherEntry));
        views.row->SetToBoxLayout(OutlinedLayout(kHairlineDip));
        views.inner = CefPanel::CreatePanel(new PanelDelegate(this, CefSize()));
        CefBoxLayoutSettings pill_settings;
        pill_settings.horizontal = static_cast<int>(true);
        pill_settings.between_child_spacing = tokens_.spacing_1_dip;
        pill_settings.inside_border_horizontal_spacing = tokens_.spacing_2_dip;
        pill_settings.cross_axis_alignment = CEF_AXIS_ALIGNMENT_CENTER;
        CefRefPtr<CefBoxLayout> pill_layout = views.inner->SetToBoxLayout(pill_settings);
        views.row->AddChildView(views.inner);

        // The color mark is the space's dot: the same size for every chip, so the
        // active one is told apart by its full name and --text label, not by a box.
        // Native segments have no dot at all (no space color in Native).
        if (!native) {
            const int dot = tokens_.spacing_2_dip;
            views.color_mark = CefPanel::CreatePanel(new PanelDelegate(this, CefSize(dot, dot)));
            views.color_mark->SetID(static_cast<int>(ChromeViewId::kSpaceSwitcherEntryColorMark));
            views.inner->AddChildView(views.color_mark);
        }

        CefRefPtr<CollectionButtonDelegate> name_delegate =
            new CollectionButtonDelegate(this, CollectionButtonAction::kActivateSpace, index);
        space_button_delegates_.push_back(name_delegate);
        // Native segments show the full name; with more than four spaces the inactive ones
        // shrink to their initial so the track still fits the sidebar.
        const bool shrink = native && entries.size() > 4;
        views.name = CefLabelButton::CreateLabelButton(
            name_delegate,
            native && !shrink ? TruncateCollectionTitle(entry.name) : SpacePillLabel(entry));
        views.name->SetID(static_cast<int>(ChromeViewId::kSpaceSwitcherEntryName));
        views.name->SetAccessibleName(SpaceEntryAccessibleName(entry));
        views.name->SetTooltipText(entry.name);
        views.name->SetFocusable(true);
        views.name->SetInkDropEnabled(true);
        views.name->SetFontList(
            FontList(12, entry.active ? ChromeFontWeight::kMedium : ChromeFontWeight::kRegular));
        views.name->SetMinimumSize(
            CefSize(pill_height - 2 * kHairlineDip - 2 * tokens_.spacing_2_dip,
                    pill_height - 2 * kHairlineDip));
        views.inner->AddChildView(views.name);
        // Native segments share the track: each segment and its label take an equal flex
        // share so the selected surface covers the whole segment, not the label's minimum.
        if (native) {
            pill_layout->SetFlexForView(views.name, 1);
        }

        space_entry_views_.push_back(views);
        space_switcher_->AddChildView(views.row);
        if (native) {
            if (CefRefPtr<CefBoxLayout> track = space_switcher_->GetLayout()->AsBoxLayout()) {
                track->SetFlexForView(views.row, 1);
            }
        }
    }
    if (tabs_section_header_ != nullptr) {
        const std::string title = ActiveSpaceSectionTitle();
        tabs_section_header_->SetText(title);
        tabs_section_header_->SetAccessibleName(title + " section");
    }
    RebuildCollapsedSpaces();
    ApplyCollectionTheme();
    sidebar_->Layout();
    collapsed_rail_->Layout();
}

void BrowserChrome::AttachBrowserView(CefRefPtr<CefBrowserView> browser_view) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || browser_view == nullptr) {
        return;
    }
    // Leaving split view tears the second pane and divider down even when the
    // surviving view is the one already attached. The divider panel itself is
    // kept for the next split, only removed from the view tree.
    if (split_view_ != nullptr) {
        browser_content_->RemoveChildView(split_view_);
        split_view_ = nullptr;
        browser_content_->RemoveChildView(split_divider_);
    }
    if (browser_view_ != nullptr && browser_view->IsSame(browser_view_)) {
        browser_content_->Layout();
        return;
    }
    if (browser_view_ != nullptr) {
        browser_content_->RemoveChildView(browser_view_);
    }
    browser_view_ = browser_view;
    browser_view_->SetID(static_cast<int>(ChromeViewId::kBrowserView));
    browser_content_->AddChildView(browser_view_);
    if (browser_content_layout_ != nullptr) {
        browser_content_layout_->SetFlexForView(browser_view_, 1);
    }
    browser_content_->Layout();
}

void BrowserChrome::AttachSplitBrowserViews(CefRefPtr<CefBrowserView> first,
                                            CefRefPtr<CefBrowserView> second) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || first == nullptr || second == nullptr || first->IsSame(second)) {
        return;
    }
    // Keep the first pane in place when it is already the attached single view. Removing and
    // immediately re-adding a newly created CefBrowserView can crash CEF while OnAfterCreated is
    // still settling it. The divider panel is reused across splits.
    const bool first_already_attached = browser_view_ != nullptr && browser_view_->IsSame(first);
    if (split_view_ != nullptr) {
        browser_content_->RemoveChildView(split_view_);
        split_view_ = nullptr;
    }
    if (split_divider_ != nullptr) {
        browser_content_->RemoveChildView(split_divider_);
    }
    if (browser_view_ != nullptr && !first_already_attached) {
        browser_content_->RemoveChildView(browser_view_);
    }
    if (split_divider_ == nullptr) {
        CefRefPtr<SurfacePanelDelegate> divider_delegate = new SurfacePanelDelegate(
            this, SurfaceSlot::kHairline, CefSize(SplitDividerWidthDip(), 0),
            CefSize(SplitDividerWidthDip(), 0), CefSize(SplitDividerWidthDip(), 0));
        surface_delegates_.push_back(divider_delegate);
        split_divider_ = CefPanel::CreatePanel(divider_delegate);
        split_divider_->SetID(static_cast<int>(ChromeViewId::kSplitDivider));
    }
    browser_view_ = first;
    split_view_ = second;
    // Both panes announce as the browser view region; the divider sits between
    // them in the live snapshot only.
    browser_view_->SetID(static_cast<int>(ChromeViewId::kBrowserView));
    split_view_->SetID(static_cast<int>(ChromeViewId::kBrowserView));
    const int first_flex = SplitFlexForRatio(split_ratio_);
    if (!first_already_attached) {
        browser_content_->AddChildView(browser_view_);
    }
    browser_content_->AddChildView(split_divider_);
    browser_content_->AddChildView(split_view_);
    if (browser_content_layout_ != nullptr) {
        browser_content_layout_->SetFlexForView(browser_view_, first_flex);
        browser_content_layout_->SetFlexForView(split_divider_, 0);
        browser_content_layout_->SetFlexForView(split_view_, 100 - first_flex);
    }
    browser_content_->Layout();
}

void BrowserChrome::SetSplitRatio(double ratio) {
    CEF_REQUIRE_UI_THREAD();
    split_ratio_ = std::clamp(ratio, SplitRatioMin(), SplitRatioMax());
    if (split_view_ == nullptr || browser_content_layout_ == nullptr) {
        return;
    }
    const int first_flex = SplitFlexForRatio(split_ratio_);
    browser_content_layout_->SetFlexForView(browser_view_, first_flex);
    browser_content_layout_->SetFlexForView(split_view_, 100 - first_flex);
    browser_content_->Layout();
}

void BrowserChrome::OnNavigationChanged(const NavigationSnapshot& snapshot) {
    CEF_REQUIRE_UI_THREAD();
    if (!detached_) {
        ProjectNavigation(snapshot);
    }
}

void BrowserChrome::OnAddressChanged(const AddressBarSnapshot& snapshot) {
    CEF_REQUIRE_UI_THREAD();
    if (!detached_) {
        ProjectAddress(snapshot);
    }
}

void BrowserChrome::ApplyTheme(ChromeTokens tokens) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    tokens_ = tokens;
    root_delegate_->SetTokens(tokens_);
    ApplyLookStructure();
    ApplyDensity();
    ApplyLayoutTokens();
    ApplyControlTheme();
    rail_delegate_->SetFixedWidth(sidebar_expanded_ ? tokens_.rail_width_dip : 0);
    root_delegate_->OnLayoutChanged(root_, root_->GetBounds());
    // Native's address patch carries the toolbar color, so a theme change repaints it now.
    PushCornerPatches();
}

std::string BrowserChrome::FontList(int size_px, ChromeFontWeight weight) const {
    return FontListForTokens(tokens_, size_px, weight);
}

void BrowserChrome::ApplyLookStructure() {
    CEF_REQUIRE_UI_THREAD();
    const bool native = tokens_.look == LookPreset::kNative;
    const AddressPlateStyle address_style = AddressPlateStyleFor(tokens_);
    if (applied_look_ != tokens_.look) {
        // Index of `child` among `parent`'s children, for putting a view back in its Arc place.
        // A missing child is a broken view tree: fail here rather than re-parent at index 0.
        const auto index_of = [](const CefRefPtr<CefPanel>& parent,
                                 const CefRefPtr<CefView>& child) {
            for (std::size_t i = 0; i < parent->GetChildViewCount(); ++i) {
                if (parent->GetChildViewAt(static_cast<int>(i))->IsSame(child)) {
                    return static_cast<int>(i);
                }
            }
            CHECK(false) << "view is not a child of the parent it is restored to";
            return -1;
        };
        if (native) {
            sidebar_->RemoveChildView(navigation_row_);
            sidebar_->RemoveChildView(address_row_);
            toolbar_content_->AddChildViewAt(navigation_row_, 0);
            toolbar_content_->AddChildViewAt(address_row_, 1);
            // Native: New Tab moves into the tab list (SetTabStripEntries), the segments and
            // the three buttons move into the switcher row, and the footer stacks it under the
            // Agent entry.
            sidebar_->RemoveChildView(new_tab_button_);
            sidebar_->RemoveChildView(space_switcher_);
            for (const CefRefPtr<CefLabelButton>& button :
                 {tabs_button_, settings_button_, new_space_button_}) {
                rail_footer_->RemoveChildView(button);
            }
            switcher_row_->AddChildView(space_switcher_);
            switcher_row_->AddChildView(new_space_button_);
            switcher_row_->AddChildView(tabs_button_);
            switcher_row_->AddChildView(settings_button_);
            rail_footer_->AddChildViewAt(switcher_row_, 0);
            CefBoxLayoutSettings footer_settings;
            footer_settings.horizontal = static_cast<int>(false);
            footer_settings.between_child_spacing = tokens_.spacing_1_dip;
            CefRefPtr<CefBoxLayout> native_footer_layout =
                rail_footer_->SetToBoxLayout(footer_settings);
            // The flex the Arc footer row gave the Agent entry stays on the view (box-layout
            // flex is a view property), and in this vertical footer it would stretch the entry
            // down the whole rail and push the tab list to zero height. Clear it.
            native_footer_layout->SetFlexForView(agent_button_, 0);
            CefBoxLayoutSettings row_settings;
            row_settings.horizontal = static_cast<int>(true);
            row_settings.between_child_spacing = tokens_.spacing_1_dip;
            CefRefPtr<CefBoxLayout> row_layout = switcher_row_->SetToBoxLayout(row_settings);
            row_layout->SetFlexForView(space_switcher_, 1);
            CefBoxLayoutSettings track_settings;
            track_settings.horizontal = static_cast<int>(true);
            track_settings.between_child_spacing = 0;
            const int inset = NativeSwitcherSizes().track_inset_dip;
            track_settings.inside_border_insets = CefInsets(inset, inset, inset, inset);
            space_switcher_->SetToBoxLayout(track_settings);
        } else if (applied_look_ == LookPreset::kNative) {
            toolbar_content_->RemoveChildView(navigation_row_);
            toolbar_content_->RemoveChildView(address_row_);
            sidebar_->AddChildViewAt(navigation_row_, 0);
            sidebar_->AddChildViewAt(address_row_, 1);
            // Arc and Graphite: the switcher sits above the footer row, New Tab above the list.
            rail_footer_->RemoveChildView(switcher_row_);
            switcher_row_->RemoveChildView(space_switcher_);
            switcher_row_->RemoveChildView(new_space_button_);
            switcher_row_->RemoveChildView(tabs_button_);
            switcher_row_->RemoveChildView(settings_button_);
            sidebar_->AddChildViewAt(space_switcher_, index_of(sidebar_, rail_footer_));
            // A box-layout flex stays on the view itself across layouts and parents: the
            // switcher row's flex must not make the space bar grow in the rail column.
            if (CefRefPtr<CefBoxLayout> rail = sidebar_->GetLayout()->AsBoxLayout()) {
                rail->SetFlexForView(space_switcher_, 0);
            }
            // New Tab sits in the tab strip under Native; detach it before restoring it.
            tab_strip_->RemoveChildView(new_tab_button_);
            sidebar_->AddChildViewAt(new_tab_button_, index_of(sidebar_, tab_list_scroll_));
            rail_footer_->AddChildView(tabs_button_);
            rail_footer_->AddChildView(settings_button_);
            rail_footer_->AddChildView(new_space_button_);
            CefBoxLayoutSettings footer_settings;
            footer_settings.horizontal = static_cast<int>(true);
            footer_settings.between_child_spacing = tokens_.spacing_1_dip / 2;
            CefRefPtr<CefBoxLayout> footer_layout = rail_footer_->SetToBoxLayout(footer_settings);
            footer_layout->SetFlexForView(agent_button_, 1);
            CefBoxLayoutSettings collection = CollectionLayout(tokens_);
            collection.horizontal = static_cast<int>(true);
            collection.between_child_spacing = tokens_.spacing_1_dip;
            space_switcher_->SetToBoxLayout(collection);
        }
        applied_look_ = tokens_.look;
        // Rows and segments take their structure at build time: rebuild them for the new look.
        const std::vector<TabStripEntrySnapshot> tabs = tab_entries_;
        const std::vector<SpaceSwitcherEntrySnapshot> spaces = space_entries_;
        SetTabStripEntries(tabs);
        SetSpaceSwitcherEntries(spaces);
    }
    toolbar_->SetVisible(native);
    const bool native_idle = native && !address_editing_;
    address_idle_label_->SetVisible(native_idle);
    address_field_->SetVisible(!native_idle);
    address_layout_ = address_row_->SetToBoxLayout(OutlinedLayout(address_style.outline_dip));
    address_layout_->SetFlexForView(address_field_holder_, 1);
    const int navigation_size = NavigationButtonSize();
    navigation_row_delegate_->SetPreferredSize(
        CefSize(native ? 2 * navigation_size + tokens_.spacing_1_dip : 0, navigation_size));
    address_row_delegate_->SetPreferredSize(CefSize(0, AddressHeight()));
    textfield_delegate_->SetPreferredSize(
        CefSize(tokens_.spacing_6_dip * 4, AddressHeight() - address_style.outline_dip * 2));
    for (const CefRefPtr<CefLabelButton>& button : {back_button_, forward_button_}) {
        button->SetMinimumSize(CefSize(navigation_size, navigation_size));
    }
    for (const CefRefPtr<CefLabelButton>& button :
         {address_location_icon_, reload_button_, reader_button_}) {
        const int cell = AddressHeight() - address_style.outline_dip * 2;
        button->SetMinimumSize(CefSize(cell, cell));
    }
    // ApplyDensity sizes the footer buttons for Arc and Graphite; Native's Agent entry and
    // switcher buttons have their own sizes.
    const int control = tokens_.control_height_dip;
    if (native) {
        // The footer's fixed height (Agent entry, gap, switcher row) keeps the box layout from
        // stretching the Agent entry down the rail.
        const int footer_height = BrowserChrome::NativeSwitcherSizes().row_dip +
                                  tokens_.spacing_1_dip + kNativeAgentHeightDip;
        rail_footer_delegate_->SetPreferredSize(CefSize(0, footer_height));
        agent_button_->SetMinimumSize(CefSize(0, kNativeAgentHeightDip));
        for (const CefRefPtr<CefLabelButton>& button :
             {tabs_button_, settings_button_, new_space_button_}) {
            const int size = BrowserChrome::NativeSwitcherSizes().button_dip;
            button->SetMinimumSize(CefSize(size, size));
        }
    } else {
        rail_footer_delegate_->SetPreferredSize(CefSize());
        agent_button_->SetMinimumSize(CefSize(0, control));
        for (const CefRefPtr<CefLabelButton>& button :
             {tabs_button_, settings_button_, new_space_button_}) {
            button->SetMinimumSize(CefSize(control, control));
        }
    }
    if (native) {
        if (CefRefPtr<CefBoxLayout> layout = toolbar_content_->GetLayout()->AsBoxLayout()) {
            layout->SetFlexForView(address_row_, 1);
        }
    }
    ApplyAddressIndicators();
}

void BrowserChrome::ApplyDensity() {
    const int h = tokens_.control_height_dip;
    if (h == applied_control_height_) {
        return;
    }
    applied_control_height_ = h;
    navigation_row_delegate_->SetPreferredSize(CefSize(0, h));
    address_row_delegate_->SetPreferredSize(CefSize(0, h));
    const int outline = AddressPlateStyleFor(tokens_).outline_dip;
    textfield_delegate_->SetPreferredSize(CefSize(tokens_.spacing_6_dip * 4, h - outline * 2));
    for (const CefRefPtr<CefLabelButton>& button : {back_button_, forward_button_}) {
        button->SetMinimumSize(CefSize(h, h));
    }
    for (const CefRefPtr<CefLabelButton>& button : {address_location_icon_, reload_button_}) {
        button->SetMinimumSize(CefSize(h - outline * 2, h - outline * 2));
    }
    for (const CefRefPtr<CefLabelButton>& button : {new_tab_button_, agent_button_}) {
        button->SetMinimumSize(CefSize(0, h));
    }
    for (const CefRefPtr<CefLabelButton>& button :
         {tabs_button_, settings_button_, new_space_button_}) {
        button->SetMinimumSize(CefSize(h, h));
    }
    ApplyLookStructure();
    // Rows and pills take their height at construction: rebuild them from the snapshots.
    const std::vector<TabStripEntrySnapshot> tabs = tab_entries_;
    const std::vector<SpaceSwitcherEntrySnapshot> spaces = space_entries_;
    SetTabStripEntries(tabs);
    SetSpaceSwitcherEntries(spaces);
}

void BrowserChrome::ApplyLayoutTokens() {
    CEF_REQUIRE_UI_THREAD();
    CefRect bounds = root_->GetBounds();
    if (bounds.width <= 0 || bounds.height <= 0) {
        bounds = CefRect(0, 0, 1200, 800);
    }
    const DipRect root = {.x = 0, .y = 0, .width = bounds.width, .height = bounds.height};
    const CardInsets content = ContentInsets(root, tokens_, sidebar_expanded_, agent_panel_open_);
    const CardInsets agent = AgentPanelInsets(root, tokens_, sidebar_expanded_);
    const std::string key = std::to_string(tokens_.titlebar_band_dip) + "/" +
                            std::to_string(static_cast<int>(tokens_.sidebar_side)) + "/" +
                            std::to_string(tokens_.control_height_dip) + "/" +
                            std::to_string(tokens_.row_gap_dip) + "/" +
                            std::to_string(content.left) + "," + std::to_string(content.top) + "," +
                            std::to_string(content.right) + "," + std::to_string(content.bottom) +
                            "/" + std::to_string(agent.left) + "," + std::to_string(agent.top) +
                            "," + std::to_string(agent.right) + "," + std::to_string(agent.bottom);
    if (key == layout_key_) {
        return;
    }
    layout_key_ = key;

    rail_layout_ = sidebar_->SetToBoxLayout(RailLayout(tokens_));
    rail_layout_->SetFlexForView(tab_list_scroll_, 1);
    navigation_row_layout_ = navigation_row_->SetToBoxLayout(NavigationRowLayout(tokens_));
    tab_strip_->SetToBoxLayout(TabListLayout(tokens_));

    CefBoxLayoutSettings collapsed_settings;
    collapsed_settings.horizontal = static_cast<int>(false);
    collapsed_settings.between_child_spacing = RailSectionSpacingDip();
    collapsed_settings.inside_border_insets =
        CefInsets(CollapsedRailTopInset(tokens_), 0, tokens_.spacing_3_dip, 0);
    collapsed_layout_ = collapsed_rail_->SetToBoxLayout(collapsed_settings);
    collapsed_layout_->SetFlexForView(collapsed_tab_scroll_, 1);

    browser_content_layout_ = browser_content_->SetToBoxLayout(FloatingCanvasLayout(content));
    if (split_view_ != nullptr) {
        const int first_flex = SplitFlexForRatio(split_ratio_);
        browser_content_layout_->SetFlexForView(browser_view_, first_flex);
        browser_content_layout_->SetFlexForView(split_divider_, 0);
        browser_content_layout_->SetFlexForView(split_view_, 100 - first_flex);
    } else if (browser_view_ != nullptr) {
        browser_content_layout_->SetFlexForView(browser_view_, 1);
    }

    CefBoxLayoutSettings agent_settings;
    agent_settings.horizontal = static_cast<int>(true);
    agent_settings.inside_border_insets =
        CefInsets(agent.top, agent.left, agent.bottom, agent.right);
    agent_panel_layout_ = agent_panel_->SetToBoxLayout(agent_settings);
    if (agent_panel_view_ != nullptr) {
        agent_panel_layout_->SetFlexForView(agent_panel_view_, 1);
    }
    UpdateTabStripPreferredSize();
}

void BrowserChrome::SetSidebarExpanded(bool expanded) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    sidebar_expanded_ = expanded;
    // Exactly one of the two rails takes the rail column; the other leaves layout. Both stay
    // in the view tree, so ViewTreeContract() is unaffected.
    rail_delegate_->SetFixedWidth(expanded ? tokens_.rail_width_dip : 0);
    collapsed_rail_delegate_->SetFixedWidth(expanded ? 0 : kCollapsedRailWidthDip);
    sidebar_->SetVisible(expanded);
    collapsed_rail_->SetVisible(!expanded);
    root_delegate_->SetSidebarExpanded(expanded);
    ApplyLayoutTokens();
    root_delegate_->OnLayoutChanged(root_, root_->GetBounds());
}

void BrowserChrome::SetAgentPanelView(CefRefPtr<CefView> view) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || view == nullptr || agent_panel_view_ != nullptr) {
        return;
    }
    agent_panel_view_ = view;
    agent_panel_->AddChildView(view);
    CefRefPtr<CefBoxLayout> layout = agent_panel_->GetLayout()->AsBoxLayout();
    if (layout != nullptr) {
        layout->SetFlexForView(view, 1);
    }
}

void BrowserChrome::SetAgentPanelOpen(bool open) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    agent_panel_open_ = open;
    agent_panel_->SetVisible(open);
    ApplyRailButtonTheme();
    PushCornerPatches();
    root_delegate_->SetAgentPanelOpen(open);
    ApplyLayoutTokens();
    root_delegate_->OnLayoutChanged(root_, root_->GetBounds());
}

void BrowserChrome::SetUpdateNotice(std::string notice) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || notice == update_notice_) {
        return;
    }
    update_notice_ = std::move(notice);
    const std::string label =
        update_notice_.empty() ? std::string("Settings") : "Settings: " + update_notice_;
    settings_button_->SetTooltipText(label);
    settings_button_->SetAccessibleName(label);
    ApplyRailButtonTheme();
}

void BrowserChrome::BeginAddressEditing() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    if (tokens_.look == LookPreset::kNative) {
        address_editing_ = true;
        address_idle_label_->SetVisible(false);
        address_field_->SetVisible(true);
    }
    host_->BeginAddressEditing();
    address_field_->RequestFocus();
    ScheduleAddressSelection();
}

void BrowserChrome::Detach() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    detached_ = true;
    host_ = nullptr;
    *alive_ = nullptr;
    root_delegate_->Detach();
    button_delegate_->Detach();
    textfield_delegate_->Detach();
    for (const CefRefPtr<CollectionButtonDelegate>& delegate : tab_button_delegates_) {
        delegate->Detach();
    }
    for (const CefRefPtr<CollectionButtonDelegate>& delegate : space_button_delegates_) {
        delegate->Detach();
    }
    for (const CefRefPtr<CollectionButtonDelegate>& delegate : collapsed_tab_delegates_) {
        delegate->Detach();
    }
    for (const CefRefPtr<CollectionButtonDelegate>& delegate : collapsed_space_delegates_) {
        delegate->Detach();
    }
    for (const CefRefPtr<SurfacePanelDelegate>& surface_delegate : surface_delegates_) {
        surface_delegate->Detach();
    }
    if (browser_view_ != nullptr && browser_content_ != nullptr) {
        browser_content_->RemoveChildView(browser_view_);
    }
    browser_view_ = nullptr;
    if (agent_panel_view_ != nullptr && agent_panel_ != nullptr) {
        agent_panel_->RemoveChildView(agent_panel_view_);
    }
    agent_panel_view_ = nullptr;
}

void BrowserChrome::HandleButtonPressed(ChromeViewId view_id) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }

    switch (view_id) {
        case ChromeViewId::kBack:
            host_->ExecuteBrowserCommand(BrowserCommand::kBack);
            host_->FocusBrowserView();
            return;
        case ChromeViewId::kForward:
            host_->ExecuteBrowserCommand(BrowserCommand::kForward);
            host_->FocusBrowserView();
            return;
        case ChromeViewId::kReload:
            host_->ExecuteBrowserCommand(BrowserCommand::kReload);
            host_->FocusBrowserView();
            return;
        case ChromeViewId::kReaderButton:
            host_->ExecuteBrowserCommand(BrowserCommand::kToggleReaderMode);
            return;
        case ChromeViewId::kAddressShield:
            host_->OpenBlockerSettings();
            return;
        case ChromeViewId::kAddressAgentDot:
            host_->ToggleAgentPanel();
            return;
        case ChromeViewId::kAddress:
        case ChromeViewId::kAddressIdleLabel:
            BeginAddressEditing();
            return;
        case ChromeViewId::kActiveTab:
            host_->FocusBrowserView();
            return;
        case ChromeViewId::kRailNewTab:
            // Arc's New Tab drops straight into the address field.
            host_->ExecuteBrowserCommand(BrowserCommand::kNewTab);
            BeginAddressEditing();
            return;
        case ChromeViewId::kCollapsedRailNewTab:
            // The new tab opens on the start page, which focuses its own search field, so the
            // collapsed rail stays collapsed instead of expanding for the address bar.
            host_->ExecuteBrowserCommand(BrowserCommand::kNewTab);
            return;
        case ChromeViewId::kRailAgentButton:
            host_->ToggleAgentPanel();
            return;
        case ChromeViewId::kToolbarShare:
            host_->ExecuteBrowserCommand(BrowserCommand::kCopyTabUrl);
            host_->FocusBrowserView();
            return;
        case ChromeViewId::kRailNewSpaceButton:
            host_->ExecuteBrowserCommand(BrowserCommand::kNewSpace);
            return;
        case ChromeViewId::kRailTabsButton:
            host_->ToggleTabOverview();
            return;
        case ChromeViewId::kRailSettingsButton:
            host_->ToggleSettings();
            return;
        default:
            return;
    }
}

namespace {

// Runs a collection action after the button's own press callback has returned: the actions
// rebuild the very rows (and delegates) the pressed button lives in, and destroying a view
// from inside its own event handler is a use-after-free in CEF's event dispatch.
class CollectionActionTask final : public CefTask {
  public:
    CollectionActionTask(std::shared_ptr<BrowserChrome*> alive, CollectionButtonAction action,
                         std::size_t index)
        : alive_(std::move(alive)), action_(action), index_(index) {}

    void Execute() override {
        if (BrowserChrome* chrome = *alive_) {
            chrome->DispatchCollectionAction(action_, index_);
        }
    }

  private:
    std::shared_ptr<BrowserChrome*> alive_;
    CollectionButtonAction action_;
    std::size_t index_;

    IMPLEMENT_REFCOUNTING(CollectionActionTask);
};

}  // namespace

void BrowserChrome::HandleCollectionButtonPressed(CollectionButtonAction action,
                                                  std::size_t index) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    CefPostTask(TID_UI, new CollectionActionTask(alive_, action, index));
}

void BrowserChrome::DispatchCollectionAction(CollectionButtonAction action, std::size_t index) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    switch (action) {
        case CollectionButtonAction::kActivateTab:
            host_->SelectTab(index);
            return;
        case CollectionButtonAction::kCloseTab:
            host_->CloseTab(index);
            return;
        case CollectionButtonAction::kActivateSpace:
            host_->SelectSpace(index);
            return;
        case CollectionButtonAction::kUnpinTab:
            host_->SetTabPinned(index, false);
            return;
    }
}

bool BrowserChrome::HandleAddressKeyEvent(CefRefPtr<CefTextfield> textfield,
                                          const CefKeyEvent& event) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || event.type != KEYEVENT_RAWKEYDOWN) {
        return false;
    }
    if (event.windows_key_code == kReturnKeyCode) {
        const std::string draft = textfield->GetText().ToString();
        host_->SubmitAddressDraft(draft);
        return true;
    }
    if (event.windows_key_code == kEscapeKeyCode) {
        host_->CancelAddressEditing();
        return true;
    }
    return false;
}

void BrowserChrome::HandleAddressUserAction(CefRefPtr<CefTextfield>) { CEF_REQUIRE_UI_THREAD(); }

void BrowserChrome::HandleAddressFocus() {
    CEF_REQUIRE_UI_THREAD();
    if (!detached_) {
        host_->BeginAddressEditing();
        UpdateAddressFocusLeadingEdge();
        ScheduleAddressSelection();
    }
}

void BrowserChrome::HandleAddressBlur() {
    CEF_REQUIRE_UI_THREAD();
    if (!detached_) {
        host_->CancelAddressEditing();
        UpdateAddressFocusLeadingEdge();
    }
}

void BrowserChrome::ProjectNavigation(const NavigationSnapshot& snapshot) {
    back_button_->SetEnabled(snapshot.can_go_back);
    forward_button_->SetEnabled(snapshot.can_go_forward);
    reload_button_->SetEnabled(true);
    active_tab_->SetText(snapshot.page_title.empty() ? "Island" : snapshot.page_title);
    active_tab_->SetAccessibleName("Current page");
}

void BrowserChrome::ProjectAddress(const AddressBarSnapshot& snapshot) {
    const bool editing = snapshot.mode != AddressBarMode::kResting;
    address_editing_ = editing;
    const bool native_idle = tokens_.look == LookPreset::kNative && !editing;
    const std::string& text = editing ? snapshot.edit_text : snapshot.display_text;
    address_idle_label_->SetText(snapshot.display_text);
    address_idle_label_->SetVisible(native_idle);
    address_field_->SetVisible(!native_idle);
    address_field_->SetReadOnly(!editing);
    if (address_field_->GetText().ToString() != text) {
        address_field_->SetText(text);
    }
    if (editing && !address_field_->HasFocus()) {
        address_field_->RequestFocus();
        ScheduleAddressSelection();
    }
    const std::string validation_message = AddressErrorMessage(snapshot.validation_error);
    validation_message_->SetText(validation_message);
    validation_message_->SetAccessibleName(validation_message);
    validation_message_->SetVisible(!validation_message.empty());
    address_field_->SetAccessibleName(
        validation_message.empty() ? "Address" : "Address, invalid: " + validation_message);
}

void BrowserChrome::ScheduleAddressSelection() {
    CEF_REQUIRE_UI_THREAD();
    CefPostTask(TID_UI, CefCreateClosureTask(base::BindOnce(SelectAllWhenFocused, address_field_)));
}

namespace {

// One posted pass after a burst of theme resets; holds the chrome through the shared
// alive handle so a task outliving the chrome is a no-op.
class ThemeReapplyTask final : public CefTask {
  public:
    explicit ThemeReapplyTask(std::shared_ptr<BrowserChrome*> alive) : alive_(std::move(alive)) {}

    void Execute() override {
        if (BrowserChrome* chrome = *alive_) {
            chrome->ReapplyThemeColors();
        }
    }

  private:
    std::shared_ptr<BrowserChrome*> alive_;

    IMPLEMENT_REFCOUNTING(ThemeReapplyTask);
};

}  // namespace

void BrowserChrome::HandleViewThemeReset() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || theme_reapply_pending_) {
        return;
    }
    theme_reapply_pending_ = true;
    CefPostTask(TID_UI, new ThemeReapplyTask(alive_));
}

void BrowserChrome::ReapplyThemeColors() {
    CEF_REQUIRE_UI_THREAD();
    theme_reapply_pending_ = false;
    if (detached_) {
        return;
    }
    ApplyControlTheme();
}

void BrowserChrome::ApplyControlTheme() {
    const ArgbColor background = ChromeSurfaceRoleForResolvedTokens(SurfaceSlot::kRoot);
    const ArgbColor rail = ChromeSurfaceRoleForResolvedTokens(SurfaceSlot::kRail);
    const ArgbColor browser_content =
        ChromeSurfaceRoleForResolvedTokens(SurfaceSlot::kBrowserContent);
    const ArgbColor hairline = ChromeSurfaceRoleForResolvedTokens(SurfaceSlot::kHairline);
    const ArgbColor address_well = AddressPlateStyleFor(tokens_).fill;
    const ArgbColor active_page_fill = ChromeSurfaceRoleForResolvedTokens(SurfaceSlot::kActivePage);
    const ArgbColor accent = ChromeSurfaceRoleForResolvedTokens(SurfaceSlot::kAccent);

    root_->SetBackgroundColor(background.argb);
    sidebar_->SetBackgroundColor(rail.argb);
    browser_content_->SetBackgroundColor(browser_content.argb);
    toolbar_->SetBackgroundColor(tokens_.surface_secondary.argb);
    toolbar_content_->SetBackgroundColor(tokens_.surface_secondary.argb);
    toolbar_hairline_->SetBackgroundColor(tokens_.border.argb);
    // The page card's rounded corners (macOS) show whatever the browser view paints behind them:
    // the canvas, not the toolkit's default.
    for (const CefRefPtr<CefBrowserView>& view : visible_browser_views()) {
        view->SetBackgroundColor(browser_content.argb);
    }
    agent_panel_->SetBackgroundColor(browser_content.argb);
    // Rail-descendant panels are created with no delegate and would otherwise inherit
    // the window's primary background; painting them the rail color keeps the whole
    // column one uniform tinted surface so the floating card separates cleanly.
    navigation_row_->SetBackgroundColor(rail.argb);
    address_row_->SetBackgroundColor(rail.argb);
    tab_strip_->SetBackgroundColor(rail.argb);
    tab_list_scroll_->SetBackgroundColor(rail.argb);
    // Native's track is the `fill` well; its segments sit on it (ApplyCollectionTheme).
    space_switcher_->SetBackgroundColor(
        (tokens_.look == LookPreset::kNative ? tokens_.fill : rail).argb);
    switcher_row_->SetBackgroundColor(rail.argb);
    rail_footer_->SetBackgroundColor(rail.argb);
    spacer_->SetBackgroundColor(rail.argb);
    divider_->SetBackgroundColor(hairline.argb);
    // The address pill's three parts (location glyph, field, reload) share the
    // surface fill; the row around them paints the hairline (accent on focus).
    address_location_icon_->SetBackgroundColor(address_well.argb);
    address_field_->SetBackgroundColor(address_well.argb);
    address_field_holder_->SetBackgroundColor(address_well.argb);
    reload_button_->SetBackgroundColor(address_well.argb);
    reader_button_->SetBackgroundColor(address_well.argb);
    // Back and Forward are ghost icon buttons: no fill of their own, so only the
    // native hover/pressed ink shows when targeted.
    back_button_->SetBackgroundColor(rail.argb);
    forward_button_->SetBackgroundColor(rail.argb);
    if (tokens_.look == LookPreset::kNative) {
        back_button_->SetBackgroundColor(tokens_.surface_secondary.argb);
        forward_button_->SetBackgroundColor(tokens_.surface_secondary.argb);
    }
    share_button_->SetBackgroundColor(tokens_.surface_secondary.argb);
    // The active-page card lifts to the primary surface against the tinted rail.
    active_page_->SetBackgroundColor(active_page_fill.argb);
    active_page_indicator_->SetBackgroundColor(accent.argb);
    UpdateAddressFocusLeadingEdge();
    address_field_->SetFontList(FontList(13, ChromeFontWeight::kRegular));
    address_idle_label_->SetBackgroundColor(address_well.argb);
    address_idle_label_->SetFontList(FontList(13, ChromeFontWeight::kRegular));
    address_idle_label_->SetEnabledTextColors(tokens_.text.argb);
    active_tab_->SetFontList(FontList(13, ChromeFontWeight::kRegular));
    back_button_->SetEnabledTextColors(tokens_.text.argb);
    forward_button_->SetEnabledTextColors(tokens_.text.argb);
    reload_button_->SetEnabledTextColors(tokens_.text.argb);
    reader_button_->SetFontList(FontList(12, ChromeFontWeight::kMedium));
    reader_button_->SetEnabledTextColors(
        (reader_active_ ? tokens_.accent : tokens_.text_secondary).argb);
    reader_button_->SetTextColor(CEF_BUTTON_STATE_DISABLED,
                                 MixColor(tokens_.surface, tokens_.text_secondary, 0.45).argb);
    address_location_icon_->SetEnabledTextColors(tokens_.text_secondary.argb);
    active_page_fallback_favicon_->SetEnabledTextColors(tokens_.text_secondary.argb);
    active_tab_->SetEnabledTextColors(tokens_.text.argb);
    validation_message_->SetEnabledTextColors(tokens_.accent.argb);
    share_button_->SetFontList(FontList(13, ChromeFontWeight::kRegular));
    share_button_->SetEnabledTextColors(tokens_.text_secondary.argb);

    const std::optional<CefRefPtr<CefImage>> back =
        icon_catalog_.Load(ChromeIcon::kBack, NavigationIconTone(), ChromeIconSize::k16);
    const std::optional<CefRefPtr<CefImage>> forward =
        icon_catalog_.Load(ChromeIcon::kForward, NavigationIconTone(), ChromeIconSize::k16);
    const std::optional<CefRefPtr<CefImage>> reload =
        icon_catalog_.Load(ChromeIcon::kReload, NavigationIconTone(), ChromeIconSize::k16);
    const std::optional<CefRefPtr<CefImage>> location =
        icon_catalog_.Load(ChromeIcon::kLocation, AddressLocationIconTone(), ChromeIconSize::k16);
    if (back.has_value()) {
        back_button_->SetImage(CEF_BUTTON_STATE_NORMAL, *back);
    }
    if (forward.has_value()) {
        forward_button_->SetImage(CEF_BUTTON_STATE_NORMAL, *forward);
    }
    if (reload.has_value()) {
        reload_button_->SetImage(CEF_BUTTON_STATE_NORMAL, *reload);
    }
    if (location.has_value()) {
        address_location_icon_->SetImage(CEF_BUTTON_STATE_NORMAL, *location);
        active_page_fallback_favicon_->SetImage(CEF_BUTTON_STATE_NORMAL, *location);
    }
    for (const CefRefPtr<CefLabelButton>& button :
         {back_button_, forward_button_, reload_button_, reader_button_, share_button_}) {
        button->SetInkDropEnabled(true);
    }
    ApplyRailButtonTheme();
    ApplyCollectionTheme();
    ApplyAddressIndicators();
}

void BrowserChrome::ApplyRailButtonTheme() {
    CEF_REQUIRE_UI_THREAD();
    const ArgbColor rail = ChromeSurfaceRoleForResolvedTokens(SurfaceSlot::kRail);
    const ArgbColor surface = ChromeSurfaceRoleForResolvedTokens(SurfaceSlot::kActivePage);
    const std::optional<CefRefPtr<CefImage>> plus =
        icon_catalog_.Load(ChromeIcon::kPlus, ChromeIconTone::kSecondary, ChromeIconSize::k16);
    const bool native = tokens_.look == LookPreset::kNative;
    // Native's sparkle is always the accent; the other looks take it only while the panel is open.
    const std::optional<CefRefPtr<CefImage>> sparkles = icon_catalog_.Load(
        ChromeIcon::kSparkles,
        agent_panel_open_ || native ? ChromeIconTone::kAccent : ChromeIconTone::kSecondary,
        ChromeIconSize::k16);
    const std::optional<CefRefPtr<CefImage>> grid =
        icon_catalog_.Load(ChromeIcon::kGrid, ChromeIconTone::kSecondary, ChromeIconSize::k16);
    const std::optional<CefRefPtr<CefImage>> gear = icon_catalog_.Load(
        ChromeIcon::kSettings,
        update_notice_.empty() ? ChromeIconTone::kSecondary : ChromeIconTone::kAccent,
        ChromeIconSize::k16);
    if (grid.has_value()) {
        tabs_button_->SetImage(CEF_BUTTON_STATE_NORMAL, *grid);
    }
    if (gear.has_value()) {
        settings_button_->SetImage(CEF_BUTTON_STATE_NORMAL, *gear);
    }
    for (const CefRefPtr<CefLabelButton>& button :
         {new_tab_button_, agent_button_, new_space_button_, tabs_button_, settings_button_}) {
        button->SetInkDropEnabled(true);
        button->SetFontList(FontList(13, ChromeFontWeight::kRegular));
        button->SetEnabledTextColors(tokens_.text_secondary.argb);
        button->SetBackgroundColor(rail.argb);
    }
    if (plus.has_value()) {
        new_tab_button_->SetImage(CEF_BUTTON_STATE_NORMAL, *plus);
        new_space_button_->SetImage(CEF_BUTTON_STATE_NORMAL, *plus);
    }
    if (sparkles.has_value()) {
        agent_button_->SetImage(CEF_BUTTON_STATE_NORMAL, *sparkles);
    }
    if (native) {
        const NativeAgentColors agent =
            NativeAgentEntryColors(tokens_, agent_panel_open_, native_agent_hovered_);
        agent_button_->SetBackgroundColor(agent.fill.argb);
        agent_button_->SetEnabledTextColors(agent.text.argb);
    } else {
        // The agent toggle lifts to the surface (and takes the accent) while the
        // panel is open, like an active tab.
        agent_button_->SetBackgroundColor(agent_panel_open_ ? surface.argb : rail.argb);
        agent_button_->SetEnabledTextColors(agent_panel_open_ ? tokens_.accent.argb
                                                              : tokens_.text_secondary.argb);
    }
}

// Entry rows sit on the rail when inactive; the active tab and space take the
// surface fill with a hairline outline (DESIGN.md §5). Re-asserted here (not through a
// SurfacePanelDelegate) because the tint depends on the entry's active state,
// which changes independently of the theme.
void BrowserChrome::ApplyCollectionTheme() {
    CEF_REQUIRE_UI_THREAD();
    ApplyCollapsedRailTheme();
    for (const CefRefPtr<CefLabelButton>& header :
         {favorites_section_header_, tabs_section_header_}) {
        if (header != nullptr) {
            header->SetBackgroundColor(tokens_.canvas.argb);
            header->SetEnabledTextColors(tokens_.text_secondary.argb);
            header->SetFontList(SectionHeaderFontList(tokens_));
        }
    }
    for (const CefRefPtr<CefPanel>& panel : group_kicker_panels_) {
        panel->SetBackgroundColor(tokens_.canvas.argb);
    }
    for (const CefRefPtr<CefLabelButton>& kicker : group_kickers_) {
        kicker->SetBackgroundColor(tokens_.canvas.argb);
        kicker->SetTextColor(CEF_BUTTON_STATE_DISABLED, tokens_.text_secondary.argb);
        kicker->SetFontList(FontList(GroupedTabKickerFontPx(), ChromeFontWeight::kRegular));
    }
    for (std::size_t index = 0; index < tab_entry_views_.size() && index < tab_entries_.size();
         ++index) {
        ApplyTabRowVisual(index);
    }
    for (std::size_t index = 0; index < space_entry_views_.size() && index < space_entries_.size();
         ++index) {
        const SpaceSwitcherEntrySnapshot& entry = space_entries_[index];
        const SpaceEntryViews& views = space_entry_views_[index];
        const EntryColors colors = SpaceChipColors(tokens_, entry.active);
        views.row->SetBackgroundColor(colors.outline.argb);
        views.inner->SetBackgroundColor(colors.fill.argb);
        views.name->SetBackgroundColor(colors.fill.argb);
        if (views.color_mark != nullptr) {
            views.color_mark->SetBackgroundColor(entry.color.argb);
        }
        views.name->SetEnabledTextColors(colors.title.argb);
    }
}

// The row's fill: `selected` for the active tab, `hover` under the pointer, none otherwise.
namespace {
std::optional<ArgbColor> RowFill(const ChromeTokens& tokens, bool active, bool pinned,
                                 bool hovered) {
    if (active) {
        return ResolveRowColors(tokens, /*selected=*/true, hovered, /*slept=*/false).fill;
    }
    if (hovered) {
        return ResolveRowColors(tokens, /*selected=*/false, /*hovered=*/true, /*slept=*/false).fill;
    }
    if (pinned && tokens.look != LookPreset::kNative) {
        return MixColor(tokens.canvas, tokens.selected, 0.5);
    }
    return std::nullopt;
}
}  // namespace

void BrowserChrome::ApplyTabRowVisual(std::size_t index) {
    if (index >= tab_entry_views_.size() || index >= tab_entries_.size()) {
        return;
    }
    const TabStripEntrySnapshot& entry = tab_entries_[index];
    const TabEntryViews& views = tab_entry_views_[index];
    const EntryColors colors = TabRowColors(tokens_, entry.active, entry.pinned);
    const ArgbColor bg =
        RowFill(tokens_, entry.active, entry.pinned, views.hovered).value_or(tokens_.canvas);
    const RowColors row_colors =
        ResolveRowColors(tokens_, entry.active, views.hovered, entry.sleeping);
    views.row->SetBackgroundColor(bg.argb);
    views.inner->SetBackgroundColor(bg.argb);
    views.favicon->SetBackgroundColor(bg.argb);
    views.title->SetBackgroundColor(bg.argb);
    views.close->SetBackgroundColor(bg.argb);
    views.title->SetEnabledTextColors(
        (tokens_.look == LookPreset::kNative
             ? row_colors.title
             : (entry.sleeping ? SleepingTitleColor(colors.title, bg) : colors.title))
            .argb);
    views.close->SetEnabledTextColors(colors.trailing.argb);
    if (views.favicon_source != nullptr) {
        const CefRefPtr<CefImage> source =
            entry.sleeping ? DimmedImage(views.favicon_source, kSleepingIconAlpha)
                           : views.favicon_source;
        SetAllStateImages(views.favicon,
                          BuildFaviconImage(source, views.favicon_is_page, tokens_.plate));
    }
    if (views.trailing_source != nullptr) {
        SetAllStateImages(views.close, views.trailing_source);
    }
}

void BrowserChrome::ApplyCollapsedTabVisual(std::size_t position) {
    if (position >= collapsed_tab_views_.size()) {
        return;
    }
    const CollapsedTabViews& views = collapsed_tab_views_[position];
    if (views.source_index >= tab_entries_.size()) {
        return;
    }
    const TabStripEntrySnapshot& entry = tab_entries_[views.source_index];
    const EntryColors colors = TabRowColors(tokens_, entry.active, entry.pinned);
    const ArgbColor bg =
        RowFill(tokens_, entry.active, entry.pinned, views.hovered).value_or(tokens_.canvas);
    views.row->SetBackgroundColor(bg.argb);
    views.inner->SetBackgroundColor(bg.argb);
    views.button->SetBackgroundColor(bg.argb);
    const RowColors row_colors =
        ResolveRowColors(tokens_, entry.active, views.hovered, entry.sleeping);
    views.button->SetEnabledTextColors(
        (tokens_.look == LookPreset::kNative
             ? row_colors.title
             : (entry.sleeping ? SleepingTitleColor(colors.title, bg) : colors.title))
            .argb);
    if (views.favicon_source != nullptr) {
        const CefRefPtr<CefImage> source =
            entry.sleeping ? DimmedImage(views.favicon_source, kSleepingIconAlpha)
                           : views.favicon_source;
        SetAllStateImages(views.button,
                          BuildFaviconImage(source, views.favicon_is_page, tokens_.plate));
    }
}

namespace {

struct WindowRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;
};

WindowRect InWindow(const CefRefPtr<CefView>& view) {
    CefPoint origin(0, 0);
    view->ConvertPointToWindow(origin);
    const CefSize size = view->GetSize();
    return {origin.x, origin.y, size.width, size.height};
}

bool Contains(const WindowRect& outer, const WindowRect& inner) {
    return inner.x >= outer.x && inner.y >= outer.y &&
           inner.x + inner.width <= outer.x + outer.width &&
           inner.y + inner.height <= outer.y + outer.height;
}

}  // namespace

std::vector<CornerPatch> BrowserChrome::ComputeCornerPatches() const {
    std::vector<CornerPatch> patches;
    if (detached_ || root_ == nullptr) {
        return patches;
    }
    const auto add = [&patches](const WindowRect& rect) {
        if (rect.width > 0 && rect.height > 0) {
            patches.push_back({rect.x, rect.y, rect.width, rect.height, kRowFillRadiusDip});
        }
    };
    const auto filled = [this](const TabStripEntrySnapshot& entry, bool hovered) {
        return RowFill(tokens_, entry.active, entry.pinned, hovered).has_value();
    };
    const WindowRect address = InWindow(address_row_);
    if (const std::optional<CornerPatch> patch = AddressCornerPatch(
            tokens_,
            {.x = address.x, .y = address.y, .width = address.width, .height = address.height},
            sidebar_expanded_)) {
        patches.push_back(*patch);
    }
    if (tokens_.look == LookPreset::kNative && sidebar_expanded_) {
        const WindowRect agent = InWindow(agent_button_);
        if (agent_panel_open_ || native_agent_hovered_) {
            patches.push_back({agent.x, agent.y, agent.width, agent.height, kRowFillRadiusDip});
        }
        const WindowRect track = InWindow(space_switcher_);
        if (track.width > 0 && track.height > 0) {
            patches.push_back({track.x, track.y, track.width, track.height, kRowFillRadiusDip});
        }
        // The selected segment sits on the track, so its corners are repainted in the track fill.
        for (std::size_t index = 0;
             index < space_entry_views_.size() && index < space_entries_.size(); ++index) {
            if (!space_entries_[index].active) {
                continue;
            }
            const WindowRect segment = InWindow(space_entry_views_[index].row);
            if (segment.width > 0 && segment.height > 0) {
                patches.push_back({segment.x, segment.y, segment.width, segment.height,
                                   kNativeSegmentRadiusDip, tokens_.fill});
            }
        }
    }
    if (sidebar_expanded_) {
        const WindowRect viewport = InWindow(tab_list_scroll_);
        for (std::size_t index = 0; index < tab_entry_views_.size() && index < tab_entries_.size();
             ++index) {
            if (!filled(tab_entries_[index], tab_entry_views_[index].hovered)) {
                continue;
            }
            const WindowRect rect = InWindow(tab_entry_views_[index].row);
            if (Contains(viewport, rect)) {
                add(rect);
            }
        }
    } else {
        const WindowRect viewport = InWindow(collapsed_tab_scroll_);
        for (const CollapsedTabViews& views : collapsed_tab_views_) {
            if (views.source_index >= tab_entries_.size() ||
                !filled(tab_entries_[views.source_index], views.hovered)) {
                continue;
            }
            const WindowRect rect = InWindow(views.row);
            if (Contains(viewport, rect)) {
                add(rect);
            }
        }
    }
    return patches;
}

void BrowserChrome::SetCornerPatchSink(std::function<void(const std::vector<CornerPatch>&)> sink) {
    corner_sink_ = std::move(sink);
    last_patches_.clear();
    PushCornerPatches();
#if defined(__APPLE__)
    // Only macOS has a native overlay to feed; the poll follows scrolling lists.
    SchedulePatchPoll();
#endif
}

void BrowserChrome::PushCornerPatches() {
    if (!corner_sink_ || detached_) {
        return;
    }
    std::vector<CornerPatch> patches = ComputeCornerPatches();
    if (patches == last_patches_) {
        return;
    }
    last_patches_ = patches;
    corner_sink_(patches);
}

void BrowserChrome::SchedulePatchPoll() {
    if (patch_poll_pending_ || detached_ || !corner_sink_) {
        return;
    }
    patch_poll_pending_ = true;
    CefPostDelayedTask(TID_UI,
                       CefCreateClosureTask(base::BindOnce(
                           [](std::shared_ptr<BrowserChrome*> alive) {
                               if (BrowserChrome* chrome = *alive) {
                                   chrome->patch_poll_pending_ = false;
                                   chrome->PushCornerPatches();
                                   chrome->SchedulePatchPoll();
                               }
                           },
                           alive_)),
                       kPatchPollMs);
}

void BrowserChrome::RefreshRowHover() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    const auto over = [](const CefRefPtr<CefLabelButton>& button) {
        const cef_button_state_t state = button->GetState();
        return state == CEF_BUTTON_STATE_HOVERED || state == CEF_BUTTON_STATE_PRESSED;
    };
    for (std::size_t index = 0; index < tab_entry_views_.size(); ++index) {
        TabEntryViews& views = tab_entry_views_[index];
        const bool hovered = over(views.favicon) || over(views.title) || over(views.close);
        if (hovered != views.hovered) {
            views.hovered = hovered;
            ApplyTabRowVisual(index);
        }
    }
    for (std::size_t position = 0; position < collapsed_tab_views_.size(); ++position) {
        CollapsedTabViews& views = collapsed_tab_views_[position];
        const bool hovered = over(views.button);
        if (hovered != views.hovered) {
            views.hovered = hovered;
            ApplyCollapsedTabVisual(position);
        }
    }
    // Button state callbacks can arrive while the sidebar is still being built, before the
    // Agent entry exists; it has no hover to track until then.
    const bool agent_hovered =
        tokens_.look == LookPreset::kNative && agent_button_ != nullptr && over(agent_button_);
    if (agent_hovered != native_agent_hovered_) {
        native_agent_hovered_ = agent_hovered;
        ApplyRailButtonTheme();
    }
    PushCornerPatches();
}

// The icon rail re-asserts everything it owns: the surfaces, the hairlines, the "+" glyph,
// the tab buttons (active = surface fill + hairline, like the expanded row) and the space
// dots, whose ring color follows the theme.
void BrowserChrome::ApplyCollapsedRailTheme() {
    CEF_REQUIRE_UI_THREAD();
    const ArgbColor rail = ChromeSurfaceRoleForResolvedTokens(SurfaceSlot::kRail);
    const ArgbColor hairline = ChromeSurfaceRoleForResolvedTokens(SurfaceSlot::kHairline);
    collapsed_rail_->SetBackgroundColor(rail.argb);
    collapsed_tab_scroll_->SetBackgroundColor(rail.argb);
    collapsed_tab_list_->SetBackgroundColor(rail.argb);
    collapsed_bottom_group_->SetBackgroundColor(rail.argb);
    collapsed_space_list_->SetBackgroundColor(rail.argb);
    collapsed_divider_wrapper_->SetBackgroundColor(rail.argb);
    collapsed_divider_->SetBackgroundColor(hairline.argb);
    if (collapsed_pinned_separator_wrapper_ != nullptr) {
        collapsed_pinned_separator_wrapper_->SetBackgroundColor(rail.argb);
        collapsed_pinned_separator_->SetBackgroundColor(hairline.argb);
    }
    collapsed_new_tab_button_->SetBackgroundColor(rail.argb);
    collapsed_new_tab_button_->SetInkDropEnabled(true);
    collapsed_new_tab_button_->SetEnabledTextColors(tokens_.text_secondary.argb);
    if (const std::optional<CefRefPtr<CefImage>> plus =
            icon_catalog_.Load(ChromeIcon::kPlus, ChromeIconTone::kSecondary, ChromeIconSize::k16);
        plus.has_value()) {
        collapsed_new_tab_button_->SetImage(CEF_BUTTON_STATE_NORMAL, *plus);
    }
    for (const CollapsedTabViews& views : collapsed_tab_views_) {
        if (views.source_index >= tab_entries_.size()) {
            continue;
        }
        ApplyCollapsedTabVisual(static_cast<std::size_t>(&views - collapsed_tab_views_.data()));
    }
    for (const CollapsedSpaceViews& views : collapsed_space_views_) {
        if (views.source_index >= space_entries_.size()) {
            continue;
        }
        const SpaceSwitcherEntrySnapshot& entry = space_entries_[views.source_index];
        if (tokens_.look == LookPreset::kNative) {
            const ArgbColor fill = entry.active ? tokens_.fill : rail;
            views.button->SetBackgroundColor(fill.argb);
            SetAllStateImages(views.button, nullptr);
            views.button->SetText(
                SpacePillLabel({.color = entry.color, .name = entry.name, .active = false}));
            views.button->SetEnabledTextColors(
                (entry.active ? tokens_.text : tokens_.text_secondary).argb);
            views.button->SetFontList(FontList(
                12, entry.active ? ChromeFontWeight::kMedium : ChromeFontWeight::kRegular));
        } else {
            views.button->SetText("");
            views.button->SetBackgroundColor(rail.argb);
            views.button->SetImage(
                CEF_BUTTON_STATE_NORMAL,
                MakeSpaceDotImage(entry.color, entry.active ? tokens_.text : tokens_.text_secondary,
                                  entry.active));
        }
    }
}

// The address outline is the hairline at rest and the accent while the field has
// focus (DESIGN.md §8: visible accent focus ring).
void BrowserChrome::UpdateAddressFocusLeadingEdge() {
    CEF_REQUIRE_UI_THREAD();
    if (address_row_ == nullptr) {
        return;
    }
    address_row_->SetBackgroundColor(ResolveSurfaceColor(SurfaceSlot::kAddressOutline).argb);
}

ArgbColor BrowserChrome::ResolveSurfaceColor(SurfaceSlot slot) const {
    if (slot == SurfaceSlot::kAddressOutline) {
        return AddressOutlineColor(tokens_,
                                   address_field_ != nullptr && address_field_->HasFocus());
    }
    return ChromeSurfaceRoleForResolvedTokens(slot);
}

}  // namespace island
