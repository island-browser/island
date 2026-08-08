#include <windows.h>

#include <array>
#include <string>
#include <utility>

#include "font_registry.h"

namespace island {
namespace {

std::filesystem::path CurrentProcessBinary() {
    HMODULE module = nullptr;
    if (GetModuleHandleExW(
            GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&CurrentProcessBinary), &module) == 0) {
        return {};
    }

    std::array<wchar_t, 32768> buffer = {};
    const DWORD length =
        GetModuleFileNameW(module, buffer.data(), static_cast<DWORD>(buffer.size()));
    if (length == 0 || length == buffer.size()) {
        return {};
    }
    return std::filesystem::path(std::wstring(buffer.data(), length));
}

bool RegisterFont(const std::filesystem::path& path) {
    return AddFontResourceExW(path.c_str(), FR_PRIVATE | FR_NOT_ENUM, nullptr) != 0;
}

void UnregisterFont(const std::filesystem::path& path) {
    RemoveFontResourceExW(path.c_str(), FR_PRIVATE | FR_NOT_ENUM, nullptr);
}

}  // namespace

FontRegistry FontRegistry::ForCurrentProcess(
    std::optional<std::filesystem::path> development_repository_root) {
    return FontRegistry(ResolveFontResources(ResourcePlatform::kWindows, CurrentProcessBinary(),
                                             development_repository_root));
}

FontRegistry::FontRegistry(FontResources resources) : resources_(std::move(resources)) {}

FontRegistry::~FontRegistry() { Unregister(); }

FontRegistrationReport FontRegistry::Register() {
    FontRegistrationReport report = InitialFontRegistrationReport(resources_);
    if (!report.failures.empty()) {
        return report;
    }
    if (is_registered_) {
        report.registered = registered_fonts_;
        return report;
    }

    for (const std::filesystem::path& font : resources_.font_files) {
        if (!RegisterFont(font)) {
            report.failures.push_back(font);
            Unregister();
            return report;
        }
        registered_fonts_.push_back(font);
    }
    is_registered_ = true;
    report.registered = registered_fonts_;
    return report;
}

void FontRegistry::Unregister() {
    for (const std::filesystem::path& font : registered_fonts_) {
        UnregisterFont(font);
    }
    registered_fonts_.clear();
    is_registered_ = false;
}

}  // namespace island
