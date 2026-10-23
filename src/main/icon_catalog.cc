#include "icon_catalog.h"

#include <algorithm>
#include <array>
#include <fstream>
#include <iterator>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

#include "space_icon.h"

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
        case ChromeIcon::kPlus:
            return "plus";
        case ChromeIcon::kSparkles:
            return "sparkles";
        case ChromeIcon::kPin:
            return "pin";
        case ChromeIcon::kClose:
            return "x";
        case ChromeIcon::kGrid:
            return "layout-grid";
        case ChromeIcon::kSettings:
            return "settings";
        case ChromeIcon::kStar:
            return "star";
        case ChromeIcon::kNewspaper:
            return "newspaper";
        case ChromeIcon::kBookOpen:
            return "book-open";
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

std::filesystem::path NamedVariantPath(const std::filesystem::path& root, std::string_view icon_id,
                                       ChromeIconTone tone, ChromeIconSize size,
                                       std::string_view scale) {
    return root / "png" /
           (std::string(icon_id) + "-" + std::string(ToneFilename(tone)) + "-" +
            std::string(SizeFilename(size)) + "@" + std::string(scale) + "x.png");
}

std::optional<CefRefPtr<CefImage>> ReadImagePair(const std::filesystem::path& one_x_path,
                                                 const std::filesystem::path& two_x_path) {
    const std::optional<std::vector<char>> one_x = ReadPng(one_x_path);
    const std::optional<std::vector<char>> two_x = ReadPng(two_x_path);
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

bool AddTintedRepresentation(CefRefPtr<CefImage> source, CefRefPtr<CefImage> destination,
                             float scale, ArgbColor color) {
    int width = 0;
    int height = 0;
    CefRefPtr<CefBinaryValue> bitmap = source->GetAsBitmap(
        scale, CEF_COLOR_TYPE_RGBA_8888, CEF_ALPHA_TYPE_POSTMULTIPLIED, width, height);
    if (bitmap == nullptr || width <= 0 || height <= 0) {
        return false;
    }
    std::vector<std::uint8_t> pixels(bitmap->GetSize());
    if (bitmap->GetData(pixels.data(), pixels.size(), 0) != pixels.size()) {
        return false;
    }
    const std::uint8_t red = static_cast<std::uint8_t>((color.argb >> 16U) & 0xFFU);
    const std::uint8_t green = static_cast<std::uint8_t>((color.argb >> 8U) & 0xFFU);
    const std::uint8_t blue = static_cast<std::uint8_t>(color.argb & 0xFFU);
    for (std::size_t offset = 0; offset + 3 < pixels.size(); offset += 4) {
        pixels[offset] = red;
        pixels[offset + 1] = green;
        pixels[offset + 2] = blue;
    }
    return destination->AddBitmap(scale, width, height, CEF_COLOR_TYPE_RGBA_8888,
                                  CEF_ALPHA_TYPE_POSTMULTIPLIED, pixels.data(), pixels.size());
}

std::optional<CefRefPtr<CefImage>> ReadTintedSpaceIcon(const std::filesystem::path& resource_root,
                                                       std::string_view icon_id, ArgbColor color,
                                                       ChromeIconSize size) {
    const std::optional<CefRefPtr<CefImage>> source =
        ReadImagePair(NamedVariantPath(resource_root, icon_id, ChromeIconTone::kText, size, "1"),
                      NamedVariantPath(resource_root, icon_id, ChromeIconTone::kText, size, "2"));
    if (!source.has_value()) {
        return std::nullopt;
    }
    CefRefPtr<CefImage> tinted = CefImage::CreateImage();
    if (!AddTintedRepresentation(*source, tinted, kOneXScale, color) ||
        !AddTintedRepresentation(*source, tinted, kTwoXScale, color)) {
        return std::nullopt;
    }
    return tinted;
}

}  // namespace

IconCatalog::IconCatalog(std::filesystem::path resource_root)
    : IconCatalog(std::move(resource_root), nullptr) {}

IconCatalog::IconCatalog(std::filesystem::path resource_root, SpaceIconReader space_icon_reader)
    : resource_root_(std::move(resource_root)), space_icon_reader_(std::move(space_icon_reader)) {
    if (!space_icon_reader_) {
        space_icon_reader_ = [root = resource_root_](std::string_view icon_id, ArgbColor color,
                                                     ChromeIconSize size) {
            return ReadTintedSpaceIcon(root, icon_id, color, size);
        };
    }
}

std::optional<CefRefPtr<CefImage>> IconCatalog::Load(ChromeIcon icon, ChromeIconTone tone,
                                                     ChromeIconSize size) const {
    return ReadImagePair(VariantPath(resource_root_, icon, tone, size, "1"),
                         VariantPath(resource_root_, icon, tone, size, "2"));
}

std::optional<CefRefPtr<CefImage>> IconCatalog::LoadTinted(ChromeIcon icon, ArgbColor color,
                                                           ChromeIconSize size) const {
    const std::optional<CefRefPtr<CefImage>> source =
        ReadImagePair(VariantPath(resource_root_, icon, ChromeIconTone::kText, size, "1"),
                      VariantPath(resource_root_, icon, ChromeIconTone::kText, size, "2"));
    if (!source.has_value()) {
        return std::nullopt;
    }
    CefRefPtr<CefImage> tinted = CefImage::CreateImage();
    if (!AddTintedRepresentation(*source, tinted, kOneXScale, color) ||
        !AddTintedRepresentation(*source, tinted, kTwoXScale, color)) {
        return std::nullopt;
    }
    return tinted;
}

std::optional<CefRefPtr<CefImage>> IconCatalog::LoadSpaceIcon(std::string_view icon_id,
                                                              ArgbColor color,
                                                              ChromeIconSize size) const {
    if (!IsKnownSpaceIconId(icon_id)) {
        return std::nullopt;
    }
    return space_icon_cache_.GetOrLoad(icon_id, static_cast<int>(size), color.argb, [&] {
        ++space_icon_reads_;
        return space_icon_reader_(icon_id, color, size);
    });
}

}  // namespace island
