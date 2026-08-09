#include <fontconfig/fontconfig.h>
#include <unistd.h>

#include <array>
#include <mutex>
#include <string>
#include <unordered_map>
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

struct OwnedFontConfig {
    FcConfig* config = nullptr;
    FcConfig* previous = nullptr;
};

std::mutex fontconfig_mutex;
std::unordered_map<const FontRegistry*, OwnedFontConfig> font_configs;

bool RegisterFont(FcConfig* config, const std::filesystem::path& path) {
    return config != nullptr &&
           FcConfigAppFontAddFile(config, reinterpret_cast<const FcChar8*>(path.c_str())) == FcTrue;
}

void DestroyFontConfig(const OwnedFontConfig& owned) {
    if (FcConfigGetCurrent() == owned.config) {
        FcConfigSetCurrent(owned.previous);
    }
    if (owned.config != nullptr) {
        FcConfigDestroy(owned.config);
    }
    if (owned.previous != nullptr) {
        FcConfigDestroy(owned.previous);
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
    std::lock_guard<std::mutex> lock(fontconfig_mutex);
    FontRegistrationReport report = InitialFontRegistrationReport(resources_);
    if (!report.failures.empty()) {
        return report;
    }
    if (is_registered_) {
        report.registered = registered_fonts_;
        return report;
    }

    OwnedFontConfig owned;
    owned.previous = FcConfigGetCurrent();
    if (owned.previous != nullptr) {
        FcConfigReference(owned.previous);
    }
    owned.config = FcInitLoadConfigAndFonts();
    if (owned.config == nullptr) {
        report.failures = resources_.font_files;
        if (owned.previous != nullptr) {
            FcConfigDestroy(owned.previous);
        }
        return report;
    }

    for (const std::filesystem::path& font : resources_.font_files) {
        if (!RegisterFont(owned.config, font)) {
            report.failures.push_back(font);
            FcConfigDestroy(owned.config);
            FcConfigDestroy(owned.previous);
            return report;
        }
    }
    FcConfigReference(owned.config);
    FcConfigSetCurrent(owned.config);
    font_configs.emplace(this, owned);
    registered_fonts_ = resources_.font_files;
    is_registered_ = true;
    report.registered = registered_fonts_;
    return report;
}

void FontRegistry::Unregister() {
    std::lock_guard<std::mutex> lock(fontconfig_mutex);
    const auto found = font_configs.find(this);
    if (found != font_configs.end()) {
        DestroyFontConfig(found->second);
        font_configs.erase(found);
    }
    registered_fonts_.clear();
    is_registered_ = false;
}

}  // namespace island
