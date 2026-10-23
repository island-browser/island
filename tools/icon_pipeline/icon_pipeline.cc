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

constexpr std::string_view kLockHash =
    "c8e27759c45d103094716767da6b8e2a9b8991b7207d6ca00bca7ef8f2e2525c";
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

constexpr std::array<Icon, 37> kIcons = {
    {{"chevron-left", "chevron-left.svg",
      "83b0681aa38bf55e9d52a1e4b4cced624abe1fe7678ecafda133a574f1161d93"},
     {"chevron-right", "chevron-right.svg",
      "2758143d7b2434e4aa7307dfd34405c87909ff4052f21b5f3f40d45224b4f19b"},
     {"reload", "rotate-cw.svg",
      "ddcfe6d87240475946935e77411cd4d15a06f3d28a9b921bafed224ebe953668"},
     {"globe-2", "globe-2.svg", "72ca6996d7032013268f46e9bcf360652136eb0f75465e3c98687fef784bbd41"},
     {"plus", "plus.svg", "7f6af73bf1ff6c4bca3f18351c8d1bdec6749c0c2530c4de5da85d520c21df17"},
     {"sparkles", "sparkles.svg",
      "f5499f33f09d7158151e9bd2ec0faf79ff8fb57292f84fdd7286d96d0f0424d8"},
     {"pin", "pin.svg", "55e8e5e04bcb671179fc1dd589abf09cf6a1e71301ff14eb739419cfab6011e7"},
     {"x", "x.svg", "4a9cdab38fbb96162e7dace28e33f4ca0e49d8963a6162abc3d4691b7d675117"},
     {"layout-grid", "layout-grid.svg",
      "b8903f61d09b1d75e55c71158277d26570962f69c2099446a251871e0a2d6678"},
     {"settings", "settings.svg",
      "0ae27fd0f81999229e3127ac96c5b32edfea448e291d509e76212b917551d66b"},
     {"home", "home.svg", "28216c479005731ef5e382e7d8627eda2b129b019eb1f3d9de9f5915576d448e"},
     {"briefcase", "briefcase.svg",
      "7c9e6bea85d15affdc5802bacbb7aae548c6031f246e4faef3747f36847365d0"},
     {"book-open", "book-open.svg",
      "9b9bed0ff057283d5c5c5a1c71f8d3e927db26a56639603a92fdf7b25961510e"},
     {"code", "code.svg", "83235d4a0352d74234f949c9fc23f89c65ff165f1b6d54a90e5f0ea5bfc2c3d7"},
     {"globe", "globe.svg", "72ca6996d7032013268f46e9bcf360652136eb0f75465e3c98687fef784bbd41"},
     {"plane", "plane.svg", "4c775a62a524be78fe20aed8544933d4bd3802c5eca17cd6923509f87671d6d6"},
     {"shopping-bag", "shopping-bag.svg",
      "24a161961b559234b7a31af904dab18b437507336ba2decff3fe6b7772495896"},
     {"music", "music.svg", "8721b8a3594219c1313c4083049e5a5c049f49c47b1cd93fdb61d52a0803e42b"},
     {"film", "film.svg", "85cfa80821b34f512bd401a1249957c9f2f612497602ecc31dff39517823ee52"},
     {"heart", "heart.svg", "70af5f30be9fcb500b852556b38dcdf8c04a4ba81fd262534fb0336bd39b3b52"},
     {"star", "star.svg", "c59da86b889569387db28e69ad00a89f96af22863ad9eea9aa12c4876a14338c"},
     {"flask", "flask.svg", "a2ce857c159bef4c128a6d9f0b6dcbd6504ecf1a2a670ee220efd14fd032b561"},
     {"graduation-cap", "graduation-cap.svg",
      "326b4a1c18429509a697cbc0c49fdfcf9316da34bcf581569c2fc73cc772b86f"},
     {"coffee", "coffee.svg", "970595bdafac0df42c00b847d53fd00c2283ecc3b83881274de2aa7dbce2bbc8"},
     {"camera", "camera.svg", "ebbb3178dd80dfd942770a2237fe7bfc3515c55c7daa9d3666933a656d2790e3"},
     {"gamepad", "gamepad.svg", "5448d59f3022acee0f8950856bb520d921aa724e70eab0132990b28739ae663f"},
     {"newspaper", "newspaper.svg",
      "f5147e2a92a0ca16edea01782b09afeea7f9ebcda029d3f38ae93a3058ed1b78"},
     {"palette", "palette.svg", "b0a958ba9889f89c40df6e1c168b8a7aa20f6309780e795c5bce00a4bc11bff6"},
     {"clock", "clock.svg", "e9d3e3acf4d1c280fcf8092293439dc0a4756a908ceb859de144b12451cd1cb9"},
     {"moon", "moon.svg", "ecc765994dc323f36bcb71b0dbab90bb4d429241a74c9f6e925d8eda8bf3b49b"},
     {"table-2", "table-2.svg", "23eaa37f489e6f2015ac017c72cf97ce2fbbe714c2e0c7a1577b32223ae06a8f"},
     {"dumbbell", "dumbbell.svg",
      "058a51e1fd42319d7eee4988a68bee4a23bc94fc69cb8aa1450835246126382d"},
     {"wallet", "wallet.svg", "76c9596f629a2abf9cfabb573452f3113ed7e9502c699aa811e630f156eb7556"},
     {"bug", "bug.svg", "c4fddf0d43b4c4f0b8fc52eb8810173c8d8bd25618034a70f162d85e8189d69d"},
     {"rocket", "rocket.svg", "2b7887d9a5623f2e5e4a5d32716f93829d61b458e03b6eae33a6df648b9eb855"},
     {"leaf", "leaf.svg", "a6cd15ecfaed579da29311eedc19302209e82515be83bfeca114183b0318a56f"},
     {"folder", "folder.svg", "f0d92b94b797a8ab7d4c4ae33c3236c47f64068351b4e14bdb5014ee42898a39"}}};
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
    Require(fs::symlink_status(source).type() != fs::file_type::symlink,
            "SVG source cannot be a symlink");
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
        while (name_end < svg.size() && std::isalpha(static_cast<unsigned char>(svg[name_end])))
            ++name_end;
        const std::string_view name(svg.data() + name_start, name_end - name_start);
        Require(name == "svg" || name == "path" || name == "circle" || name == "rect",
                "SVG contains a forbidden element");
        cursor = name_end;
    }
}

std::string ReadSvg(const fs::path& path, std::string_view digest, std::string_view color) {
    ValidateSvg(path, digest);
    std::vector<unsigned char> bytes = ReadFile(path);
    std::string svg(bytes.begin(), bytes.end());
    const std::string needle = "currentColor";
    const size_t offset = svg.find(needle);
    Require(offset != std::string::npos &&
                svg.find(needle, offset + needle.size()) == std::string::npos,
            "SVG must contain exactly one currentColor stroke");
    svg.replace(offset, needle.size(), "#" + std::string(color));
    return svg;
}

fs::path OutputPath(const fs::path& root, const Icon& icon, const Tone& tone, int dip, int scale) {
    return root / "resources/island/icons/png" /
           (std::string(icon.name) + "-" + std::string(tone.name) + "-" + std::to_string(dip) +
            "@" + std::to_string(scale) + "x.png");
}

void WritePng(const fs::path& destination, const std::string& svg, int pixels) {
    std::vector<char> parsed(svg.begin(), svg.end());
    parsed.push_back('\0');
    NSVGimage* image = nsvgParse(parsed.data(), "px", 96.0f);
    Require(image != nullptr, "NanoSVG rejected the locked SVG");
    std::unique_ptr<NSVGimage, decltype(&nsvgDelete)> image_guard(image, nsvgDelete);
    NSVGrasterizer* rasterizer = nsvgCreateRasterizer();
    Require(rasterizer != nullptr, "NanoSVG rasterizer allocation failed");
    std::unique_ptr<NSVGrasterizer, decltype(&nsvgDeleteRasterizer)> rasterizer_guard(
        rasterizer, nsvgDeleteRasterizer);
    std::vector<unsigned char> pixels_rgba(static_cast<size_t>(pixels) * pixels * 4, 0);
    nsvgRasterize(rasterizer, image, 0.0f, 0.0f, static_cast<float>(pixels) / 24.0f,
                  pixels_rgba.data(), pixels, pixels, pixels * 4);
    Require(stbi_write_png(destination.string().c_str(), pixels, pixels, 4, pixels_rgba.data(),
                           pixels * 4) != 0,
            "stb_image_write could not write canonical PNG");
}

std::string OutputDigest(const fs::path& output) { return Sha256(ReadFile(output)); }

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
                    outputs.push_back(
                        {fs::relative(output, root).generic_string(), OutputDigest(output)});
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
    output
        << "{\n  \"schema\": 1,\n  \"generator\": \"NanoSVG 239e102 + stb_image_write 2c980bb\",\n";
    output << "  \"lock_sha256\": \"" << kLockHash << "\",\n";
    output << "  \"canonical_host_limit\": \"PNG byte hashes are canonical on macOS arm64 with "
              "Apple clang; other hosts must verify before adopting regenerated output.\",\n";
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
    Require(
        document.find("\"lock_sha256\": \"" + std::string(kLockHash) + "\"") != std::string::npos,
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
    const std::vector<std::string> expected =
        ManifestDigests(root / "resources/island/icons/manifest.json");
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

}  // namespace

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
