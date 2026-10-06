#include "updater.h"

#include <algorithm>
#include <array>
#include <cctype>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <sstream>
#include <system_error>
#include <utility>

#if defined(_WIN32)
#ifndef NOMINMAX
#define NOMINMAX
#endif
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <unistd.h>

extern char** environ;
#endif

namespace island::update {
namespace {

bool IsAsciiDigit(char c) { return c >= '0' && c <= '9'; }

bool IsIdentifierChar(char c) {
    return IsAsciiDigit(c) || (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || c == '-';
}

bool IsNumericIdentifier(std::string_view id) {
    return !id.empty() && std::all_of(id.begin(), id.end(), IsAsciiDigit);
}

std::vector<std::string_view> SplitDots(std::string_view text) {
    std::vector<std::string_view> parts;
    std::size_t start = 0;
    for (;;) {
        const std::size_t dot = text.find('.', start);
        parts.push_back(
            text.substr(start, dot == std::string_view::npos ? text.npos : dot - start));
        if (dot == std::string_view::npos) {
            return parts;
        }
        start = dot + 1;
    }
}

// A numeric version component: digits only, no leading zero, small enough to
// fit comfortably in 64 bits.
std::optional<std::uint64_t> ParseNumericPart(std::string_view part) {
    if (!IsNumericIdentifier(part) || part.size() > 18 || (part.size() > 1 && part[0] == '0')) {
        return std::nullopt;
    }
    std::uint64_t value = 0;
    for (const char c : part) {
        value = value * 10U + static_cast<std::uint64_t>(c - '0');
    }
    return value;
}

int CompareIdentifiers(std::string_view a, std::string_view b) {
    const bool a_numeric = IsNumericIdentifier(a);
    const bool b_numeric = IsNumericIdentifier(b);
    if (a_numeric && b_numeric) {
        // No leading zeros, so a longer numeral is larger.
        if (a.size() != b.size()) {
            return a.size() < b.size() ? -1 : 1;
        }
        return a.compare(b) < 0 ? -1 : (a == b ? 0 : 1);
    }
    if (a_numeric != b_numeric) {
        return a_numeric ? -1 : 1;  // numeric identifiers have lower precedence
    }
    const int compared = a.compare(b);
    return compared < 0 ? -1 : (compared == 0 ? 0 : 1);
}

std::string AsciiLower(std::string_view text) {
    std::string out(text);
    for (char& c : out) {
        if (c >= 'A' && c <= 'Z') {
            c = static_cast<char>(c - 'A' + 'a');
        }
    }
    return out;
}

bool EndsWith(std::string_view text, std::string_view suffix) {
    return text.size() >= suffix.size() && text.substr(text.size() - suffix.size()) == suffix;
}

std::string_view TrimAscii(std::string_view text) {
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.front())) != 0) {
        text.remove_prefix(1);
    }
    while (!text.empty() && std::isspace(static_cast<unsigned char>(text.back())) != 0) {
        text.remove_suffix(1);
    }
    return text;
}

// The "https://host[:port]" prefix of an absolute URL, or "".
std::string_view UrlOrigin(std::string_view url) {
    const std::size_t scheme_end = url.find("://");
    if (scheme_end == std::string_view::npos) {
        return {};
    }
    const std::size_t authority_end = url.find_first_of("/?#", scheme_end + 3);
    return url.substr(0, authority_end);
}

bool ProbeWritable(const std::filesystem::path& directory) {
    const std::filesystem::path probe =
        directory / (".island-update-probe-" + std::to_string(CurrentProcessId()));
#if defined(_WIN32)
    std::FILE* file = _wfopen(probe.c_str(), L"wb");
#else
    std::FILE* file = std::fopen(probe.c_str(), "wb");
#endif
    if (file == nullptr) {
        return false;
    }
    std::fclose(file);
    std::error_code error;
    std::filesystem::remove(probe, error);
    return true;
}

std::optional<std::filesystem::path> HomeDirectory() {
#if defined(_WIN32)
    const char* const home = std::getenv("USERPROFILE");
#else
    const char* const home = std::getenv("HOME");
#endif
    if (home == nullptr || *home == '\0') {
        return std::nullopt;
    }
    return std::filesystem::path(home);
}

bool SamePath(const std::filesystem::path& a, const std::filesystem::path& b) {
    std::error_code error;
    const bool same = std::filesystem::equivalent(a, b, error);
    return !error && same;
}

std::optional<std::string> ReadSmallFile(const std::filesystem::path& path, std::size_t max_bytes) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return std::nullopt;
    }
    std::string content(max_bytes + 1, '\0');
    in.read(content.data(), static_cast<std::streamsize>(content.size()));
    const std::streamsize read = in.gcount();
    if (in.bad() || read < 0 || static_cast<std::size_t>(read) > max_bytes) {
        return std::nullopt;
    }
    content.resize(static_cast<std::size_t>(read));
    return content;
}

constexpr std::string_view kStagingSuffix = ".island-update";

std::string PosixInstallScript(const InstallPlan& plan) {
    const bool mac = plan.platform == Platform::kMac;
    const std::string exe = plan.executable_name;
    std::ostringstream s;
    s << "#!/bin/sh\n"
      << "# Island updater: installs Island " << plan.version << " once the browser (pid "
      << plan.pid << ") exits.\n"
      << "# Generated by Island; safe to delete.\n"
      << "PID=" << plan.pid << "\n"
      << "VERSION=" << ShellQuote(plan.version) << "\n"
      << "ARCHIVE=" << ShellQuote(PathUtf8(plan.archive)) << "\n"
      << "EXTRACT=" << ShellQuote(PathUtf8(plan.extract_dir())) << "\n"
      << "TARGET=" << ShellQuote(PathUtf8(plan.install_root)) << "\n"
      << "BACKUP=" << ShellQuote(PathUtf8(plan.backup_path())) << "\n"
      << "BACKUP_PARENT=" << ShellQuote(PathUtf8(plan.backup_path().parent_path())) << "\n"
      << "RESULT=" << ShellQuote(PathUtf8(plan.result_file())) << "\n"
      << "LOG=" << ShellQuote(PathUtf8(plan.log_file())) << "\n"
      << "EXE=" << ShellQuote(exe) << "\n";
    if (mac) {
        // The archive holds island_browser.app beside the package metadata.
        s << "NEW_ROOT=\"$EXTRACT/island_browser.app\"\n"
          << "NEW_EXE=\"$NEW_ROOT/Contents/MacOS/$EXE\"\n";
    } else {
        // The archive holds the install directory's contents at its root.
        s << "NEW_ROOT=\"$EXTRACT\"\n"
          << "NEW_EXE=\"$NEW_ROOT/$EXE\"\n";
    }
    s << "\n"
      << "cd / || exit 1\n"
      << "exec >>\"$LOG\" 2>&1\n"
      << "\n"
      << "relaunch() {\n";
    if (mac) {
        s << "    open \"$TARGET\"\n";
    } else {
        s << "    nohup \"$TARGET/$EXE\" >/dev/null 2>&1 &\n";
    }
    s << "}\n"
      << "record() {\n"
      << "    printf '%s\\n' \"$1\" >\"$RESULT\"\n"
      << "}\n"
      << "fail() {\n"
      << "    echo \"Island update failed: $1\"\n"
      << "    record \"failed $1\"\n"
      << "    relaunch\n"
      << "    exit 1\n"
      << "}\n"
      << "\n"
      << "echo \"Installing Island $VERSION\"\n"
      << "tries=0\n"
      << "while kill -0 \"$PID\" 2>/dev/null; do\n"
      << "    tries=$((tries + 1))\n"
      << "    if [ \"$tries\" -gt 120 ]; then\n"
      << "        record \"failed Island did not quit\"\n"
      << "        exit 1\n"
      << "    fi\n"
      << "    sleep 1\n"
      << "done\n"
      << "\n"
      << "rm -rf \"$EXTRACT\"\n"
      << "mkdir -p \"$EXTRACT\" || fail \"Could not create the staging folder\"\n";
    if (mac) {
        s << "ditto -x -k \"$ARCHIVE\" \"$EXTRACT\" || fail \"Could not extract the update\"\n";
    } else {
        s << "tar -xzf \"$ARCHIVE\" -C \"$EXTRACT\" || fail \"Could not extract the update\"\n";
    }
    s << "[ -x \"$NEW_EXE\" ] || fail \"The update is missing $EXE\"\n"
      << "rm -rf \"$BACKUP\"\n"
      << "mkdir -p \"$BACKUP_PARENT\" || fail \"Could not create the backup folder\"\n"
      << "mv \"$TARGET\" \"$BACKUP\" || fail \"Could not move the current version aside\"\n"
      << "if ! mv \"$NEW_ROOT\" \"$TARGET\"; then\n"
      << "    if [ -e \"$TARGET\" ]; then rm -rf \"$TARGET\"; fi\n"
      << "    mv \"$BACKUP\" \"$TARGET\"\n"
      << "    fail \"Could not install the update, so the previous version was restored\"\n"
      << "fi\n";
    if (mac) {
        s << "xattr -dr com.apple.quarantine \"$TARGET\" 2>/dev/null || true\n";
    }
    s << "record \"ok $VERSION\"\n"
      << "rm -rf \"$ARCHIVE\" \"$EXTRACT\"\n"
      << "relaunch\n"
      << "exit 0\n";
    return s.str();
}

std::optional<std::string> WindowsInstallScript(const InstallPlan& plan) {
    struct Variable {
        std::string_view name;
        std::string value;
    };
    const std::array<Variable, 10> variables = {{
        {"ISLAND_PID", std::to_string(plan.pid)},
        {"ISLAND_VERSION", plan.version},
        {"ISLAND_ARCHIVE", PathUtf8(plan.archive)},
        {"ISLAND_EXTRACT", PathUtf8(plan.extract_dir())},
        {"ISLAND_TARGET", PathUtf8(plan.install_root)},
        {"ISLAND_BACKUP", PathUtf8(plan.backup_path())},
        {"ISLAND_BACKUP_PARENT", PathUtf8(plan.backup_path().parent_path())},
        {"ISLAND_RESULT", PathUtf8(plan.result_file())},
        {"ISLAND_LOG", PathUtf8(plan.log_file())},
        {"ISLAND_EXE", plan.executable_name},
    }};
    std::ostringstream s;
    s << "@echo off\r\n"
      << "rem Island updater: installs Island " << plan.version << " once the browser (pid "
      << plan.pid << ") exits.\r\n"
      << "rem Generated by Island; safe to delete.\r\n"
      << "setlocal EnableExtensions DisableDelayedExpansion\r\n"
      << "chcp 65001 >nul\r\n";
    for (const Variable& variable : variables) {
        const std::optional<std::string> value = CmdSetValue(variable.value);
        if (!value.has_value()) {
            return std::nullopt;
        }
        s << "set \"" << variable.name << '=' << *value << "\"\r\n";
    }
    s << "cd /d \"%~dp0\"\r\n"
      << "set /a TRIES=0\r\n"
      << ":wait\r\n"
      << "tasklist /FI \"PID eq %ISLAND_PID%\" /NH 2>nul | find \" %ISLAND_PID% \" >nul\r\n"
      << "if errorlevel 1 goto exited\r\n"
      << "set /a TRIES+=1\r\n"
      << "if %TRIES% GEQ 120 (\r\n"
      << "  > \"%ISLAND_RESULT%\" echo failed Island did not quit\r\n"
      << "  exit /b 1\r\n"
      << ")\r\n"
      << "ping -n 2 127.0.0.1 >nul\r\n"
      << "goto wait\r\n"
      << ":exited\r\n"
      << "if exist \"%ISLAND_EXTRACT%\" rmdir /s /q \"%ISLAND_EXTRACT%\"\r\n"
      << "mkdir \"%ISLAND_EXTRACT%\" || (set \"REASON=Could not create the staging folder\" & "
         "goto fail)\r\n"
      // The paths reach PowerShell through the environment, never through
      // its command text, so they need no PowerShell quoting.
      << "powershell.exe -NoLogo -NoProfile -NonInteractive -ExecutionPolicy Bypass -Command "
         "\"$ErrorActionPreference='Stop'; Expand-Archive -LiteralPath $env:ISLAND_ARCHIVE "
         "-DestinationPath $env:ISLAND_EXTRACT -Force\" >>\"%ISLAND_LOG%\" 2>&1\r\n"
      << "if errorlevel 1 (set \"REASON=Could not extract the update\" & goto fail)\r\n"
      << "if not exist \"%ISLAND_EXTRACT%\\%ISLAND_EXE%\" (set \"REASON=The update is missing "
         "%ISLAND_EXE%\" & goto fail)\r\n"
      << "if exist \"%ISLAND_BACKUP%\" rmdir /s /q \"%ISLAND_BACKUP%\"\r\n"
      << "if not exist \"%ISLAND_BACKUP_PARENT%\" mkdir \"%ISLAND_BACKUP_PARENT%\"\r\n"
      // Helper processes can hold files for a moment after the browser exits.
      << "set /a TRIES=0\r\n"
      << ":moveaside\r\n"
      << "move \"%ISLAND_TARGET%\" \"%ISLAND_BACKUP%\" >nul 2>&1\r\n"
      << "if not errorlevel 1 goto moved\r\n"
      << "set /a TRIES+=1\r\n"
      << "if %TRIES% GEQ 15 (set \"REASON=Could not move the current version aside\" & goto "
         "fail)\r\n"
      << "ping -n 2 127.0.0.1 >nul\r\n"
      << "goto moveaside\r\n"
      << ":moved\r\n"
      << "move \"%ISLAND_EXTRACT%\" \"%ISLAND_TARGET%\" >nul 2>&1\r\n"
      << "if errorlevel 1 goto restore\r\n"
      << "> \"%ISLAND_RESULT%\" echo ok %ISLAND_VERSION%\r\n"
      << "if exist \"%ISLAND_ARCHIVE%\" del /f /q \"%ISLAND_ARCHIVE%\"\r\n"
      << "start \"\" \"%ISLAND_TARGET%\\%ISLAND_EXE%\"\r\n"
      << "exit /b 0\r\n"
      << ":restore\r\n"
      << "if exist \"%ISLAND_TARGET%\" rmdir /s /q \"%ISLAND_TARGET%\"\r\n"
      << "move \"%ISLAND_BACKUP%\" \"%ISLAND_TARGET%\" >nul 2>&1\r\n"
      << "set \"REASON=Could not install the update, so the previous version was restored\"\r\n"
      << ":fail\r\n"
      << "> \"%ISLAND_RESULT%\" echo failed %REASON%\r\n"
      << "start \"\" \"%ISLAND_TARGET%\\%ISLAND_EXE%\"\r\n"
      << "exit /b 1\r\n";
    return s.str();
}

}  // namespace

std::string ReleasesApiUrl() {
    return "https://api.github.com/repos/" + std::string(kReleasesRepo) + "/releases";
}

std::string ReleaseNotesUrl(std::string_view tag) {
    return "https://github.com/" + std::string(kReleasesRepo) + "/releases/tag/" + std::string(tag);
}

// ---------------------------------------------------------------------------
// SemVer
// ---------------------------------------------------------------------------

std::string SemVer::ToString() const {
    std::string text =
        std::to_string(major) + "." + std::to_string(minor) + "." + std::to_string(patch);
    for (std::size_t i = 0; i < prerelease.size(); ++i) {
        text += i == 0 ? '-' : '.';
        text += prerelease[i];
    }
    return text;
}

std::optional<SemVer> ParseSemVer(std::string_view text) {
    if (text.empty() || text.size() > 256) {
        return std::nullopt;
    }
    const std::size_t plus = text.find('+');
    if (plus != std::string_view::npos) {
        for (const std::string_view id : SplitDots(text.substr(plus + 1))) {
            if (id.empty() || !std::all_of(id.begin(), id.end(), IsIdentifierChar)) {
                return std::nullopt;
            }
        }
        text = text.substr(0, plus);
    }
    std::string_view core = text;
    std::string_view pre;
    const std::size_t dash = text.find('-');
    if (dash != std::string_view::npos) {
        core = text.substr(0, dash);
        pre = text.substr(dash + 1);
        if (pre.empty()) {
            return std::nullopt;
        }
    }
    const std::vector<std::string_view> numbers = SplitDots(core);
    if (numbers.size() != 3) {
        return std::nullopt;
    }
    SemVer version;
    const std::optional<std::uint64_t> major = ParseNumericPart(numbers[0]);
    const std::optional<std::uint64_t> minor = ParseNumericPart(numbers[1]);
    const std::optional<std::uint64_t> patch = ParseNumericPart(numbers[2]);
    if (!major || !minor || !patch) {
        return std::nullopt;
    }
    version.major = *major;
    version.minor = *minor;
    version.patch = *patch;
    if (dash != std::string_view::npos) {
        for (const std::string_view id : SplitDots(pre)) {
            if (id.empty() || !std::all_of(id.begin(), id.end(), IsIdentifierChar)) {
                return std::nullopt;
            }
            if (IsNumericIdentifier(id) && id.size() > 1 && id[0] == '0') {
                return std::nullopt;
            }
            version.prerelease.emplace_back(id);
        }
    }
    return version;
}

int CompareSemVer(const SemVer& a, const SemVer& b) {
    if (a.major != b.major) return a.major < b.major ? -1 : 1;
    if (a.minor != b.minor) return a.minor < b.minor ? -1 : 1;
    if (a.patch != b.patch) return a.patch < b.patch ? -1 : 1;
    if (a.prerelease.empty() || b.prerelease.empty()) {
        if (a.prerelease.empty() && b.prerelease.empty()) return 0;
        // A release outranks any of its pre-releases.
        return a.prerelease.empty() ? 1 : -1;
    }
    const std::size_t common = std::min(a.prerelease.size(), b.prerelease.size());
    for (std::size_t i = 0; i < common; ++i) {
        const int compared = CompareIdentifiers(a.prerelease[i], b.prerelease[i]);
        if (compared != 0) return compared;
    }
    if (a.prerelease.size() == b.prerelease.size()) return 0;
    return a.prerelease.size() < b.prerelease.size() ? -1 : 1;
}

std::optional<SemVer> ParseReleaseTag(std::string_view tag) {
    if (tag.size() < 2 || tag[0] != 'v') {
        return std::nullopt;
    }
    return ParseSemVer(tag.substr(1));
}

// ---------------------------------------------------------------------------
// Releases
// ---------------------------------------------------------------------------

std::optional<std::vector<Release>> ParseReleases(std::string_view json_text) {
    if (json_text.size() > static_cast<std::size_t>(kMaxReleasesJsonBytes)) {
        return std::nullopt;
    }
    const std::optional<json::Value> root = json::Parse(json_text);
    if (!root.has_value() || !root->IsArray()) {
        return std::nullopt;
    }
    std::vector<Release> releases;
    std::size_t seen = 0;
    for (const json::Value& item : root->array_val) {
        if (++seen > kMaxReleases) {
            break;
        }
        if (!item.IsObject() || item.BoolOr("draft", false)) {
            continue;
        }
        const std::optional<SemVer> version = ParseReleaseTag(item.StringOr("tag_name", ""));
        if (!version.has_value()) {
            continue;  // nightly and any other non-versioned tag
        }
        Release release;
        release.tag = std::string(item.StringOr("tag_name", ""));
        release.version = *version;
        release.name = std::string(item.StringOr("name", ""));
        release.html_url = std::string(item.StringOr("html_url", ""));
        release.prerelease = item.BoolOr("prerelease", false);
        if (const json::Value* assets = item.FindMember("assets");
            assets != nullptr && assets->IsArray()) {
            for (const json::Value& asset : assets->array_val) {
                if (release.assets.size() >= kMaxAssetsPerRelease) {
                    break;
                }
                if (!asset.IsObject()) {
                    continue;
                }
                ReleaseAsset entry;
                entry.name = std::string(asset.StringOr("name", ""));
                entry.download_url = std::string(asset.StringOr("browser_download_url", ""));
                entry.size = std::max<std::int64_t>(0, asset.IntOr("size", 0));
                if (entry.name.empty() || entry.download_url.empty()) {
                    continue;
                }
                release.assets.push_back(std::move(entry));
            }
        }
        releases.push_back(std::move(release));
    }
    return releases;
}

std::optional<Release> SelectUpdate(const std::vector<Release>& releases, const SemVer& current,
                                    bool include_prereleases) {
    const bool offer_prereleases = include_prereleases || current.IsPrerelease();
    const Release* best = nullptr;
    for (const Release& release : releases) {
        if (CompareSemVer(release.version, current) <= 0) {
            continue;
        }
        if (release.version.IsPrerelease() && !offer_prereleases) {
            continue;
        }
        if (best == nullptr || CompareSemVer(release.version, best->version) > 0) {
            best = &release;
        }
    }
    if (best == nullptr) {
        return std::nullopt;
    }
    return *best;
}

// ---------------------------------------------------------------------------
// Targets and assets
// ---------------------------------------------------------------------------

std::string_view TargetName(Platform platform, Arch arch) noexcept {
    const bool arm = arch == Arch::kArm64;
    switch (platform) {
        case Platform::kMac:
            return arm ? "macosarm64" : "macosx64";
        case Platform::kWindows:
            return arm ? "windowsarm64" : "windows64";
        case Platform::kLinux:
            return arm ? "linuxarm64" : "linux64";
    }
    return {};
}

std::optional<Platform> CurrentPlatform() noexcept {
#if defined(__APPLE__)
    return Platform::kMac;
#elif defined(_WIN32)
    return Platform::kWindows;
#elif defined(__linux__)
    return Platform::kLinux;
#else
    return std::nullopt;
#endif
}

std::optional<Arch> CurrentArch() noexcept {
#if defined(__aarch64__) || defined(__arm64__) || defined(_M_ARM64)
    return Arch::kArm64;
#elif defined(__x86_64__) || defined(_M_X64) || defined(_M_AMD64)
    return Arch::kX64;
#else
    return std::nullopt;
#endif
}

std::string_view CurrentTarget() noexcept {
    const std::optional<Platform> platform = CurrentPlatform();
    const std::optional<Arch> arch = CurrentArch();
    if (!platform.has_value() || !arch.has_value()) {
        return {};
    }
    return TargetName(*platform, *arch);
}

std::string_view ArchiveSuffix(std::string_view target) noexcept {
    if (target == "macosx64" || target == "macosarm64" || target == "windows64" ||
        target == "windowsarm64") {
        return ".zip";
    }
    if (target == "linux64" || target == "linuxarm64") {
        return ".tar.gz";
    }
    return {};
}

std::string ArchiveName(std::string_view version, std::string_view target) {
    const std::string_view suffix = ArchiveSuffix(target);
    if (suffix.empty() || version.empty()) {
        return {};
    }
    return "island_browser-" + std::string(version) + "-" + std::string(target) +
           std::string(suffix);
}

std::optional<SelectedAssets> SelectAssets(const Release& release, std::string_view target) {
    const std::string archive_name = ArchiveName(release.version.ToString(), target);
    if (archive_name.empty()) {
        return std::nullopt;
    }
    const ReleaseAsset* archive = nullptr;
    const ReleaseAsset* checksums = nullptr;
    for (const ReleaseAsset& asset : release.assets) {
        if (asset.name == archive_name && archive == nullptr) {
            archive = &asset;
        } else if (asset.name == kChecksumsAssetName && checksums == nullptr) {
            checksums = &asset;
        }
    }
    if (archive == nullptr || checksums == nullptr || !IsAllowedUpdateUrl(archive->download_url) ||
        !IsAllowedUpdateUrl(checksums->download_url)) {
        return std::nullopt;
    }
    return SelectedAssets{*archive, *checksums};
}

std::vector<ChecksumEntry> ParseSha256Sums(std::string_view text) {
    std::vector<ChecksumEntry> entries;
    std::size_t start = 0;
    while (start < text.size()) {
        std::size_t end = text.find('\n', start);
        if (end == std::string_view::npos) {
            end = text.size();
        }
        std::string_view line = text.substr(start, end - start);
        start = end + 1;
        if (!line.empty() && line.back() == '\r') {
            line.remove_suffix(1);
        }
        if (line.size() < 66) {
            continue;
        }
        const std::string_view hex = line.substr(0, 64);
        if (!std::all_of(hex.begin(), hex.end(), [](char c) {
                return std::isxdigit(static_cast<unsigned char>(c)) != 0;
            })) {
            continue;
        }
        // "<hex>  <name>" (text mode) or "<hex> *<name>" (binary mode).
        if (line[64] != ' ' || (line[65] != ' ' && line[65] != '*')) {
            continue;
        }
        const std::string_view name = line.substr(66);
        if (name.empty()) {
            continue;
        }
        entries.push_back({AsciiLower(hex), std::string(name)});
    }
    return entries;
}

std::optional<std::string> FindChecksum(std::string_view sums_text, std::string_view filename) {
    std::optional<std::string> found;
    for (const ChecksumEntry& entry : ParseSha256Sums(sums_text)) {
        if (entry.filename != filename) {
            continue;
        }
        if (found.has_value() && *found != entry.sha256) {
            return std::nullopt;  // contradictory listing
        }
        found = entry.sha256;
    }
    return found;
}

bool IsAllowedUpdateUrl(std::string_view url) {
    constexpr std::string_view kScheme = "https://";
    if (url.size() > 4096 || url.size() <= kScheme.size() ||
        AsciiLower(url.substr(0, kScheme.size())) != kScheme) {
        return false;
    }
    for (const char c : url) {
        if (static_cast<unsigned char>(c) <= 0x20 || c == 0x7F || c == '\\') {
            return false;
        }
    }
    const std::string_view rest = url.substr(kScheme.size());
    std::string_view authority = rest.substr(0, rest.find_first_of("/?#"));
    if (authority.empty() || authority.find('@') != std::string_view::npos) {
        return false;
    }
    if (const std::size_t colon = authority.find(':'); colon != std::string_view::npos) {
        if (authority.substr(colon + 1) != "443") {
            return false;
        }
        authority = authority.substr(0, colon);
    }
    const std::string host = AsciiLower(authority);
    if (host.empty() || host.front() == '.' || host.back() == '.' ||
        host.find("..") != std::string::npos || !std::all_of(host.begin(), host.end(), [](char c) {
            return (c >= 'a' && c <= 'z') || IsAsciiDigit(c) || c == '.' || c == '-';
        })) {
        return false;
    }
    constexpr std::string_view kUserContent = ".githubusercontent.com";
    return host == "github.com" || host == "api.github.com" ||
           (EndsWith(host, kUserContent) && host.size() > kUserContent.size());
}

std::optional<std::string> ResolveRedirect(std::string_view from, std::string_view location) {
    location = TrimAscii(location);
    std::string candidate;
    if (location.find("://") != std::string_view::npos) {
        candidate = std::string(location);
    } else if (location.size() > 1 && location[0] == '/' && location[1] != '/') {
        const std::string_view origin = UrlOrigin(from);
        if (origin.empty()) {
            return std::nullopt;
        }
        candidate = std::string(origin) + std::string(location);
    } else {
        return std::nullopt;
    }
    if (!IsAllowedUpdateUrl(candidate)) {
        return std::nullopt;
    }
    return candidate;
}

// ---------------------------------------------------------------------------
// Install location
// ---------------------------------------------------------------------------

InstallLocation DetectInstallLocation(Platform platform, const std::filesystem::path& executable) {
    InstallLocation location;
    location.platform = platform;
    location.support = InstallSupport::kUnrecognizedLayout;
    if (executable.empty() || !executable.has_parent_path()) {
        return location;
    }
    location.executable_name = PathUtf8(executable.filename());

    // A CMake build directory: Island built from source updates through git.
    std::filesystem::path directory = executable.parent_path();
    std::error_code error;
    for (int depth = 0; depth < 6 && !directory.empty(); ++depth) {
        if (std::filesystem::exists(directory / "CMakeCache.txt", error)) {
            location.support = InstallSupport::kBuildTree;
            return location;
        }
        const std::filesystem::path parent = directory.parent_path();
        if (parent == directory) {
            break;
        }
        directory = parent;
    }

    if (platform == Platform::kMac) {
        const std::filesystem::path macos = executable.parent_path();
        const std::filesystem::path contents = macos.parent_path();
        const std::filesystem::path bundle = contents.parent_path();
        if (macos.filename() != "MacOS" || contents.filename() != "Contents" ||
            bundle.extension() != ".app" || !bundle.has_parent_path()) {
            return location;
        }
        location.install_root = bundle;
    } else {
        const std::filesystem::path root = executable.parent_path();
        const std::filesystem::path parent = root.parent_path();
        // A packaged install is the archive's contents: build-metadata.json
        // and libcef beside the executable. Anything else (a loose binary in
        // a shared folder, the home directory, a drive root) is never swapped.
        const char* const libcef = platform == Platform::kWindows ? "libcef.dll" : "libcef.so";
        if (root.empty() || parent.empty() || parent == root ||
            !std::filesystem::is_regular_file(root / "build-metadata.json", error) ||
            !std::filesystem::is_regular_file(root / libcef, error)) {
            return location;
        }
        if (const std::optional<std::filesystem::path> home = HomeDirectory();
            home.has_value() && SamePath(root, *home)) {
            return location;
        }
        std::size_t entries = 0;
        for (std::filesystem::directory_iterator it(root, error), end; !error && it != end;
             it.increment(error)) {
            if (++entries > 64) {
                return location;
            }
        }
        if (error) {
            return location;
        }
        location.install_root = root;
    }

    location.staging_dir =
        location.install_root.parent_path() /
        ("." + PathUtf8(location.install_root.filename()) + std::string(kStagingSuffix));
    location.staging_dir = PathFromUtf8(PathUtf8(location.staging_dir));
    if (!ProbeWritable(location.install_root.parent_path()) ||
        !ProbeWritable(location.install_root)) {
        location.support = InstallSupport::kNotWritable;
        return location;
    }
    location.support = InstallSupport::kSupported;
    return location;
}

std::string_view InstallSupportMessage(InstallSupport support) noexcept {
    switch (support) {
        case InstallSupport::kSupported:
            return {};
        case InstallSupport::kUnsupportedPlatform:
            return "Updates aren't available for this platform.";
        case InstallSupport::kBuildTree:
            return "Updates are managed by your build.";
        case InstallSupport::kUnrecognizedLayout:
            return "This copy of Island wasn't installed from a release package, so it can't "
                   "update itself.";
        case InstallSupport::kNotWritable:
            return "Island can't write to its install folder. Download the update from the "
                   "release page instead.";
    }
    return {};
}

// ---------------------------------------------------------------------------
// Apply script
// ---------------------------------------------------------------------------

std::string ShellQuote(std::string_view text) {
    std::string quoted = "'";
    for (const char c : text) {
        if (c == '\'') {
            quoted += "'\\''";
        } else {
            quoted += c;
        }
    }
    quoted += '\'';
    return quoted;
}

std::optional<std::string> CmdSetValue(std::string_view text) {
    std::string value;
    for (const char c : text) {
        if (c == '"' || c == '\r' || c == '\n' || c == '\0') {
            return std::nullopt;
        }
        if (c == '%') {
            value += "%%";
        } else {
            value += c;
        }
    }
    return value;
}

std::optional<std::string> GenerateInstallScript(const InstallPlan& plan) {
    if (plan.pid <= 0 || !ParseSemVer(plan.version).has_value() || plan.archive.empty() ||
        plan.staging_dir.empty() || plan.install_root.empty() || plan.executable_name.empty() ||
        plan.executable_name.find_first_of("/\\") != std::string::npos) {
        return std::nullopt;
    }
    for (const std::filesystem::path& path : {plan.archive, plan.staging_dir, plan.install_root}) {
        const std::string text = PathUtf8(path);
        if (text.find_first_of(std::string_view("\r\n\0", 3)) != std::string::npos) {
            return std::nullopt;
        }
    }
    if (plan.platform == Platform::kWindows) {
        return WindowsInstallScript(plan);
    }
    return PosixInstallScript(plan);
}

std::string PathUtf8(const std::filesystem::path& path) {
    const std::u8string text = path.u8string();
    return std::string(text.begin(), text.end());
}

std::filesystem::path PathFromUtf8(std::string_view text) {
    return std::filesystem::path(std::u8string(text.begin(), text.end()));
}

bool LaunchDetachedScript(Platform platform, const std::filesystem::path& script) {
#if defined(_WIN32)
    if (platform != Platform::kWindows) {
        return false;
    }
    std::wstring system_dir(MAX_PATH, L'\0');
    const UINT length = GetSystemDirectoryW(system_dir.data(), MAX_PATH);
    if (length == 0 || length >= MAX_PATH) {
        return false;
    }
    system_dir.resize(length);
    const std::wstring cmd = system_dir + L"\\cmd.exe";
    // cmd /c strips the outer quote pair, leaving the quoted script path.
    std::wstring command_line = L"\"" + cmd + L"\" /d /c \"\"" + script.wstring() + L"\"\"";
    STARTUPINFOW startup = {};
    startup.cb = sizeof(startup);
    PROCESS_INFORMATION process = {};
    const std::wstring working_dir = script.parent_path().wstring();
    if (CreateProcessW(cmd.c_str(), command_line.data(), nullptr, nullptr, FALSE,
                       CREATE_NO_WINDOW | CREATE_NEW_PROCESS_GROUP, nullptr, working_dir.c_str(),
                       &startup, &process) == 0) {
        return false;
    }
    CloseHandle(process.hThread);
    CloseHandle(process.hProcess);
    return true;
#else
    if (platform == Platform::kWindows) {
        return false;
    }
    posix_spawn_file_actions_t actions;
    if (posix_spawn_file_actions_init(&actions) != 0) {
        return false;
    }
    posix_spawn_file_actions_addopen(&actions, 0, "/dev/null", O_RDONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 1, "/dev/null", O_WRONLY, 0);
    posix_spawn_file_actions_addopen(&actions, 2, "/dev/null", O_WRONLY, 0);
    posix_spawnattr_t attributes;
    if (posix_spawnattr_init(&attributes) != 0) {
        posix_spawn_file_actions_destroy(&actions);
        return false;
    }
    short flags = POSIX_SPAWN_SETSIGMASK | POSIX_SPAWN_SETSIGDEF;
#if defined(POSIX_SPAWN_SETSID)
    flags |= POSIX_SPAWN_SETSID;
#else
    flags |= POSIX_SPAWN_SETPGROUP;
    posix_spawnattr_setpgroup(&attributes, 0);
#endif
    posix_spawnattr_setflags(&attributes, flags);
    // The browser's blocked or ignored signals must not leak into the script.
    sigset_t no_signals;
    sigemptyset(&no_signals);
    posix_spawnattr_setsigmask(&attributes, &no_signals);
    sigset_t defaults;
    sigemptyset(&defaults);
    for (const int signal_number : {SIGHUP, SIGINT, SIGPIPE, SIGTERM, SIGCHLD}) {
        sigaddset(&defaults, signal_number);
    }
    posix_spawnattr_setsigdefault(&attributes, &defaults);

    std::string shell = "/bin/sh";
    std::string script_path = script.string();
    std::array<char*, 3> argv = {shell.data(), script_path.data(), nullptr};
    pid_t child = 0;
    const int result =
        posix_spawn(&child, shell.c_str(), &actions, &attributes, argv.data(), environ);
    posix_spawnattr_destroy(&attributes);
    posix_spawn_file_actions_destroy(&actions);
    return result == 0;
#endif
}

std::int64_t CurrentProcessId() noexcept {
#if defined(_WIN32)
    return static_cast<std::int64_t>(GetCurrentProcessId());
#else
    return static_cast<std::int64_t>(getpid());
#endif
}

std::optional<InstallOutcome> ParseInstallOutcome(std::string_view text) {
    text = TrimAscii(text);
    const std::size_t newline = text.find_first_of("\r\n");
    if (newline != std::string_view::npos) {
        text = TrimAscii(text.substr(0, newline));
    }
    if (text.rfind("ok ", 0) == 0) {
        return InstallOutcome{true, std::string(TrimAscii(text.substr(3)))};
    }
    if (text.rfind("failed ", 0) == 0) {
        return InstallOutcome{false, std::string(TrimAscii(text.substr(7)))};
    }
    return std::nullopt;
}

// ---------------------------------------------------------------------------
// Download sink
// ---------------------------------------------------------------------------

DownloadSink::~DownloadSink() {
    if (file_ != nullptr) {
        Discard();
    }
}

bool DownloadSink::Open(const std::filesystem::path& file, std::int64_t max_bytes) {
    path_ = file;
    max_bytes_ = max_bytes;
    size_ = 0;
    overflowed_ = false;
    failed_ = false;
    body_.clear();
    if (!file.empty()) {
#if defined(_WIN32)
        file_ = _wfopen(file.c_str(), L"wb");
#else
        file_ = std::fopen(file.c_str(), "wb");
#endif
        if (file_ == nullptr) {
            failed_ = true;
            return false;
        }
    }
    open_ = true;
    return true;
}

bool DownloadSink::Append(const void* data, std::size_t size) {
    if (!open_ || failed_) {
        return false;
    }
    if (static_cast<std::int64_t>(size) > max_bytes_ - size_) {
        overflowed_ = true;
        failed_ = true;
        return false;
    }
    hasher_.Update(data, size);
    if (file_ != nullptr) {
        if (std::fwrite(data, 1, size, file_) != size) {
            failed_ = true;
            return false;
        }
    } else {
        body_.append(static_cast<const char*>(data), size);
    }
    size_ += static_cast<std::int64_t>(size);
    return true;
}

bool DownloadSink::Close(std::string* sha256_hex) {
    if (file_ != nullptr) {
        if (std::fflush(file_) != 0) {
            failed_ = true;
        }
        if (std::fclose(file_) != 0) {
            failed_ = true;
        }
        file_ = nullptr;
    }
    open_ = false;
    const std::string hex = hasher_.FinishHex();
    if (sha256_hex != nullptr) {
        *sha256_hex = hex;
    }
    return !failed_;
}

void DownloadSink::Discard() {
    if (file_ != nullptr) {
        std::fclose(file_);
        file_ = nullptr;
    }
    open_ = false;
    if (!path_.empty()) {
        std::error_code error;
        std::filesystem::remove(path_, error);
    }
    body_.clear();
}

// ---------------------------------------------------------------------------
// Check policy
// ---------------------------------------------------------------------------

bool UpdatesDisabledByEnvironment() {
    const char* const value = std::getenv("ISLAND_DISABLE_UPDATES");
    return value != nullptr && *value != '\0' && std::string_view(value) != "0";
}

bool AutoCheckDue(bool auto_check_enabled, std::int64_t last_check_unix,
                  std::int64_t now_unix) noexcept {
    if (!auto_check_enabled) {
        return false;
    }
    if (last_check_unix <= 0 || now_unix < last_check_unix) {
        return true;
    }
    return now_unix - last_check_unix >= kAutoCheckIntervalSeconds;
}

// ---------------------------------------------------------------------------
// Updater
// ---------------------------------------------------------------------------

std::string_view UpdateStatusName(UpdateStatus status) noexcept {
    switch (status) {
        case UpdateStatus::kIdle:
            return "idle";
        case UpdateStatus::kChecking:
            return "checking";
        case UpdateStatus::kUpToDate:
            return "up_to_date";
        case UpdateStatus::kUnavailable:
            return "unavailable";
        case UpdateStatus::kAvailable:
            return "available";
        case UpdateStatus::kDownloading:
            return "downloading";
        case UpdateStatus::kReady:
            return "ready";
        case UpdateStatus::kError:
            return "error";
    }
    return "idle";
}

Updater::Updater(Config config, std::unique_ptr<UpdateFetcher> fetcher,
                 std::function<void()> on_change)
    : config_(std::move(config)), fetcher_(std::move(fetcher)), on_change_(std::move(on_change)) {
    current_ = ParseSemVer(config_.current_version);
    snapshot_.current_version = config_.current_version;
    RefreshBlockedReason();
}

Updater::~Updater() { Cancel(); }

void Updater::Changed() {
    if (on_change_) {
        on_change_();
    }
}

void Updater::SetError(std::string message) {
    snapshot_.status = UpdateStatus::kError;
    snapshot_.error = std::move(message);
    snapshot_.progress_percent = -1;
    Changed();
}

void Updater::RefreshBlockedReason() {
    if (config_.target.empty()) {
        snapshot_.install_blocked_reason =
            std::string(InstallSupportMessage(InstallSupport::kUnsupportedPlatform));
    } else if (config_.install.support != InstallSupport::kSupported) {
        snapshot_.install_blocked_reason =
            std::string(InstallSupportMessage(config_.install.support));
    } else if (release_.has_value() && !assets_.has_value()) {
        snapshot_.install_blocked_reason =
            "This release has no verified download for " + config_.target + ".";
    } else {
        snapshot_.install_blocked_reason.clear();
    }
}

std::filesystem::path Updater::ArchivePath() const {
    if (!release_.has_value()) {
        return {};
    }
    return config_.install.staging_dir /
           PathFromUtf8(ArchiveName(release_->version.ToString(), config_.target));
}

bool Updater::Check(bool include_prereleases) {
    if (fetcher_ == nullptr || snapshot_.status == UpdateStatus::kChecking ||
        snapshot_.status == UpdateStatus::kDownloading ||
        snapshot_.status == UpdateStatus::kReady) {
        return false;
    }
    if (!current_.has_value()) {
        SetError("This build's version (" + config_.current_version +
                 ") is not a release version.");
        return false;
    }
    snapshot_.status = UpdateStatus::kChecking;
    snapshot_.error.clear();
    snapshot_.progress_percent = -1;
    Changed();
    fetcher_->Start(FetchRequest{.url = config_.releases_url,
                                 .accept = "application/vnd.github+json",
                                 .max_bytes = kMaxReleasesJsonBytes,
                                 .destination = {}},
                    nullptr, [this, include_prereleases](FetchResult result) {
                        OnReleasesFetched(std::move(result), include_prereleases);
                    });
    return true;
}

void Updater::OnReleasesFetched(FetchResult result, bool include_prereleases) {
    if (!result.ok && (result.http_status == 403 || result.http_status == 404)) {
        // A private repository (or an exhausted anonymous rate limit) answers
        // 404/403: there is simply no update information right now.
        release_.reset();
        assets_.reset();
        snapshot_.status = UpdateStatus::kUnavailable;
        snapshot_.latest_version.clear();
        snapshot_.release_name.clear();
        snapshot_.release_url.clear();
        RefreshBlockedReason();
        Changed();
        return;
    }
    if (!result.ok) {
        SetError(result.error.empty() ? "Couldn't reach GitHub to check for updates."
                                      : "Couldn't check for updates: " + result.error);
        return;
    }
    const std::optional<std::vector<Release>> releases = ParseReleases(result.body);
    if (!releases.has_value()) {
        SetError("GitHub returned an unexpected response.");
        return;
    }
    std::optional<Release> selected = SelectUpdate(*releases, *current_, include_prereleases);
    if (!selected.has_value()) {
        release_.reset();
        assets_.reset();
        snapshot_.status = UpdateStatus::kUpToDate;
        snapshot_.latest_version.clear();
        snapshot_.release_name.clear();
        snapshot_.release_url.clear();
        RefreshBlockedReason();
        Changed();
        return;
    }
    assets_ = SelectAssets(*selected, config_.target);
    release_ = std::move(selected);
    snapshot_.status = UpdateStatus::kAvailable;
    snapshot_.latest_version = release_->version.ToString();
    snapshot_.release_name = release_->name;
    snapshot_.release_url = ReleaseNotesUrl(release_->tag);
    RefreshBlockedReason();
    Changed();
}

bool Updater::StartDownload() {
    if (fetcher_ == nullptr || snapshot_.status != UpdateStatus::kAvailable ||
        !release_.has_value() || !assets_.has_value() ||
        !snapshot_.install_blocked_reason.empty()) {
        return false;
    }
    if (assets_->archive.size > kMaxArchiveBytes) {
        SetError("The update is larger than Island accepts.");
        return false;
    }
    snapshot_.status = UpdateStatus::kDownloading;
    snapshot_.progress_percent = 0;
    snapshot_.error.clear();
    Changed();
    fetcher_->Start(FetchRequest{.url = assets_->checksums.download_url,
                                 .accept = "application/octet-stream",
                                 .max_bytes = kMaxChecksumsBytes,
                                 .destination = {}},
                    nullptr, [this](FetchResult result) { OnChecksumsFetched(std::move(result)); });
    return true;
}

void Updater::OnChecksumsFetched(FetchResult result) {
    if (!result.ok) {
        SetError("Couldn't download the release checksums" +
                 (result.error.empty() ? std::string(".") : ": " + result.error));
        return;
    }
    const std::optional<std::string> expected = FindChecksum(result.body, assets_->archive.name);
    if (!expected.has_value()) {
        SetError("The release lists no checksum for " + assets_->archive.name + ".");
        return;
    }
    std::error_code error;
    std::filesystem::create_directories(config_.install.staging_dir, error);
    if (error) {
        SetError("Couldn't create the update folder.");
        return;
    }
    std::filesystem::path partial = ArchivePath();
    partial += ".part";
    std::filesystem::remove(partial, error);
    const std::int64_t cap = assets_->archive.size > 0 ? assets_->archive.size : kMaxArchiveBytes;
    fetcher_->Start(
        FetchRequest{.url = assets_->archive.download_url,
                     .accept = "application/octet-stream",
                     .max_bytes = cap,
                     .destination = partial},
        [this](std::int64_t received, std::int64_t total) { OnArchiveProgress(received, total); },
        [this, sha = *expected](FetchResult fetched) {
            OnArchiveFetched(std::move(fetched), sha);
        });
}

void Updater::OnArchiveProgress(std::int64_t received, std::int64_t total) {
    if (snapshot_.status != UpdateStatus::kDownloading) {
        return;
    }
    if (total <= 0 && assets_.has_value()) {
        total = assets_->archive.size;
    }
    const int percent =
        total > 0 ? static_cast<int>(std::clamp<std::int64_t>(received * 100 / total, 0, 100)) : -1;
    if (percent != snapshot_.progress_percent) {
        snapshot_.progress_percent = percent;
        Changed();
    }
}

void Updater::OnArchiveFetched(FetchResult result, std::string expected_sha256) {
    std::filesystem::path partial = ArchivePath();
    partial += ".part";
    std::error_code error;
    if (!result.ok) {
        std::filesystem::remove(partial, error);
        SetError("Couldn't download the update" +
                 (result.error.empty() ? std::string(".") : ": " + result.error));
        return;
    }
    if (result.sha256_hex != expected_sha256) {
        std::filesystem::remove(partial, error);
        SetError("The download didn't match its published checksum, so it was discarded.");
        return;
    }
    if (assets_->archive.size > 0 && result.size != assets_->archive.size) {
        std::filesystem::remove(partial, error);
        SetError("The download was incomplete.");
        return;
    }
    const std::filesystem::path archive = ArchivePath();
    std::filesystem::remove(archive, error);
    std::filesystem::rename(partial, archive, error);
    if (error) {
        std::filesystem::remove(partial, error);
        SetError("Couldn't save the update.");
        return;
    }
    verified_sha256_ = std::move(expected_sha256);
    snapshot_.status = UpdateStatus::kReady;
    snapshot_.progress_percent = 100;
    Changed();
}

std::optional<std::filesystem::path> Updater::PrepareInstall(std::int64_t pid) {
    if (snapshot_.status != UpdateStatus::kReady || !release_.has_value()) {
        return std::nullopt;
    }
    const std::filesystem::path archive = ArchivePath();
    // Never hand the script a file that changed after verification.
    const std::optional<std::string> hash = Sha256::HexOfFile(archive);
    if (!hash.has_value() || *hash != verified_sha256_) {
        std::error_code error;
        std::filesystem::remove(archive, error);
        SetError("The downloaded update changed on disk. Check for updates to download it again.");
        return std::nullopt;
    }
    InstallPlan plan;
    plan.platform = config_.install.platform;
    plan.pid = pid;
    plan.version = release_->version.ToString();
    plan.archive = archive;
    plan.staging_dir = config_.install.staging_dir;
    plan.install_root = config_.install.install_root;
    plan.executable_name = config_.install.executable_name;
    const std::optional<std::string> script = GenerateInstallScript(plan);
    if (!script.has_value()) {
        SetError("Island's install path can't be used by the updater.");
        return std::nullopt;
    }
    std::error_code error;
    std::filesystem::remove(plan.result_file(), error);
    const std::filesystem::path script_path = plan.script_path();
    {
        std::ofstream out(script_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            SetError("Couldn't write the update script.");
            return std::nullopt;
        }
        out << *script;
        if (!out) {
            SetError("Couldn't write the update script.");
            return std::nullopt;
        }
    }
#if !defined(_WIN32)
    std::filesystem::permissions(script_path, std::filesystem::perms::owner_all,
                                 std::filesystem::perm_options::replace, error);
#endif
    return script_path;
}

std::optional<InstallOutcome> Updater::FinalizePreviousInstall() {
    const std::filesystem::path staging = config_.install.staging_dir;
    if (staging.empty() || !EndsWith(PathUtf8(staging.filename()), kStagingSuffix)) {
        return std::nullopt;
    }
    std::error_code error;
    if (!std::filesystem::exists(staging, error)) {
        return std::nullopt;
    }
    std::optional<InstallOutcome> outcome;
    if (const std::optional<std::string> text = ReadSmallFile(staging / "result.txt", 4096)) {
        outcome = ParseInstallOutcome(*text);
    }
    // Only the window that just launched cleans up; a download in flight owns
    // the folder.
    if (snapshot_.status != UpdateStatus::kDownloading &&
        snapshot_.status != UpdateStatus::kReady) {
        std::filesystem::remove_all(staging, error);
    }
    if (outcome.has_value()) {
        snapshot_.notice = outcome->ok ? "Island was updated to " + outcome->detail + "."
                                       : "The last update didn't install: " + outcome->detail +
                                             ". Island kept the previous version.";
        Changed();
    }
    return outcome;
}

void Updater::Cancel() {
    if (fetcher_ != nullptr) {
        fetcher_->Cancel();
    }
    if (snapshot_.status == UpdateStatus::kChecking) {
        snapshot_.status = UpdateStatus::kIdle;
    } else if (snapshot_.status == UpdateStatus::kDownloading) {
        std::filesystem::path partial = ArchivePath();
        partial += ".part";
        std::error_code error;
        std::filesystem::remove(partial, error);
        snapshot_.status = release_.has_value() ? UpdateStatus::kAvailable : UpdateStatus::kIdle;
        snapshot_.progress_percent = -1;
    }
}

json::Value Updater::StateJson() const {
    using json::Value;
    const bool can_install = snapshot_.install_blocked_reason.empty();
    return Value::MakeObject()
        .Set("status", Value::String(std::string(UpdateStatusName(snapshot_.status))))
        .Set("current_version", Value::String(snapshot_.current_version))
        .Set("latest_version", Value::String(snapshot_.latest_version))
        .Set("release_name", Value::String(snapshot_.release_name))
        .Set("release_url", Value::String(snapshot_.release_url))
        .Set("progress", Value::Int(snapshot_.progress_percent))
        .Set("error", Value::String(snapshot_.error))
        .Set("can_install", Value::Bool(can_install))
        .Set("install_blocked_reason", Value::String(snapshot_.install_blocked_reason))
        .Set("notice", Value::String(snapshot_.notice))
        .Set("target", Value::String(config_.target));
}

}  // namespace island::update
