#include "app_resources.h"

#include <system_error>

namespace island {
namespace {

std::vector<std::filesystem::path> ExpectedAssets(const std::filesystem::path& directory) {
    std::vector<std::filesystem::path> assets;
    assets.reserve(kRequiredFontResourceFiles.size());
    for (const std::string_view filename : kRequiredFontResourceFiles) {
        assets.emplace_back(directory / filename);
    }
    return assets;
}

std::vector<std::filesystem::path> MissingAssets(const std::vector<std::filesystem::path>& assets) {
    std::vector<std::filesystem::path> missing;
    for (const std::filesystem::path& asset : assets) {
        std::error_code error;
        if (!std::filesystem::is_regular_file(asset, error)) {
            missing.push_back(asset);
        }
    }
    return missing;
}

std::vector<std::filesystem::path> FontFiles(const std::vector<std::filesystem::path>& assets) {
    return {assets.begin(), assets.begin() + 8};
}

}  // namespace

std::filesystem::path PackagedFontDirectory(ResourcePlatform platform,
                                            const std::filesystem::path& runtime_binary) {
    switch (platform) {
        case ResourcePlatform::kMacOS:
            return runtime_binary.parent_path().parent_path() / "Resources" / "island" / "fonts";
        case ResourcePlatform::kWindows:
        case ResourcePlatform::kLinux:
            return runtime_binary.parent_path() / "resources" / "island" / "fonts";
    }
    return {};
}

FontResources ResolveFontResources(
    ResourcePlatform platform, const std::filesystem::path& runtime_binary,
    const std::optional<std::filesystem::path>& development_repository_root) {
    FontResources resources;
    const std::vector<std::filesystem::path> packaged_assets =
        ExpectedAssets(PackagedFontDirectory(platform, runtime_binary));
    resources.missing_packaged_assets = MissingAssets(packaged_assets);
    if (resources.missing_packaged_assets.empty()) {
        resources.expected_assets = packaged_assets;
        resources.font_files = FontFiles(resources.expected_assets);
        return resources;
    }

    if (development_repository_root.has_value()) {
        resources.using_fallback = true;
        resources.expected_assets =
            ExpectedAssets(*development_repository_root / "assets" / "fonts");
        resources.missing_assets = MissingAssets(resources.expected_assets);
        if (resources.missing_assets.empty()) {
            resources.font_files = FontFiles(resources.expected_assets);
        }
        return resources;
    }

    resources.expected_assets = packaged_assets;
    resources.missing_assets = resources.missing_packaged_assets;
    return resources;
}

}  // namespace island
