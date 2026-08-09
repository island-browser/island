#include "app_resources.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>
#include <type_traits>

#include "font_registry.h"

namespace {

void WriteRequiredAssets(const std::filesystem::path& fonts_directory) {
    std::filesystem::create_directories(fonts_directory);
    for (const std::string_view filename : island::kRequiredFontResourceFiles) {
        std::ofstream(fonts_directory / filename) << "fixture";
    }
}

void TestPackagedFontDirectoryUsesPlatformLayouts() {
    using island::ResourcePlatform;

    assert(
        island::PackagedFontDirectory(ResourcePlatform::kMacOS,
                                      "/Applications/Island.app/Contents/MacOS/island_browser") ==
        "/Applications/Island.app/Contents/Resources/island/fonts");
    assert(island::PackagedFontDirectory(ResourcePlatform::kWindows,
                                         "C:/Island/Release/island_browser.dll") ==
           "C:/Island/Release/resources/island/fonts");
    assert(island::PackagedFontDirectory(ResourcePlatform::kLinux,
                                         "/opt/island/Release/island_browser") ==
           "/opt/island/Release/resources/island/fonts");
}

void TestPackagedIconDirectoryUsesPlatformLayouts() {
    using island::ResourcePlatform;

    assert(
        island::PackagedIconDirectory(ResourcePlatform::kMacOS,
                                      "/Applications/Island.app/Contents/MacOS/island_browser") ==
        "/Applications/Island.app/Contents/Resources/island/icons");
    assert(island::PackagedIconDirectory(ResourcePlatform::kWindows,
                                         "C:/Island/Release/island_browser.dll") ==
           "C:/Island/Release/resources/island/icons");
    assert(island::PackagedIconDirectory(ResourcePlatform::kLinux,
                                         "/opt/island/Release/island_browser") ==
           "/opt/island/Release/resources/island/icons");
}

void TestIconResourcesUseThePackagedManifestBeforeAnExplicitFallback() {
    const std::filesystem::path fixture_root =
        std::filesystem::temp_directory_path() / "island-icon-resources-packaged-test";
    const std::filesystem::path packaged_icons =
        fixture_root / "Release" / "resources" / "island" / "icons";
    const std::filesystem::path fallback_icons =
        fixture_root / "repository" / "resources" / "island" / "icons";
    std::filesystem::create_directories(packaged_icons);
    std::filesystem::create_directories(fallback_icons);
    std::ofstream(packaged_icons / "manifest.json") << "{}";
    std::ofstream(fallback_icons / "manifest.json") << "{}";

    const island::IconResources resources = island::ResolveIconResources(
        island::ResourcePlatform::kLinux, fixture_root / "Release" / "island_browser",
        fixture_root / "repository");

    assert(resources.manifest_present);
    assert(!resources.using_fallback);
    assert(resources.root == packaged_icons);
    assert(resources.manifest == packaged_icons / "manifest.json");

    std::filesystem::remove_all(fixture_root);
}

void TestIconResourcesRequireAnExplicitFallbackRoot() {
    const std::filesystem::path fixture_root =
        std::filesystem::temp_directory_path() / "island-icon-resources-missing-test";

    const island::IconResources resources =
        island::ResolveIconResources(island::ResourcePlatform::kLinux,
                                     fixture_root / "Release" / "island_browser", std::nullopt);

    assert(!resources.manifest_present);
    assert(!resources.using_fallback);
    assert(resources.manifest ==
           fixture_root / "Release" / "resources" / "island" / "icons" / "manifest.json");

    std::filesystem::remove_all(fixture_root);
}

void TestIconResourcesUseAnExplicitRepositoryFallback() {
    const std::filesystem::path fixture_root =
        std::filesystem::temp_directory_path() / "island-icon-resources-fallback-test";
    const std::filesystem::path fallback_icons =
        fixture_root / "repository" / "resources" / "island" / "icons";
    std::filesystem::create_directories(fallback_icons);
    std::ofstream(fallback_icons / "manifest.json") << "{}";

    const island::IconResources resources = island::ResolveIconResources(
        island::ResourcePlatform::kLinux, fixture_root / "Release" / "island_browser",
        fixture_root / "repository");

    assert(resources.manifest_present);
    assert(resources.using_fallback);
    assert(resources.root == fallback_icons);

    std::filesystem::remove_all(fixture_root);
}

void TestExplicitDevelopmentRootProvidesCompleteFallback() {
    const std::filesystem::path fixture_root =
        std::filesystem::temp_directory_path() / "island-app-resources-test";
    const std::filesystem::path fallback_fonts = fixture_root / "repository" / "assets" / "fonts";
    WriteRequiredAssets(fallback_fonts);

    const island::FontResources resources = island::ResolveFontResources(
        island::ResourcePlatform::kLinux, fixture_root / "Release" / "island_browser",
        fixture_root / "repository");

    assert(resources.using_fallback);
    assert(resources.missing_assets.empty());
    assert(resources.font_files.size() == 8U);
    assert(resources.missing_packaged_assets.size() == 10U);
    assert(resources.font_files.front() == fallback_fonts / "Geist-Regular.ttf");

    std::filesystem::remove_all(fixture_root);
}

void TestMissingFallbackAssetsRemainReportedWithoutCurrentDirectoryLookup() {
    const std::filesystem::path fixture_root =
        std::filesystem::temp_directory_path() / "island-app-resources-missing-test";
    const std::filesystem::path fallback_fonts = fixture_root / "repository" / "assets" / "fonts";
    std::filesystem::create_directories(fallback_fonts);
    std::ofstream(fallback_fonts / "Geist-Regular.ttf") << "fixture";

    const island::FontResources resources = island::ResolveFontResources(
        island::ResourcePlatform::kLinux, fixture_root / "Release" / "island_browser",
        fixture_root / "repository");

    assert(resources.using_fallback);
    assert(resources.font_files.empty());
    assert(resources.missing_assets.size() == 9U);
    assert(resources.missing_assets.front() == fallback_fonts / "Geist-Medium.ttf");

    std::filesystem::remove_all(fixture_root);
}

void TestRegistrationReportPreservesFallbackAndMissingAssetState() {
    const island::FontResources resources = {
        {"Geist-Regular.ttf", "OFL.txt"},
        {"Geist-Regular.ttf"},
        {"OFL.txt"},
        {"packaged/OFL.txt"},
        true,
    };

    const island::FontRegistrationReport report = island::InitialFontRegistrationReport(resources);

    assert(report.expected.size() == 2U);
    assert(report.registered.empty());
    assert(report.failures == resources.missing_assets);
    assert(report.using_fallback);
}

}  // namespace

int main() {
    static_assert(!std::is_copy_constructible_v<island::FontRegistry>);
    static_assert(!std::is_copy_assignable_v<island::FontRegistry>);
    TestPackagedFontDirectoryUsesPlatformLayouts();
    TestPackagedIconDirectoryUsesPlatformLayouts();
    TestIconResourcesUseThePackagedManifestBeforeAnExplicitFallback();
    TestIconResourcesRequireAnExplicitFallbackRoot();
    TestIconResourcesUseAnExplicitRepositoryFallback();
    TestExplicitDevelopmentRootProvidesCompleteFallback();
    TestMissingFallbackAssetsRemainReportedWithoutCurrentDirectoryLookup();
    TestRegistrationReportPreservesFallbackAndMissingAssetState();
}
