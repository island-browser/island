#include "icon_catalog.h"

#include <gtest/gtest.h>

#include <optional>
#include <string_view>

#include "design_tokens.h"

namespace island {
namespace {

// The reader stands in for the PNG read and recolor; a null CefImage is enough for the cache.
IconCatalog MakeCatalog(int& reads) {
    return IconCatalog{"unused",
                       [&reads](std::string_view, ArgbColor,
                                ChromeIconSize) -> std::optional<CefRefPtr<CefImage>> {
                           ++reads;
                           return CefRefPtr<CefImage>();
                       }};
}

// Repeated space-icon lookups with the same (id, size, color) must not read the PNGs again; this is
// what keeps ordinary chrome updates off the disk.
TEST(IconCatalogSpaceIcons, RepeatedLookupsWithTheSameKeyDoNotReloadFromDisk) {
    int reads = 0;
    const IconCatalog catalog = MakeCatalog(reads);
    const ArgbColor ink{0xFF112233U};
    ASSERT_TRUE(catalog.LoadSpaceIcon("home", ink, ChromeIconSize::k16).has_value());
    EXPECT_EQ(reads, 1);
    constexpr int kUpdates = 5;
    for (int update = 0; update < kUpdates; ++update) {
        EXPECT_TRUE(catalog.LoadSpaceIcon("home", ink, ChromeIconSize::k16).has_value());
    }
    EXPECT_EQ(reads, 1);
    EXPECT_EQ(catalog.space_icon_reads(), 1);

    // A different color or size is a different key and reads once more.
    EXPECT_TRUE(
        catalog.LoadSpaceIcon("home", ArgbColor{0xFF445566U}, ChromeIconSize::k16).has_value());
    EXPECT_TRUE(catalog.LoadSpaceIcon("home", ink, ChromeIconSize::k15).has_value());
    EXPECT_EQ(reads, 3);
}

TEST(IconCatalogSpaceIcons, UnknownIdsNeverReachTheReader) {
    int reads = 0;
    const IconCatalog catalog = MakeCatalog(reads);
    EXPECT_FALSE(catalog.LoadSpaceIcon("../etc/passwd", ArgbColor{0xFF112233U}, ChromeIconSize::k16)
                     .has_value());
    EXPECT_EQ(reads, 0);
}

}  // namespace
}  // namespace island
