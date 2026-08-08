#include "app_resources.h"

#include <cassert>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string_view>

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
}  // namespace

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
    TestPackagedFontDirectoryUsesPlatformLayouts();
    TestExplicitDevelopmentRootProvidesCompleteFallback();
    TestMissingFallbackAssetsRemainReportedWithoutCurrentDirectoryLookup();
    TestRegistrationReportPreservesFallbackAndMissingAssetState();
}
