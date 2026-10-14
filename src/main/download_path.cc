#include "download_path.h"

#include <cstdlib>
#include <system_error>

namespace island {
namespace {

constexpr std::string_view kFallbackName = "download";

bool IsSeparator(char c) { return c == '/' || c == '\\'; }

// Names are UTF-8 throughout. Building the path from a char8_t string keeps
// them UTF-8 on every platform; a plain std::string would be read in the
// Windows ANSI code page and mangle any non-ASCII name.
std::filesystem::path PathFromUtf8(const std::string& utf8) {
    return std::filesystem::path(std::u8string(utf8.begin(), utf8.end()));
}

}  // namespace

std::filesystem::path DefaultDownloadsDirectory() {
#if defined(_WIN32)
    // The wide variant keeps a non-ASCII profile path intact.
    const wchar_t* const home = _wgetenv(L"USERPROFILE");
    if (home == nullptr || *home == L'\0') {
        return {};
    }
#else
    const char* const home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') {
        return {};
    }
#endif
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
    const std::string name = SanitizeDownloadFileName(suggested_name);
    std::error_code error;
    std::filesystem::path candidate = downloads_dir / PathFromUtf8(name);
    if (!std::filesystem::exists(candidate, error)) {
        return candidate;
    }
    // Split on the last dot, as std::filesystem::path::extension does, but on
    // the UTF-8 string so no narrow-encoding conversion is involved. A leading
    // dot (".bashrc") is part of the stem.
    const std::size_t dot = name.rfind('.');
    const bool has_extension = dot != std::string::npos && dot != 0;
    const std::string stem = has_extension ? name.substr(0, dot) : name;
    const std::string extension = has_extension ? name.substr(dot) : std::string();
    for (int attempt = 1; attempt < 10000; ++attempt) {
        candidate =
            downloads_dir / PathFromUtf8(stem + " (" + std::to_string(attempt) + ")" + extension);
        if (!std::filesystem::exists(candidate, error)) {
            return candidate;
        }
    }
    return candidate;
}

}  // namespace island
