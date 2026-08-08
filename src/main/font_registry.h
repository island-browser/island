#ifndef ISLAND_MAIN_FONT_REGISTRY_H_
#define ISLAND_MAIN_FONT_REGISTRY_H_

#include <filesystem>
#include <optional>
#include <vector>

#include "app_resources.h"

namespace island {

struct FontRegistrationReport {
    std::vector<std::filesystem::path> expected;
    std::vector<std::filesystem::path> registered;
    std::vector<std::filesystem::path> failures;
    bool using_fallback = false;
};

inline FontRegistrationReport InitialFontRegistrationReport(const FontResources& resources) {
    return {resources.expected_assets, {}, resources.missing_assets, resources.using_fallback};
}  // namespace island

class FontRegistry final {
  public:
    static FontRegistry ForCurrentProcess(
        std::optional<std::filesystem::path> development_repository_root = std::nullopt);

    explicit FontRegistry(FontResources resources);
    ~FontRegistry();

    FontRegistry(const FontRegistry&) = delete;
    FontRegistry& operator=(const FontRegistry&) = delete;

    FontRegistrationReport Register();
    void Unregister();

  private:
    FontResources resources_;
    std::vector<std::filesystem::path> registered_fonts_;
    bool is_registered_ = false;
};

}  // namespace island

#endif
