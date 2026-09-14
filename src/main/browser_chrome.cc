#include "browser_chrome.h"

#include <string>
#include <utility>

#include "include/base/cef_bind.h"
#include "include/base/cef_callback.h"
#include "include/cef_task.h"
#include "include/views/cef_box_layout.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_button_delegate.h"
#include "include/views/cef_label_button.h"
#include "include/views/cef_panel.h"
#include "include/views/cef_textfield.h"
#include "include/views/cef_textfield_delegate.h"
#include "include/wrapper/cef_closure_task.h"
#include "include/wrapper/cef_helpers.h"

// allow: SIZE_OK — the CEF delegate callbacks must share one composition owner.
namespace island {
namespace {

constexpr int kReturnKeyCode = 0x0D;
constexpr int kEscapeKeyCode = 0x1B;

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

int ControlHeight(const ChromeTokens& tokens) {
    return tokens.spacing_6_dip + tokens.spacing_4_dip;
}

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

CefBoxLayoutSettings HorizontalLayout(const ChromeTokens& tokens) {
    CefBoxLayoutSettings settings;
    settings.horizontal = static_cast<int>(true);
    settings.between_child_spacing = tokens.spacing_2_dip;
    settings.inside_border_horizontal_spacing = tokens.spacing_3_dip;
    settings.inside_border_vertical_spacing = tokens.spacing_2_dip;
    return settings;
}

// The browser_content panel holds the single CefBrowserView with a uniform
// floating-canvas gutter on every side. CEF cannot round or clip the BrowserView,
// so the floating read comes entirely from this rectangular inset: the tinted
// background shows through the gap and the page reads as a raised card. No spacing
// between the panel and the view beyond the gutter.
CefBoxLayoutSettings FloatingCanvasLayout(const ChromeTokens& tokens) {
    const int pad = BrowserChrome::BrowserContentPaddingDip();
    CefBoxLayoutSettings settings;
    settings.horizontal = static_cast<int>(true);
    settings.between_child_spacing = tokens.spacing_1_dip - tokens.spacing_1_dip;
    settings.inside_border_insets = CefInsets(pad, pad, pad, pad);
    return settings;
}

// The rail uses a dedicated layout: a top inset (RailTopInsetDip) reserves the
// platform title-bar / traffic-light region, and a calmer between-section step
// (RailSectionSpacingDip) replaces the cramped generic cadence. Side and bottom
// insets stay on spacing_3 so the rail padding stays consistent.
CefBoxLayoutSettings RailLayout(const ChromeTokens& tokens) {
    CefBoxLayoutSettings settings;
    settings.horizontal = static_cast<int>(false);
    settings.between_child_spacing = BrowserChrome::RailSectionSpacingDip();
    settings.inside_border_insets =
        CefInsets(BrowserChrome::RailTopInsetDip(), tokens.spacing_3_dip, tokens.spacing_3_dip,
                  tokens.spacing_3_dip);
    return settings;
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

class BrowserChrome::PanelDelegate final : public CefPanelDelegate {
  public:
    PanelDelegate(CefSize preferred_size, CefSize minimum_size = CefSize(),
                  CefSize maximum_size = CefSize())
        : preferred_size_(preferred_size),
          minimum_size_(minimum_size),
          maximum_size_(maximum_size) {}

    CefSize GetPreferredSize(CefRefPtr<CefView>) override { return preferred_size_; }
    CefSize GetMinimumSize(CefRefPtr<CefView>) override { return minimum_size_; }
    CefSize GetMaximumSize(CefRefPtr<CefView>) override { return maximum_size_; }

  private:
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

    CefSize GetPreferredSize(CefRefPtr<CefView>) override { return preferred_size_; }
    CefSize GetMinimumSize(CefRefPtr<CefView>) override { return minimum_size_; }
    CefSize GetMaximumSize(CefRefPtr<CefView>) override { return maximum_size_; }

    void OnThemeChanged(CefRefPtr<CefView> view) override {
        if (chrome_ != nullptr) {
            view->SetBackgroundColor(chrome_->ResolveSurfaceColor(slot_).argb);
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
    explicit RootPanelDelegate(ChromeTokens tokens) : tokens_(tokens) {}

    void SetChildren(CefRefPtr<CefPanel> rail, CefRefPtr<CefPanel> browser_content) {
        rail_ = rail;
        browser_content_ = browser_content;
    }

    void SetTokens(ChromeTokens tokens) { tokens_ = tokens; }

    void SetSidebarRevealed(bool revealed) { sidebar_revealed_ = revealed; }

    void OnLayoutChanged(CefRefPtr<CefView>, const CefRect& new_bounds) override {
        if (rail_ == nullptr || browser_content_ == nullptr) {
            return;
        }

        const ChromeGeometrySnapshot geometry =
            BrowserChrome::LayoutForBounds({.x = new_bounds.x,
                                            .y = new_bounds.y,
                                            .width = new_bounds.width,
                                            .height = new_bounds.height},
                                           tokens_, sidebar_revealed_);
        ApplyBounds(rail_, geometry.rail_bounds);
        ApplyBounds(browser_content_, geometry.browser_content_bounds);
        rail_->Layout();
        browser_content_->Layout();
    }

  private:
    static void ApplyBounds(CefRefPtr<CefPanel> panel, const DipRect& bounds) {
        const CefRect target(bounds.x, bounds.y, bounds.width, bounds.height);
        if (panel->GetBounds() != target) {
            panel->SetBounds(target);
        }
    }

    ChromeTokens tokens_;
    bool sidebar_revealed_ = kSidebarRevealedByDefault;
    CefRefPtr<CefPanel> rail_;
    CefRefPtr<CefPanel> browser_content_;

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

    CefSize GetPreferredSize(CefRefPtr<CefView>) override { return preferred_size_; }

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
      browser_view_(browser_view) {
    CEF_REQUIRE_UI_THREAD();

    const int control_height = ControlHeight(tokens_);
    root_delegate_ = new RootPanelDelegate(tokens_);
    root_ = CefPanel::CreatePanel(root_delegate_);
    root_->SetID(static_cast<int>(ChromeViewId::kRoot));

    const CefSize rail_size(tokens_.rail_width_dip, 0);
    CefRefPtr<SurfacePanelDelegate> rail_delegate =
        new SurfacePanelDelegate(this, SurfaceSlot::kRail, rail_size, rail_size, rail_size);
    surface_delegates_.push_back(rail_delegate);
    rail_delegate_ = rail_delegate;
    sidebar_ = CefPanel::CreatePanel(rail_delegate);
    sidebar_->SetID(static_cast<int>(ChromeViewId::kRail));
    sidebar_->SetToBoxLayout(RailLayout(tokens_));

    CefRefPtr<SurfacePanelDelegate> navigation_row_delegate =
        new SurfacePanelDelegate(this, SurfaceSlot::kRail);
    surface_delegates_.push_back(navigation_row_delegate);
    CefRefPtr<CefPanel> navigation_row = CefPanel::CreatePanel(navigation_row_delegate);
    navigation_row->SetID(static_cast<int>(ChromeViewId::kNavigationRow));
    navigation_row_ = navigation_row;
    navigation_row->SetToBoxLayout(HorizontalLayout(tokens_));

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
        new SurfacePanelDelegate(this, SurfaceSlot::kRail);
    surface_delegates_.push_back(address_row_delegate);
    CefRefPtr<CefPanel> address_row = CefPanel::CreatePanel(address_row_delegate);
    address_row->SetID(static_cast<int>(ChromeViewId::kAddressRow));
    address_row_ = address_row;
    CefRefPtr<CefBoxLayout> address_layout = address_row->SetToBoxLayout(HorizontalLayout(tokens_));

    address_focus_leading_edge_ = CefPanel::CreatePanel(
        new PanelDelegate(CefSize(AddressFocusLeadingEdgeDip(), control_height)));
    address_row->AddChildView(address_focus_leading_edge_);

    address_location_icon_ = CefLabelButton::CreateLabelButton(button_delegate_, "");
    address_location_icon_->SetID(static_cast<int>(ChromeViewId::kAddressLocationIcon));
    address_location_icon_->SetAccessibleName("Location");
    address_location_icon_->SetTooltipText("Location");
    address_location_icon_->SetFocusable(false);
    address_location_icon_->SetMinimumSize(CefSize(control_height, control_height));
    address_row->AddChildView(address_location_icon_);

    textfield_delegate_ =
        new TextfieldDelegate(this, CefSize(tokens_.spacing_6_dip * 4, control_height));
    address_field_ = CefTextfield::CreateTextfield(textfield_delegate_);
    address_field_->SetID(static_cast<int>(ChromeViewId::kAddress));
    address_field_->SetAccessibleName("Address");
    address_field_->SetPlaceholderText("Enter address");
    address_field_->SetFocusable(true);
    address_row->AddChildView(address_field_);
    address_layout->SetFlexForView(address_field_, 1);

    reload_button_ = CefLabelButton::CreateLabelButton(button_delegate_, "");
    reload_button_->SetID(static_cast<int>(ChromeViewId::kReload));
    reload_button_->SetAccessibleName("Reload");
    reload_button_->SetTooltipText("Reload");
    reload_button_->SetFocusable(true);
    reload_button_->SetMinimumSize(CefSize(control_height, control_height));
    address_row->AddChildView(reload_button_);
    sidebar_->AddChildView(address_row);

    validation_message_ = CefLabelButton::CreateLabelButton(button_delegate_, "");
    validation_message_->SetID(static_cast<int>(ChromeViewId::kValidationMessage));
    validation_message_->SetFocusable(false);
    validation_message_->SetEnabled(false);
    validation_message_->SetVisible(false);
    sidebar_->AddChildView(validation_message_);

    CefRefPtr<SurfacePanelDelegate> tab_strip_delegate =
        new SurfacePanelDelegate(this, SurfaceSlot::kRail);
    surface_delegates_.push_back(tab_strip_delegate);
    tab_strip_ = CefPanel::CreatePanel(tab_strip_delegate);
    tab_strip_->SetID(static_cast<int>(ChromeViewId::kTabStrip));
    tab_strip_->SetToBoxLayout(CollectionLayout(tokens_));
    sidebar_->AddChildView(tab_strip_);

    // The spacer stays in its contract position between the two collection regions.
    // It carries no flex and zero preferred size, so it is inert: the rail sections
    // top-align and the divider + active-page card sit at the top of the lower group,
    // with the open tinted rail below the card reading as intentional negative space.
    CefRefPtr<SurfacePanelDelegate> spacer_delegate =
        new SurfacePanelDelegate(this, SurfaceSlot::kRail);
    surface_delegates_.push_back(spacer_delegate);
    CefRefPtr<CefPanel> spacer = CefPanel::CreatePanel(spacer_delegate);
    spacer->SetID(static_cast<int>(ChromeViewId::kSpacer));
    spacer_ = spacer;
    sidebar_->AddChildView(spacer);

    CefRefPtr<SurfacePanelDelegate> space_switcher_delegate =
        new SurfacePanelDelegate(this, SurfaceSlot::kRail);
    surface_delegates_.push_back(space_switcher_delegate);
    space_switcher_ = CefPanel::CreatePanel(space_switcher_delegate);
    space_switcher_->SetID(static_cast<int>(ChromeViewId::kSpaceSwitcher));
    space_switcher_->SetToBoxLayout(CollectionLayout(tokens_));
    sidebar_->AddChildView(space_switcher_);

    // The hairline divider and the active-page pill sit at the TOP of the lower group,
    // immediately after the (empty) collections, so the current page reads as a compact
    // card near the top instead of being pinned to the far bottom.
    CefRefPtr<CefPanel> divider = CefPanel::CreatePanel(
        new PanelDelegate(CefSize(tokens_.rail_width_dip, DividerHeight(tokens_))));
    divider->SetID(static_cast<int>(ChromeViewId::kDivider));
    divider_ = divider;
    sidebar_->AddChildView(divider);

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
        new PanelDelegate(CefSize(ActivePageIndicatorWidthDip(), control_height)));
    active_page_indicator_->SetID(static_cast<int>(ChromeViewId::kActivePageIndicator));
    active_page_->AddChildView(active_page_indicator_);
    sidebar_->AddChildView(active_page_);

    browser_content_ = CefPanel::CreatePanel(new PanelDelegate(
        CefSize(control_height, control_height), CefSize(control_height, control_height)));
    browser_content_->SetID(static_cast<int>(ChromeViewId::kBrowserContent));
    browser_content_layout_ = browser_content_->SetToBoxLayout(FloatingCanvasLayout(tokens_));
    browser_view_->SetID(static_cast<int>(ChromeViewId::kBrowserView));
    browser_content_->AddChildView(browser_view_);
    browser_content_layout_->SetFlexForView(browser_view_, 1);

    root_->AddChildView(sidebar_);
    root_->AddChildView(browser_content_);
    root_delegate_->SetChildren(sidebar_, browser_content_);
    ApplyControlTheme();
    // Apply the hidden-by-default reveal state before the first layout so no
    // frame ever shows a rail the sidebar state says is collapsed.
    SetSidebarRevealed(sidebar_revealed_);
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

ChromeViewTreeNode BrowserChrome::view_tree_snapshot() const {
    // The live projection: fixed regions from the contract, collection entries
    // from the snapshots the window last pushed, so the snapshot always matches
    // the rows actually in the view tree.
    return BrowserChrome::CollectionCountContract(tab_entries_, space_entries_);
}

ChromeGeometrySnapshot BrowserChrome::view_bounds_snapshot() const {
    CEF_REQUIRE_UI_THREAD();
    return {
        .root_bounds = ToDipRect(root_->GetBounds()),
        .rail_bounds = ToDipRect(sidebar_->GetBounds()),
        .browser_content_bounds = ToDipRect(browser_content_->GetBounds()),
        .browser_view_bounds = ToDipRect(browser_view_->GetBounds()),
    };
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
    return "Tab: " + entry.title + (entry.active ? ", active" : ", inactive");
}

std::string BrowserChrome::TabCloseAccessibleName(const TabStripEntrySnapshot& entry) {
    return "Close tab: " + entry.title;
}

std::string BrowserChrome::SpaceEntryAccessibleName(const SpaceSwitcherEntrySnapshot& entry) {
    return "Space: " + entry.name + (entry.active ? ", active" : ", inactive");
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
    tab_strip_->RemoveAllChildViews();

    const int control_height = ControlHeight(tokens_);
    const std::optional<CefRefPtr<CefImage>> fallback_icon =
        icon_catalog_.Load(ChromeIcon::kLocation, FallbackFaviconIconTone(), ChromeIconSize::k16);
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const TabStripEntrySnapshot& entry = entries[index];
        TabEntryViews views;
        views.row = CefPanel::CreatePanel(new PanelDelegate(CefSize(0, control_height)));
        views.row->SetID(static_cast<int>(ChromeViewId::kTabStripEntry));
        CefRefPtr<CefBoxLayout> row_layout = views.row->SetToBoxLayout(HorizontalLayout(tokens_));

        views.favicon = CefLabelButton::CreateLabelButton(button_delegate_, "");
        views.favicon->SetID(static_cast<int>(ChromeViewId::kTabStripEntryFavicon));
        views.favicon->SetAccessibleName("Tab favicon placeholder");
        views.favicon->SetTooltipText("Tab favicon placeholder");
        views.favicon->SetFocusable(false);
        views.favicon->SetMinimumSize(CefSize(control_height, control_height));
        if (fallback_icon.has_value()) {
            views.favicon->SetImage(CEF_BUTTON_STATE_NORMAL, *fallback_icon);
        }
        views.row->AddChildView(views.favicon);

        CefRefPtr<CollectionButtonDelegate> title_delegate = new CollectionButtonDelegate(
            this, CollectionButtonAction::kActivateTab, index);
        tab_button_delegates_.push_back(title_delegate);
        views.title = CefLabelButton::CreateLabelButton(title_delegate,
                                                        TruncateCollectionTitle(entry.title));
        views.title->SetID(static_cast<int>(ChromeViewId::kTabStripEntryTitle));
        views.title->SetAccessibleName(TabEntryAccessibleName(entry));
        views.title->SetTooltipText(entry.title);
        views.title->SetFocusable(true);
        views.title->SetMinimumSize(CefSize(tokens_.spacing_6_dip * 2, control_height));
        views.row->AddChildView(views.title);
        row_layout->SetFlexForView(views.title, 1);

        CefRefPtr<CollectionButtonDelegate> close_delegate =
            new CollectionButtonDelegate(this, CollectionButtonAction::kCloseTab, index);
        tab_button_delegates_.push_back(close_delegate);
        views.close = CefLabelButton::CreateLabelButton(close_delegate, "\xC3\x97");
        views.close->SetID(static_cast<int>(ChromeViewId::kTabStripEntryClose));
        views.close->SetAccessibleName(TabCloseAccessibleName(entry));
        views.close->SetTooltipText("Close tab");
        views.close->SetFocusable(true);
        views.close->SetMinimumSize(CefSize(control_height, control_height));
        views.row->AddChildView(views.close);

        tab_entry_views_.push_back(views);
        tab_strip_->AddChildView(views.row);
    }
    ApplyCollectionTheme();
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

    const int control_height = ControlHeight(tokens_);
    for (std::size_t index = 0; index < entries.size(); ++index) {
        const SpaceSwitcherEntrySnapshot& entry = entries[index];
        SpaceEntryViews views;
        views.row = CefPanel::CreatePanel(new PanelDelegate(CefSize(0, control_height)));
        views.row->SetID(static_cast<int>(ChromeViewId::kSpaceSwitcherEntry));
        CefRefPtr<CefBoxLayout> row_layout = views.row->SetToBoxLayout(HorizontalLayout(tokens_));

        views.color_mark = CefPanel::CreatePanel(
            new PanelDelegate(CefSize(tokens_.spacing_3_dip, tokens_.spacing_3_dip)));
        views.color_mark->SetID(static_cast<int>(ChromeViewId::kSpaceSwitcherEntryColorMark));
        views.row->AddChildView(views.color_mark);

        CefRefPtr<CollectionButtonDelegate> name_delegate = new CollectionButtonDelegate(
            this, CollectionButtonAction::kActivateSpace, index);
        space_button_delegates_.push_back(name_delegate);
        views.name = CefLabelButton::CreateLabelButton(name_delegate,
                                                       TruncateCollectionTitle(entry.name));
        views.name->SetID(static_cast<int>(ChromeViewId::kSpaceSwitcherEntryName));
        views.name->SetAccessibleName(SpaceEntryAccessibleName(entry));
        views.name->SetTooltipText(entry.name);
        views.name->SetFocusable(true);
        views.name->SetMinimumSize(CefSize(tokens_.spacing_6_dip * 2, control_height));
        views.row->AddChildView(views.name);
        row_layout->SetFlexForView(views.name, 1);

        space_entry_views_.push_back(views);
        space_switcher_->AddChildView(views.row);
    }
    ApplyCollectionTheme();
}

void BrowserChrome::AttachBrowserView(CefRefPtr<CefBrowserView> browser_view) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_ || browser_view == nullptr) {
        return;
    }
    if (browser_view_ != nullptr && browser_view->IsSame(browser_view_)) {
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
    ApplyControlTheme();
    rail_delegate_->SetFixedWidth(sidebar_revealed_ ? tokens_.rail_width_dip : 0);
    root_delegate_->OnLayoutChanged(root_, root_->GetBounds());
}

void BrowserChrome::SetSidebarRevealed(bool revealed) {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
    }
    sidebar_revealed_ = revealed;
    rail_delegate_->SetFixedWidth(revealed ? tokens_.rail_width_dip : 0);
    // The rail keeps its place in the view tree; hiding it only removes it from
    // layout, so ViewTreeContract() is unaffected.
    sidebar_->SetVisible(revealed);
    root_delegate_->SetSidebarRevealed(revealed);
    root_delegate_->OnLayoutChanged(root_, root_->GetBounds());
}

void BrowserChrome::BeginAddressEditing() {
    CEF_REQUIRE_UI_THREAD();
    if (detached_) {
        return;
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
    button_delegate_->Detach();
    textfield_delegate_->Detach();
    for (const CefRefPtr<CollectionButtonDelegate>& delegate : tab_button_delegates_) {
        delegate->Detach();
    }
    for (const CefRefPtr<CollectionButtonDelegate>& delegate : space_button_delegates_) {
        delegate->Detach();
    }
    for (const CefRefPtr<SurfacePanelDelegate>& surface_delegate : surface_delegates_) {
        surface_delegate->Detach();
    }
    if (browser_view_ != nullptr && browser_content_ != nullptr) {
        browser_content_->RemoveChildView(browser_view_);
    }
    browser_view_ = nullptr;
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
        case ChromeViewId::kActiveTab:
            host_->FocusBrowserView();
            return;
        default:
            return;
    }
}

void BrowserChrome::HandleCollectionButtonPressed(CollectionButtonAction action,
                                                  std::size_t index) {
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
    const std::string& text = editing ? snapshot.edit_text : snapshot.display_text;
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

void BrowserChrome::ApplyControlTheme() {
    const ArgbColor background = ChromeSurfaceRoleForResolvedTokens(SurfaceSlot::kRoot);
    const ArgbColor rail = ChromeSurfaceRoleForResolvedTokens(SurfaceSlot::kRail);
    const ArgbColor browser_content =
        ChromeSurfaceRoleForResolvedTokens(SurfaceSlot::kBrowserContent);
    const ArgbColor hairline = ChromeSurfaceRoleForResolvedTokens(SurfaceSlot::kHairline);
    const ArgbColor address_well = ChromeSurfaceRoleForResolvedTokens(SurfaceSlot::kAddressWell);
    const ArgbColor nav_control = ChromeSurfaceRoleForResolvedTokens(SurfaceSlot::kNavControl);
    const ArgbColor active_page_fill = ChromeSurfaceRoleForResolvedTokens(SurfaceSlot::kActivePage);
    const ArgbColor accent = ChromeSurfaceRoleForResolvedTokens(SurfaceSlot::kAccent);

    root_->SetBackgroundColor(background.argb);
    sidebar_->SetBackgroundColor(rail.argb);
    browser_content_->SetBackgroundColor(browser_content.argb);
    // Rail-descendant panels are created with no delegate and would otherwise inherit
    // the window's primary background; painting them the rail color keeps the whole
    // column one uniform tinted surface so the floating card separates cleanly.
    navigation_row_->SetBackgroundColor(rail.argb);
    address_row_->SetBackgroundColor(rail.argb);
    tab_strip_->SetBackgroundColor(rail.argb);
    space_switcher_->SetBackgroundColor(rail.argb);
    spacer_->SetBackgroundColor(rail.argb);
    divider_->SetBackgroundColor(hairline.argb);
    // The address unit is one raised surface pill: the location glyph, field, and
    // reload chip all carry the same surface fill so they read as a single control
    // group with consistent padding, matching the canonical .rail-address /
    // .rail-reload anatomy.
    address_location_icon_->SetBackgroundColor(address_well.argb);
    address_field_->SetBackgroundColor(address_well.argb);
    reload_button_->SetBackgroundColor(address_well.argb);
    // Back and Forward are quiet icon chips on the same raised step; CEF layers the
    // native hover/pressed ring over this base fill when targeted.
    back_button_->SetBackgroundColor(nav_control.argb);
    forward_button_->SetBackgroundColor(nav_control.argb);
    // The active-page card lifts to the primary surface against the tinted rail.
    active_page_->SetBackgroundColor(active_page_fill.argb);
    active_page_indicator_->SetBackgroundColor(accent.argb);
    UpdateAddressFocusLeadingEdge();
    address_field_->SetFontList("Geist Mono, 14px");
    active_tab_->SetFontList("Geist, 14px");
    back_button_->SetEnabledTextColors(tokens_.text.argb);
    forward_button_->SetEnabledTextColors(tokens_.text.argb);
    reload_button_->SetEnabledTextColors(tokens_.text.argb);
    address_location_icon_->SetEnabledTextColors(tokens_.text_secondary.argb);
    active_page_fallback_favicon_->SetEnabledTextColors(tokens_.text_secondary.argb);
    active_tab_->SetEnabledTextColors(tokens_.text.argb);
    validation_message_->SetEnabledTextColors(tokens_.accent.argb);

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
    ApplyCollectionTheme();
}

// Entry rows tint like the rail when inactive and lift to the active-page
// surface when active, so the current tab and current space read as the raised
// rows the same way the active-page card does. Re-asserted here (not through a
// SurfacePanelDelegate) because the tint depends on the entry's active state,
// which changes independently of the theme.
void BrowserChrome::ApplyCollectionTheme() {
    CEF_REQUIRE_UI_THREAD();
    const ArgbColor rail = ChromeSurfaceRoleForResolvedTokens(SurfaceSlot::kRail);
    const ArgbColor surface = ChromeSurfaceRoleForResolvedTokens(SurfaceSlot::kActivePage);
    for (std::size_t index = 0;
         index < tab_entry_views_.size() && index < tab_entries_.size(); ++index) {
        const TabStripEntrySnapshot& entry = tab_entries_[index];
        const TabEntryViews& views = tab_entry_views_[index];
        const ArgbColor row_fill = entry.active ? surface : rail;
        views.row->SetBackgroundColor(row_fill.argb);
        views.favicon->SetBackgroundColor(row_fill.argb);
        views.title->SetEnabledTextColors(tokens_.text.argb);
        views.close->SetEnabledTextColors(tokens_.text_secondary.argb);
    }
    for (std::size_t index = 0;
         index < space_entry_views_.size() && index < space_entries_.size(); ++index) {
        const SpaceSwitcherEntrySnapshot& entry = space_entries_[index];
        const SpaceEntryViews& views = space_entry_views_[index];
        views.row->SetBackgroundColor(entry.active ? surface.argb : rail.argb);
        views.color_mark->SetBackgroundColor(entry.color.argb);
        views.name->SetEnabledTextColors(tokens_.text.argb);
    }
}

void BrowserChrome::UpdateAddressFocusLeadingEdge() {
    CEF_REQUIRE_UI_THREAD();
    const ArgbColor accent = ChromeSurfaceRoleForResolvedTokens(SurfaceSlot::kAccent);
    const ArgbColor well = ChromeSurfaceRoleForResolvedTokens(SurfaceSlot::kAddressWell);
    address_focus_leading_edge_->SetBackgroundColor(address_field_->HasFocus() ? accent.argb
                                                                               : well.argb);
}

}  // namespace island
