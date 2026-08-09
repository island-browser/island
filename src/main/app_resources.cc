#include "app_resources.h"

#if defined(__APPLE__)
#include <mach-o/dyld.h>
#elif defined(_WIN32)
#include <windows.h>
#elif defined(__linux__)
#include <unistd.h>
#endif

#include <array>
#include <cstdint>
#include <string>
#include <system_error>
#include <vector>

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

bool IsRegularFile(const std::filesystem::path& path) {
    std::error_code error;
    return std::filesystem::is_regular_file(path, error);
}

ResourcePlatform CurrentResourcePlatform() {
#if defined(__APPLE__)
    return ResourcePlatform::kMacOS;
#elif defined(_WIN32)
    return ResourcePlatform::kWindows;
#else
    return ResourcePlatform::kLinux;
#endif
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

std::filesystem::path PackagedIconDirectory(ResourcePlatform platform,
                                            const std::filesystem::path& runtime_binary) {
    switch (platform) {
        case ResourcePlatform::kMacOS:
            return runtime_binary.parent_path().parent_path() / "Resources" / "island" / "icons";
        case ResourcePlatform::kWindows:
        case ResourcePlatform::kLinux:
            return runtime_binary.parent_path() / "resources" / "island" / "icons";
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

IconResources ResolveIconResources(
    ResourcePlatform platform, const std::filesystem::path& runtime_binary,
    const std::optional<std::filesystem::path>& development_repository_root) {
    IconResources resources;
    resources.root = PackagedIconDirectory(platform, runtime_binary);
    resources.manifest = resources.root / "manifest.json";
    resources.manifest_present = IsRegularFile(resources.manifest);
    if (resources.manifest_present || !development_repository_root.has_value()) {
        return resources;
    }

    resources.using_fallback = true;
    resources.root = *development_repository_root / "resources" / "island" / "icons";
    resources.manifest = resources.root / "manifest.json";
    resources.manifest_present = IsRegularFile(resources.manifest);
    return resources;
}

std::filesystem::path CurrentRuntimeBinaryPath() {
#if defined(__APPLE__)
    std::array<char, 1024> buffer = {};
    std::uint32_t size = static_cast<std::uint32_t>(buffer.size());
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        return buffer.data();
    }
    std::vector<char> expanded_buffer(size);
    if (_NSGetExecutablePath(expanded_buffer.data(), &size) != 0) {
        return {};
    }
    return expanded_buffer.data();
#elif defined(_WIN32)
    HMODULE module = nullptr;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&CurrentRuntimeBinaryPath), &module) == 0) {
        return {};
    }
    std::vector<wchar_t> buffer(1024);
    while (buffer.size() <= 32768U) {
        const DWORD length =
            GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
        if (length == 0) {
            return {};
        }
        if (length < buffer.size() - 1U) {
            return std::filesystem::path(std::wstring(buffer.data(), length));
        }
        buffer.resize(buffer.size() * 2U);
    }
    return {};
#elif defined(__linux__)
    std::vector<char> buffer(1024);
    while (buffer.size() <= 32768U) {
        const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1U);
        if (length < 0) {
            return {};
        }
        if (static_cast<std::size_t>(length) < buffer.size() - 1U) {
            return std::string(buffer.data(), static_cast<std::size_t>(length));
        }
        buffer.resize(buffer.size() * 2U);
    }
    return {};
#else
    return {};
#endif
}

IconResources ResolveCurrentProcessIconResources(
    const std::optional<std::filesystem::path>& development_repository_root) {
    return ResolveIconResources(CurrentResourcePlatform(), CurrentRuntimeBinaryPath(),
                                development_repository_root);
}

}  // namespace island
