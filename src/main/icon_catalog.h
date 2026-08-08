#ifndef ISLAND_ICON_CATALOG_H_
#define ISLAND_ICON_CATALOG_H_

#include <filesystem>
#include <optional>

#include "include/cef_image.h"

namespace island {

enum class ChromeIcon {
    kBack,
    kForward,
    kReload,
    kLocation,
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

  private:
    std::filesystem::path resource_root_;
};

}  // namespace island

#endif  // ISLAND_ICON_CATALOG_H_
