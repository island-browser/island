#include "space_icon_cache.h"

#include <gtest/gtest.h>

#include <optional>
#include <string>

namespace island {
namespace {

TEST(SpaceIconCache, SecondLookupWithTheSameKeyDoesNotReload) {
    SpaceIconCache<std::string> cache;
    int loads = 0;
    const auto load = [&]() -> std::optional<std::string> {
        ++loads;
        return "image";
    };
    EXPECT_EQ(cache.GetOrLoad("rocket", 16, 0xFF112233U, load), "image");
    EXPECT_EQ(cache.GetOrLoad("rocket", 16, 0xFF112233U, load), "image");
    EXPECT_EQ(loads, 1);
    EXPECT_EQ(cache.size(), 1U);
}

TEST(SpaceIconCache, DifferentIdSizeOrColorLoadSeparately) {
    SpaceIconCache<std::string> cache;
    int loads = 0;
    const auto load = [&]() -> std::optional<std::string> {
        ++loads;
        return "image";
    };
    static_cast<void>(cache.GetOrLoad("rocket", 16, 1U, load));
    static_cast<void>(cache.GetOrLoad("home", 16, 1U, load));
    static_cast<void>(cache.GetOrLoad("rocket", 15, 1U, load));
    static_cast<void>(cache.GetOrLoad("rocket", 16, 2U, load));
    EXPECT_EQ(loads, 4);
}

TEST(SpaceIconCache, FailedLoadsAreRetriedAndClearDropsEntries) {
    SpaceIconCache<std::string> cache;
    int loads = 0;
    EXPECT_FALSE(cache.GetOrLoad("rocket", 16, 1U, [&]() -> std::optional<std::string> {
        ++loads;
        return std::nullopt;
    }));
    EXPECT_FALSE(cache.GetOrLoad("rocket", 16, 1U, [&]() -> std::optional<std::string> {
        ++loads;
        return std::nullopt;
    }));
    EXPECT_EQ(loads, 2);
    EXPECT_EQ(cache.size(), 0U);

    static_cast<void>(
        cache.GetOrLoad("rocket", 16, 1U, [] { return std::optional<std::string>("x"); }));
    cache.Clear();
    EXPECT_EQ(cache.size(), 0U);
}

}  // namespace
}  // namespace island
