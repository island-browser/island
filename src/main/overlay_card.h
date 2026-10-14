#ifndef ISLAND_OVERLAY_CARD_H_
#define ISLAND_OVERLAY_CARD_H_

#include <cstdint>
#include <vector>

#include "design_tokens.h"
#include "include/cef_base.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_panel_delegate.h"
#include "include/views/cef_view.h"

namespace island {

// A solid-color panel that re-asserts its color when CEF resets view colors on a
// theme change. Used for the translucent shadow rings and scrims of overlay cards,
// which have no owner-specific state.
class OverlayFillDelegate final : public CefPanelDelegate {
  public:
    explicit OverlayFillDelegate(ArgbColor color, CefSize preferred_size = CefSize())
        : color_(color), preferred_size_(preferred_size) {}

    void SetColor(ArgbColor color) { color_ = color; }
    void SetPreferredSize(CefSize size) { preferred_size_ = size; }

    CefSize GetPreferredSize(CefRefPtr<CefView>) override { return preferred_size_; }

    void OnThemeChanged(CefRefPtr<CefView> view) override { view->SetBackgroundColor(color_.argb); }

  private:
    ArgbColor color_;
    CefSize preferred_size_;

    IMPLEMENT_REFCOUNTING(OverlayFillDelegate);
};

// Dark tokens carry light text and light tokens dark text, so the text channel tells them
// apart even after a space tint (which never touches text).
[[nodiscard]] inline ChromeTheme ThemeOfTokens(const ChromeTokens& tokens) {
    return (tokens.text.argb & 0xFFU) > 0x80U ? ChromeTheme::kDark : ChromeTheme::kLight;
}

// The one card shadow DESIGN.md §7 allows ("0 1px 2px plus 0 8px 24px", low-opacity
// neutral), drawn for CEF views, which have no shadow or blur API: the card is wrapped in
// nested translucent rings whose alphas accumulate toward the card, so the shadow fades
// out with distance. The rings extend further below the card than above it, which gives
// the downward offset. The overlay that hosts a card is therefore larger than the card by
// the amounts below, and the extra area is transparent except for the rings.
class OverlayShadow {
  public:
    static constexpr int kSteps = 10;
    // Cumulative extent of the rings on each side of the card, in DIP.
    static constexpr int kSideDip = kSteps;
    static constexpr int kTopDip = kSteps;
    static constexpr int kBottomDip = kSteps * 2;

    // Overlay bounds that hold a card placed at |card| (window-relative DIP).
    [[nodiscard]] static CefRect OverlayBoundsForCard(const CefRect& card) {
        return CefRect(card.x - kSideDip, card.y - kTopDip, card.width + 2 * kSideDip,
                       card.height + kTopDip + kBottomDip);
    }

    // The ring color for each step: black at an alpha that sums to about 8% at the card
    // edge in light, and a deeper but still low-opacity step in dark.
    [[nodiscard]] static ArgbColor RingColor(ChromeTheme theme) {
        return {.argb = theme == ChromeTheme::kDark ? 0x07000000U : 0x02000000U};
    }

    // Wraps |inner| in the rings and returns the outermost panel (the overlay root).
    CefRefPtr<CefPanel> Wrap(CefRefPtr<CefPanel> inner, ChromeTheme theme) {
        CefRefPtr<CefPanel> current = inner;
        for (int step = 0; step < kSteps; ++step) {
            CefRefPtr<OverlayFillDelegate> delegate = new OverlayFillDelegate(RingColor(theme));
            CefRefPtr<CefPanel> ring = CefPanel::CreatePanel(delegate);
            CefBoxLayoutSettings settings;
            settings.horizontal = static_cast<int>(true);
            settings.between_child_spacing = 0;
            // Each ring is one DIP wider than the next on the sides and top and two DIP
            // taller below, which accumulates to kSide/kTop/kBottom.
            settings.inside_border_insets = CefInsets(1, 1, 2, 1);
            CefRefPtr<CefBoxLayout> layout = ring->SetToBoxLayout(settings);
            ring->AddChildView(current);
            layout->SetFlexForView(current, 1);
            ring->SetBackgroundColor(RingColor(theme).argb);
            delegates_.push_back(delegate);
            rings_.push_back(ring);
            current = ring;
        }
        return current;
    }

    // The size the outermost ring asks its parent layout for: the card plus the ring extents.
    void SetCardSize(int card_width, int card_height) {
        if (!delegates_.empty()) {
            delegates_.back()->SetPreferredSize(
                CefSize(card_width + 2 * kSideDip, card_height + kTopDip + kBottomDip));
        }
    }

    void Apply(ChromeTheme theme) {
        for (std::size_t index = 0; index < rings_.size(); ++index) {
            delegates_[index]->SetColor(RingColor(theme));
            rings_[index]->SetBackgroundColor(RingColor(theme).argb);
        }
    }

  private:
    std::vector<CefRefPtr<OverlayFillDelegate>> delegates_;
    std::vector<CefRefPtr<CefPanel>> rings_;
};

// A palette-style overlay card: the content panel inside a 1 DIP hairline inside the shadow.
// The caller creates and destroys the overlay itself and passes Wrap()'s result as its root
// view; the card re-colors through Apply() on every theme change.
class OverlayCard {
  public:
    static constexpr int kHairlineDip = 1;

    // Overlay bounds for a content panel placed at |content| (window-relative DIP).
    [[nodiscard]] static CefRect OverlayBoundsForContent(const CefRect& content) {
        return OverlayShadow::OverlayBoundsForCard(CefRect(content.x - kHairlineDip,
                                                           content.y - kHairlineDip,
                                                           content.width + 2 * kHairlineDip,
                                                           content.height + 2 * kHairlineDip));
    }
    // The overlay's own height for a content height (its width is content + the extents).
    [[nodiscard]] static int OverlayHeightForContent(int content_height) {
        return content_height + 2 * kHairlineDip + OverlayShadow::kTopDip +
               OverlayShadow::kBottomDip;
    }
    [[nodiscard]] static int OverlayWidthForContent(int content_width) {
        return content_width + 2 * kHairlineDip + 2 * OverlayShadow::kSideDip;
    }

    CefRefPtr<CefPanel> Wrap(CefRefPtr<CefPanel> content, const ChromeTokens& tokens) {
        outline_delegate_ = new OverlayFillDelegate(tokens.border);
        outline_ = CefPanel::CreatePanel(outline_delegate_);
        CefBoxLayoutSettings settings;
        settings.horizontal = static_cast<int>(true);
        settings.between_child_spacing = 0;
        settings.inside_border_insets =
            CefInsets(kHairlineDip, kHairlineDip, kHairlineDip, kHairlineDip);
        CefRefPtr<CefBoxLayout> layout = outline_->SetToBoxLayout(settings);
        outline_->AddChildView(content);
        layout->SetFlexForView(content, 1);
        outline_->SetBackgroundColor(tokens.border.argb);
        return shadow_.Wrap(outline_, ThemeOfTokens(tokens));
    }

    void Apply(const ChromeTokens& tokens) {
        shadow_.Apply(ThemeOfTokens(tokens));
        if (outline_delegate_ != nullptr) {
            outline_delegate_->SetColor(tokens.border);
            outline_->SetBackgroundColor(tokens.border.argb);
        }
    }

    // Sizes the outermost ring for a content panel of the given size.
    void SetContentSize(int content_width, int content_height) {
        shadow_.SetCardSize(content_width + 2 * kHairlineDip, content_height + 2 * kHairlineDip);
    }

  private:
    OverlayShadow shadow_;
    CefRefPtr<OverlayFillDelegate> outline_delegate_;
    CefRefPtr<CefPanel> outline_;
};

}  // namespace island

#endif  // ISLAND_OVERLAY_CARD_H_
