#ifndef ISLAND_MAIN_APP_RESOURCES_H_
#define ISLAND_MAIN_APP_RESOURCES_H_

#include <array>
#include <filesystem>
#include <optional>
#include <string_view>
#include <vector>

namespace island {

enum class ResourcePlatform {
    kLinux,
    kMacOS,
    kWindows,
};

inline constexpr std::array<std::string_view, 10> kRequiredFontResourceFiles = {
    "Geist-Regular.ttf",
    "Geist-Medium.ttf",
    "Geist-SemiBold.ttf",
    "Geist-Bold.ttf",
    "GeistMono-Regular.ttf",
    "GeistMono-Medium.ttf",
    "GeistMono-SemiBold.ttf",
    "GeistMono-Bold.ttf",
    "OFL.txt",
    "LICENSE.txt",
};

struct FontResources {
    std::vector<std::filesystem::path> expected_assets;
    std::vector<std::filesystem::path> font_files;
    std::vector<std::filesystem::path> missing_assets;
    std::vector<std::filesystem::path> missing_packaged_assets;
    bool using_fallback = false;
};

struct IconResources {
    std::filesystem::path root;
    std::filesystem::path manifest;
    bool manifest_present = false;
    bool using_fallback = false;
};

std::filesystem::path PackagedFontDirectory(ResourcePlatform platform,
                                            const std::filesystem::path& runtime_binary);
FontResources ResolveFontResources(
    ResourcePlatform platform, const std::filesystem::path& runtime_binary,
    const std::optional<std::filesystem::path>& development_repository_root);
std::filesystem::path PackagedIconDirectory(ResourcePlatform platform,
                                            const std::filesystem::path& runtime_binary);
IconResources ResolveIconResources(
    ResourcePlatform platform, const std::filesystem::path& runtime_binary,
    const std::optional<std::filesystem::path>& development_repository_root);

}  // namespace island

#endif
