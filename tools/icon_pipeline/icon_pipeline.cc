#define NANOSVG_IMPLEMENTATION
#define NANOSVGRAST_IMPLEMENTATION
#define STB_IMAGE_WRITE_IMPLEMENTATION

#include <algorithm>
#include <array>
#include <cctype>
#include <cstddef>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <span>
#include <sstream>
#include <string>
#include <string_view>
#include <vector>

#include "sha256.h"

extern "C" {
#include "vendor/nanosvg.h"
#include "vendor/nanosvgrast.h"
#include "vendor/stb_image_write.h"
}

namespace {

namespace fs = std::filesystem;

constexpr std::string_view kLockHash = "adead0ce6ad40d033a2bf882b961ef8fe87608e51135eca4ffe518512396ed4b";
constexpr std::array<int, 4> kDips = {13, 15, 16, 17};
constexpr std::array<int, 2> kScales = {1, 2};

struct Icon {
    std::string_view name;
    std::string_view source;
    std::string_view digest;
};

struct Tone {
    std::string_view name;
    std::string_view hex;
};

constexpr std::array<Icon, 4> kIcons = {{{"chevron-left", "chevron-left.svg",
                                           "83b0681aa38bf55e9d52a1e4b4cced624abe1fe7678ecafda133a574f1161d93"},
                                          {"chevron-right", "chevron-right.svg",
                                           "2758143d7b2434e4aa7307dfd34405c87909ff4052f21b5f3f40d45224b4f19b"},
                                          {"reload", "rotate-cw.svg",
                                           "ddcfe6d87240475946935e77411cd4d15a06f3d28a9b921bafed224ebe953668"},
                                          {"globe-2", "globe-2.svg",
                                           "72ca6996d7032013268f46e9bcf360652136eb0f75465e3c98687fef784bbd41"}}};
constexpr std::array<Tone, 3> kTones = {
    {{"text", "18303a"}, {"secondary", "687a7d"}, {"accent", "168c99"}}};

std::vector<unsigned char> ReadFile(const fs::path& path) {
    std::ifstream input(path, std::ios::binary);
    if (!input) throw std::runtime_error("cannot read " + path.string());
    return {std::istreambuf_iterator<char>(input), std::istreambuf_iterator<char>()};
}

void Require(bool condition, std::string_view detail) {
    if (!condition) throw std::runtime_error(std::string(detail));
}

bool IsWithin(const fs::path& path, const fs::path& directory) {
    const fs::path canonical_path = fs::weakly_canonical(path);
    const fs::path canonical_directory = fs::weakly_canonical(directory);
    const auto mismatch = std::mismatch(canonical_directory.begin(), canonical_directory.end(),
                                        canonical_path.begin(), canonical_path.end());
    return mismatch.first == canonical_directory.end();
}

void ValidateLock(const fs::path& root) {
    const fs::path lock = root / "tools/icon_pipeline/icon-sources.lock.json";
    const std::vector<unsigned char> bytes = ReadFile(lock);
    Require(Sha256(bytes) == kLockHash, "icon source lock is not the approved immutable lock");
}

void ValidateSvg(const fs::path& source, std::string_view expected_digest) {
    Require(fs::symlink_status(source).type() != fs::file_type::symlink, "SVG source cannot be a symlink");
    const std::vector<unsigned char> bytes = ReadFile(source);
    Require(bytes.size() <= 8192, "SVG source exceeds 8192 byte limit");
    Require(Sha256(bytes) == expected_digest, "SVG source hash is not locked");
    const std::string svg(bytes.begin(), bytes.end());
    Require(svg.find("width=\"24\"") != std::string::npos &&
                svg.find("height=\"24\"") != std::string::npos &&
                svg.find("viewBox=\"0 0 24 24\"") != std::string::npos,
            "SVG must use the fixed 24 by 24 viewBox");
    Require(svg.find("href=") == std::string::npos && svg.find("url(") == std::string::npos &&
                svg.find("<!") == std::string::npos && svg.find("<?") == std::string::npos,
            "SVG contains an untrusted resource or declaration");
    size_t cursor = 0;
    while ((cursor = svg.find('<', cursor)) != std::string::npos) {
        const size_t name_start = cursor + (svg[cursor + 1] == '/' ? 2 : 1);
        size_t name_end = name_start;
        while (name_end < svg.size() && std::isalpha(static_cast<unsigned char>(svg[name_end]))) ++name_end;
        const std::string_view name(svg.data() + name_start, name_end - name_start);
        Require(name == "svg" || name == "path" || name == "circle", "SVG contains a forbidden element");
        cursor = name_end;
    }
}

std::string ReadSvg(const fs::path& path, std::string_view digest, std::string_view color) {
    ValidateSvg(path, digest);
    std::vector<unsigned char> bytes = ReadFile(path);
    std::string svg(bytes.begin(), bytes.end());
    const std::string needle = "currentColor";
    const size_t offset = svg.find(needle);
    Require(offset != std::string::npos && svg.find(needle, offset + needle.size()) == std::string::npos,
            "SVG must contain exactly one currentColor stroke");
    svg.replace(offset, needle.size(), "#" + std::string(color));
    return svg;
}

fs::path OutputPath(const fs::path& root, const Icon& icon, const Tone& tone, int dip, int scale) {
    return root / "resources/island/icons/png" /
           (std::string(icon.name) + "-" + std::string(tone.name) + "-" + std::to_string(dip) + "@" +
            std::to_string(scale) + "x.png");
}

void WritePng(const fs::path& destination, const std::string& svg, int pixels) {
    std::vector<char> parsed(svg.begin(), svg.end());
    parsed.push_back('\0');
    NSVGimage* image = nsvgParse(parsed.data(), "px", 96.0f);
    Require(image != nullptr, "NanoSVG rejected the locked SVG");
    std::unique_ptr<NSVGimage, decltype(&nsvgDelete)> image_guard(image, nsvgDelete);
    NSVGrasterizer* rasterizer = nsvgCreateRasterizer();
    Require(rasterizer != nullptr, "NanoSVG rasterizer allocation failed");
    std::unique_ptr<NSVGrasterizer, decltype(&nsvgDeleteRasterizer)> rasterizer_guard(rasterizer,
                                                                                         nsvgDeleteRasterizer);
    std::vector<unsigned char> pixels_rgba(static_cast<size_t>(pixels) * pixels * 4, 0);
    nsvgRasterize(rasterizer, image, 0.0f, 0.0f, static_cast<float>(pixels) / 24.0f, pixels_rgba.data(), pixels,
                  pixels, pixels * 4);
    Require(stbi_write_png(destination.string().c_str(), pixels, pixels, 4, pixels_rgba.data(), pixels * 4) != 0,
            "stb_image_write could not write canonical PNG");
}

std::string OutputDigest(const fs::path& output) {
    return Sha256(ReadFile(output));
}

struct Output {
    std::string path;
    std::string digest;
};

std::vector<Output> Generate(const fs::path& root) {
    const fs::path source_directory = root / "resources/island/icons/source";
    const fs::path output_directory = root / "resources/island/icons/png";
    fs::create_directories(output_directory);
    std::vector<Output> outputs;
    for (const Icon& icon : kIcons) {
        const fs::path source = source_directory / icon.source;
        Require(IsWithin(source, source_directory), "SVG source path is untrusted");
        for (const Tone& tone : kTones) {
            const std::string svg = ReadSvg(source, icon.digest, tone.hex);
            for (int dip : kDips) {
                for (int scale : kScales) {
                    const fs::path output = OutputPath(root, icon, tone, dip, scale);
                    Require(IsWithin(output, output_directory), "PNG output path is untrusted");
                    WritePng(output, svg, dip * scale);
                    outputs.push_back({fs::relative(output, root).generic_string(), OutputDigest(output)});
                }
            }
        }
    }
    return outputs;
}

void WriteManifest(const fs::path& root, const std::vector<Output>& outputs) {
    const fs::path manifest = root / "resources/island/icons/manifest.json";
    std::ofstream output(manifest, std::ios::binary | std::ios::trunc);
    Require(output.good(), "cannot write icon manifest");
    output << "{\n  \"schema\": 1,\n  \"generator\": \"NanoSVG 239e102 + stb_image_write 2c980bb\",\n";
    output << "  \"lock_sha256\": \"" << kLockHash << "\",\n";
    output << "  \"canonical_host_limit\": \"PNG byte hashes are canonical on macOS arm64 with Apple clang; other hosts must verify before adopting regenerated output.\",\n";
    output << "  \"source_hashes\": {\n";
    for (size_t index = 0; index < kIcons.size(); ++index) {
        output << "    \"" << kIcons[index].name << "\": \"" << kIcons[index].digest << "\""
               << (index + 1 == kIcons.size() ? "\n" : ",\n");
    }
    output << "  },\n  \"outputs\": [\n";
    for (size_t index = 0; index < outputs.size(); ++index) {
        output << "    {\"path\": \"" << outputs[index].path << "\", \"sha256\": \""
               << outputs[index].digest << "\"}" << (index + 1 == outputs.size() ? "\n" : ",\n");
    }
    output << "  ]\n}\n";
    Require(output.good(), "cannot finalize icon manifest");
}

std::vector<std::string> ManifestDigests(const fs::path& manifest) {
    const std::vector<unsigned char> bytes = ReadFile(manifest);
    const std::string document(bytes.begin(), bytes.end());
    Require(document.find("\"lock_sha256\": \"" + std::string(kLockHash) + "\"") != std::string::npos,
            "manifest was not generated from the approved icon lock");
    std::vector<std::string> digests;
    const std::string marker = "\"sha256\": \"";
    size_t cursor = 0;
    while ((cursor = document.find(marker, cursor)) != std::string::npos) {
        cursor += marker.size();
        Require(cursor + 64 <= document.size(), "manifest has a malformed output hash");
        digests.push_back(document.substr(cursor, 64));
        cursor += 64;
    }
    Require(digests.size() == kIcons.size() * kTones.size() * kDips.size() * kScales.size(),
            "manifest has an unexpected output count");
    return digests;
}

void Verify(const fs::path& root) {
    const fs::path source_directory = root / "resources/island/icons/source";
    for (const Icon& icon : kIcons) ValidateSvg(source_directory / icon.source, icon.digest);
    const std::vector<std::string> expected = ManifestDigests(root / "resources/island/icons/manifest.json");
    size_t index = 0;
    for (const Icon& icon : kIcons) {
        for (const Tone& tone : kTones) {
            for (int dip : kDips) {
                for (int scale : kScales) {
                    const fs::path output = OutputPath(root, icon, tone, dip, scale);
                    Require(fs::is_regular_file(output) && OutputDigest(output) == expected[index],
                            "PNG output hash differs from manifest");
                    ++index;
                }
            }
        }
    }
}

}

int main(int argc, char* argv[]) {
    try {
        Require(argc == 3, "usage: icon_pipeline generate|verify repository-root");
        const std::string mode = argv[1];
        const fs::path root = fs::canonical(argv[2]);
        Require(fs::is_directory(root), "repository root is not a directory");
        ValidateLock(root);
        if (mode == "generate") {
            WriteManifest(root, Generate(root));
        } else if (mode == "verify") {
            Verify(root);
        } else {
            throw std::runtime_error("mode must be generate or verify");
        }
        return 0;
    } catch (const std::exception& error) {
        std::cerr << "icon_pipeline: " << error.what() << '\n';
        return 1;
    }
}
