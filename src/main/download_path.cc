#include "download_path.h"

#include <cstdlib>
#include <system_error>

namespace island {
namespace {

constexpr std::string_view kFallbackName = "download";

bool IsSeparator(char c) { return c == '/' || c == '\\'; }

}  // namespace

std::filesystem::path DefaultDownloadsDirectory() {
#if defined(_WIN32)
    const char* const home = std::getenv("USERPROFILE");
#else
    const char* const home = std::getenv("HOME");
#endif
    if (home == nullptr || *home == '\0') {
        return {};
    }
    return std::filesystem::path(home) / "Downloads";
}

std::string SanitizeDownloadFileName(std::string_view suggested_name) {
    // Keep only what follows the last separator, whichever style the server used.
    std::size_t start = 0;
    for (std::size_t i = 0; i < suggested_name.size(); ++i) {
        if (IsSeparator(suggested_name[i])) {
            start = i + 1;
        }
    }
    std::string name;
    for (const char c : suggested_name.substr(start)) {
        const auto byte = static_cast<unsigned char>(c);
        if (byte >= 0x20 && byte != 0x7F) {
            name.push_back(c);
        }
    }
    while (!name.empty() && (name.back() == '.' || name.back() == ' ')) {
        name.pop_back();
    }
    while (!name.empty() && name.front() == ' ') {
        name.erase(name.begin());
    }
    if (name.empty()) {
        return std::string(kFallbackName);
    }
    return name;
}

std::filesystem::path ResolveDownloadPath(const std::filesystem::path& downloads_dir,
                                          std::string_view suggested_name) {
    const std::filesystem::path name(SanitizeDownloadFileName(suggested_name));
    std::error_code error;
    std::filesystem::path candidate = downloads_dir / name;
    if (!std::filesystem::exists(candidate, error)) {
        return candidate;
    }
    const std::string stem = name.stem().string();
    const std::string extension = name.extension().string();
    for (int attempt = 1; attempt < 10000; ++attempt) {
        candidate = downloads_dir / (stem + " (" + std::to_string(attempt) + ")" + extension);
        if (!std::filesystem::exists(candidate, error)) {
            return candidate;
        }
    }
    return candidate;
}

}  // namespace island
