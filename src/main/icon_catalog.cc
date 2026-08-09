#include "icon_catalog.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <string_view>
#include <utility>
#include <vector>

namespace island {
namespace {

constexpr float kOneXScale = 1.0F;
constexpr float kTwoXScale = 2.0F;

std::string_view IconFilename(ChromeIcon icon) {
    switch (icon) {
        case ChromeIcon::kBack:
            return "chevron-left";
        case ChromeIcon::kForward:
            return "chevron-right";
        case ChromeIcon::kReload:
            return "reload";
        case ChromeIcon::kLocation:
            return "globe-2";
    }
    return {};
}

std::string_view ToneFilename(ChromeIconTone tone) {
    switch (tone) {
        case ChromeIconTone::kText:
            return "text";
        case ChromeIconTone::kSecondary:
            return "secondary";
        case ChromeIconTone::kAccent:
            return "accent";
    }
    return {};
}

std::string_view SizeFilename(ChromeIconSize size) {
    switch (size) {
        case ChromeIconSize::k13:
            return "13";
        case ChromeIconSize::k15:
            return "15";
        case ChromeIconSize::k16:
            return "16";
        case ChromeIconSize::k17:
            return "17";
    }
    return {};
}

std::optional<std::vector<char>> ReadPng(const std::filesystem::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) {
        return std::nullopt;
    }

    std::vector<char> bytes((std::istreambuf_iterator<char>(input)),
                            std::istreambuf_iterator<char>());
    constexpr std::array<char, 8> kPngSignature = {'\x89', 'P', 'N', 'G', '\r', '\n', '\x1a', '\n'};
    if (bytes.size() < kPngSignature.size() ||
        !std::equal(kPngSignature.begin(), kPngSignature.end(), bytes.begin())) {
        return std::nullopt;
    }
    return bytes;
}

std::filesystem::path VariantPath(const std::filesystem::path& root, ChromeIcon icon,
                                  ChromeIconTone tone, ChromeIconSize size,
                                  std::string_view scale) {
    return root / "png" /
           (std::string(IconFilename(icon)) + "-" + std::string(ToneFilename(tone)) + "-" +
            std::string(SizeFilename(size)) + "@" + std::string(scale) + "x.png");
}

}  // namespace

IconCatalog::IconCatalog(std::filesystem::path resource_root)
    : resource_root_(std::move(resource_root)) {}

std::optional<CefRefPtr<CefImage>> IconCatalog::Load(ChromeIcon icon, ChromeIconTone tone,
                                                     ChromeIconSize size) const {
    const std::optional<std::vector<char>> one_x =
        ReadPng(VariantPath(resource_root_, icon, tone, size, "1"));
    const std::optional<std::vector<char>> two_x =
        ReadPng(VariantPath(resource_root_, icon, tone, size, "2"));
    if (!one_x.has_value() || !two_x.has_value()) {
        return std::nullopt;
    }

    CefRefPtr<CefImage> image = CefImage::CreateImage();
    if (!image->AddPNG(kOneXScale, one_x->data(), one_x->size()) ||
        !image->AddPNG(kTwoXScale, two_x->data(), two_x->size())) {
        return std::nullopt;
    }
    return image;
}

}  // namespace island
