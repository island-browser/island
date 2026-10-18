#ifndef ISLAND_ICON_CATALOG_H_
#define ISLAND_ICON_CATALOG_H_

#include <filesystem>
#include <optional>
#include <string_view>

#include "design_tokens.h"
#include "include/cef_image.h"
#include "space_icon_cache.h"

namespace island {

enum class ChromeIcon {
    kBack,
    kForward,
    kReload,
    kLocation,
    // Arc-style rail affordances.
    kPlus,
    kSparkles,
    kPin,
    kClose,
    kGrid,
    kSettings,
};

enum class ChromeIconTone {
    kText,
    kSecondary,
    kAccent,
};

enum class ChromeIconSize {
    k13,
    k15,
    k16,
    k17,
};

class IconCatalog {
  public:
    explicit IconCatalog(std::filesystem::path resource_root);

    [[nodiscard]] std::optional<CefRefPtr<CefImage>> Load(ChromeIcon icon, ChromeIconTone tone,
                                                          ChromeIconSize size) const;
    // Space icons are stored as the same locked monochrome PNGs as chrome icons, then recolored
    // from their alpha masks so Arc and Graphite can use the exact per-space color.
    [[nodiscard]] std::optional<CefRefPtr<CefImage>> LoadSpaceIcon(std::string_view icon_id,
                                                                   ArgbColor color,
                                                                   ChromeIconSize size) const;

    // Number of space-icon loads that went to disk (cache misses), for tests and diagnostics.
    [[nodiscard]] int space_icon_disk_loads() const noexcept { return space_icon_disk_loads_; }

  private:
    [[nodiscard]] std::optional<CefRefPtr<CefImage>> ReadTintedSpaceIcon(std::string_view icon_id,
                                                                         ArgbColor color,
                                                                         ChromeIconSize size) const;

    std::filesystem::path resource_root_;
    mutable SpaceIconCache<CefRefPtr<CefImage>> space_icon_cache_;
    mutable int space_icon_disk_loads_ = 0;
};

}  // namespace island

#endif  // ISLAND_ICON_CATALOG_H_
