#include <fontconfig/fontconfig.h>
#include <unistd.h>

#include <array>
#include <string>
#include <utility>

#include "font_registry.h"

namespace island {
namespace {

std::filesystem::path CurrentProcessBinary() {
    std::array<char, 4096> buffer = {};
    const ssize_t length = readlink("/proc/self/exe", buffer.data(), buffer.size() - 1);
    if (length <= 0 || static_cast<std::size_t>(length) == buffer.size() - 1) {
        return {};
    }
    return std::string(buffer.data(), static_cast<std::size_t>(length));
}  // namespace

bool RegisterFont(const std::filesystem::path& path) {
    FcConfig* config = FcConfigGetCurrent();
    return config != nullptr &&
           FcConfigAppFontAddFile(config, reinterpret_cast<const FcChar8*>(path.c_str())) == FcTrue;
}  // namespace

void UnregisterFonts() {
    FcConfig* config = FcConfigGetCurrent();
    if (config != nullptr) {
        FcConfigAppFontClear(config);
    }
}

}  // namespace

FontRegistry FontRegistry::ForCurrentProcess(
    std::optional<std::filesystem::path> development_repository_root) {
    return FontRegistry(ResolveFontResources(ResourcePlatform::kLinux, CurrentProcessBinary(),
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
    if (!registered_fonts_.empty()) {
        UnregisterFonts();
    }
    registered_fonts_.clear();
    is_registered_ = false;
}

}  // namespace island
