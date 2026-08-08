#include "font_registry.h"

#include <CoreText/CoreText.h>
#include <mach-o/dyld.h>

#include <array>
#include <string>
#include <utility>

namespace island {
namespace {

std::filesystem::path CurrentProcessBinary() {
    std::array<char, 1024> buffer = {};
    std::uint32_t size = static_cast<std::uint32_t>(buffer.size());
    if (_NSGetExecutablePath(buffer.data(), &size) == 0) {
        return buffer.data();
    }

    std::string path(size, '\0');
    if (_NSGetExecutablePath(path.data(), &size) != 0) {
        return {};
    }
    return path.c_str();
}

bool RegisterFont(const std::filesystem::path& path) {
    const std::string native_path = path.string();
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(native_path.data()), native_path.size(),
        false);
    if (url == nullptr) {
        return false;
    }

    CFErrorRef error = nullptr;
    const bool registered =
        CTFontManagerRegisterFontsForURL(url, kCTFontManagerScopeProcess, &error);
    if (error != nullptr) {
        CFRelease(error);
    }
    CFRelease(url);
    return registered;
}

void UnregisterFont(const std::filesystem::path& path) {
    const std::string native_path = path.string();
    CFURLRef url = CFURLCreateFromFileSystemRepresentation(
        kCFAllocatorDefault, reinterpret_cast<const UInt8*>(native_path.data()), native_path.size(),
        false);
    if (url == nullptr) {
        return;
    }

    CFErrorRef error = nullptr;
    CTFontManagerUnregisterFontsForURL(url, kCTFontManagerScopeProcess, &error);
    if (error != nullptr) {
        CFRelease(error);
    }
    CFRelease(url);
}

}  // namespace

FontRegistry FontRegistry::ForCurrentProcess(
    std::optional<std::filesystem::path> development_repository_root) {
    return FontRegistry(ResolveFontResources(ResourcePlatform::kMacOS, CurrentProcessBinary(),
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
