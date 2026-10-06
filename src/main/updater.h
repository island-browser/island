#ifndef ISLAND_UPDATER_H_
#define ISLAND_UPDATER_H_

// The in-browser updater's CEF-free core: SemVer precedence, GitHub Releases
// parsing and selection, per-target asset and SHA256SUMS lookup, install
// location detection, the apply-script generator, and the Updater state
// machine. Network access goes through the UpdateFetcher seam, implemented
// over CefURLRequest in cef_update_fetcher.{h,cc}; nothing here includes CEF,
// so the whole policy is unit-tested without a CEF runtime.
//
// Release contract (shared with the CI release workflow):
//   - https://api.github.com/repos/<kReleasesRepo>/releases lists releases.
//   - Versioned releases are tagged `vX.Y.Z[-pre]`; drafts, the rolling
//     `nightly` tag and every tag that is not `v` + SemVer are ignored.
//   - Assets: island_browser-<version>-<target>.zip (macOS, Windows) or
//     .tar.gz (Linux), plus SHA256SUMS.txt with `<sha256>  <file>` lines.
//
// Applying an update: the browser downloads the archive into a staging
// directory beside the install, verifies its SHA-256 against SHA256SUMS.txt,
// and on "Restart to update" writes a small platform script and launches it
// detached. The script waits for the browser to exit, extracts the archive
// with the system tool (ditto / tar / Expand-Archive), moves the old install
// aside as a backup, moves the new one into place (restoring the backup when
// that fails), records the outcome, and relaunches Island. The next launch
// reads the outcome and removes the staging directory and the backup.

#include <cstdint>
#include <cstdio>
#include <filesystem>
#include <functional>
#include <memory>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

#include "json_util.h"
#include "sha256.h"

namespace island::update {

// The GitHub owner/repo that publishes Island releases. The only place it is
// written: the API URL and release-notes links are derived from it.
inline constexpr std::string_view kReleasesRepo = "island-browser/island";
// https://api.github.com/repos/<kReleasesRepo>/releases
[[nodiscard]] std::string ReleasesApiUrl();
// https://github.com/<kReleasesRepo>/releases/tag/<tag>
[[nodiscard]] std::string ReleaseNotesUrl(std::string_view tag);
inline constexpr std::string_view kChecksumsAssetName = "SHA256SUMS.txt";
// Automatic checks run at most once per this interval.
inline constexpr std::int64_t kAutoCheckIntervalSeconds = 24 * 60 * 60;
// Delay between startup and the automatic check.
inline constexpr std::int64_t kStartupCheckDelayMs = 10 * 1000;
// Size caps for each fetch.
inline constexpr std::int64_t kMaxReleasesJsonBytes = 8 * 1024 * 1024;
inline constexpr std::int64_t kMaxChecksumsBytes = 64 * 1024;
inline constexpr std::int64_t kMaxArchiveBytes = 1024LL * 1024 * 1024;
inline constexpr int kMaxRedirects = 5;
// Parsing bounds for the releases document.
inline constexpr std::size_t kMaxReleases = 100;
inline constexpr std::size_t kMaxAssetsPerRelease = 64;

// ---------------------------------------------------------------------------
// Semantic versions
// ---------------------------------------------------------------------------

struct SemVer {
    std::uint64_t major = 0;
    std::uint64_t minor = 0;
    std::uint64_t patch = 0;
    // Dot-separated pre-release identifiers ("beta", "1" for -beta.1).
    std::vector<std::string> prerelease;

    [[nodiscard]] bool IsPrerelease() const noexcept { return !prerelease.empty(); }
    // Canonical text without build metadata, e.g. "0.5.0-beta.1".
    [[nodiscard]] std::string ToString() const;
    bool operator==(const SemVer&) const = default;
};

// Strict SemVer 2.0.0: no leading zeros in numeric parts or numeric
// pre-release identifiers, no empty identifiers. Build metadata ("+...") is
// validated and dropped, since it has no precedence.
[[nodiscard]] std::optional<SemVer> ParseSemVer(std::string_view text);
// SemVer 2.0.0 precedence: negative, zero, or positive like strcmp.
[[nodiscard]] int CompareSemVer(const SemVer& a, const SemVer& b);
// A release tag: "v" followed by SemVer. "nightly", "0.4.0" and "v0.4" are not.
[[nodiscard]] std::optional<SemVer> ParseReleaseTag(std::string_view tag);

// ---------------------------------------------------------------------------
// Releases
// ---------------------------------------------------------------------------

struct ReleaseAsset {
    std::string name;
    std::string download_url;  // browser_download_url
    std::int64_t size = 0;     // bytes as reported by GitHub; 0 when unknown

    bool operator==(const ReleaseAsset&) const = default;
};

struct Release {
    std::string tag;
    SemVer version;
    std::string name;
    std::string html_url;     // release notes page
    bool prerelease = false;  // GitHub's flag; informational (builds are unsigned)
    std::vector<ReleaseAsset> assets;
};

// Parses the GitHub releases array. Drafts, non-SemVer tags (including
// `nightly`) and malformed entries are skipped; at most kMaxReleases releases
// and kMaxAssetsPerRelease assets each are kept. std::nullopt when the text is
// oversized, not JSON, or not an array.
[[nodiscard]] std::optional<std::vector<Release>> ParseReleases(std::string_view json_text);

// The newest release strictly newer than |current|. A version with SemVer
// pre-release identifiers (0.5.0-beta.1) is only offered when
// |include_prereleases| is set or |current| is itself a pre-release; GitHub's
// `prerelease` flag does not affect selection.
[[nodiscard]] std::optional<Release> SelectUpdate(const std::vector<Release>& releases,
                                                  const SemVer& current, bool include_prereleases);

// ---------------------------------------------------------------------------
// Targets and assets
// ---------------------------------------------------------------------------

enum class Platform : std::uint8_t { kMac, kWindows, kLinux };
enum class Arch : std::uint8_t { kX64, kArm64 };

// "macosx64", "macosarm64", "windows64", "windowsarm64", "linux64", "linuxarm64".
[[nodiscard]] std::string_view TargetName(Platform platform, Arch arch) noexcept;
[[nodiscard]] std::optional<Platform> CurrentPlatform() noexcept;
[[nodiscard]] std::optional<Arch> CurrentArch() noexcept;
// The running build's target name, or "" on an unsupported host.
[[nodiscard]] std::string_view CurrentTarget() noexcept;
// ".zip" for macOS/Windows targets, ".tar.gz" for Linux; "" for unknown targets.
[[nodiscard]] std::string_view ArchiveSuffix(std::string_view target) noexcept;
// island_browser-<version>-<target><suffix>; "" for unknown targets.
[[nodiscard]] std::string ArchiveName(std::string_view version, std::string_view target);

struct SelectedAssets {
    ReleaseAsset archive;
    ReleaseAsset checksums;
};
// The release's archive for |target| plus its SHA256SUMS.txt; std::nullopt
// when either is missing or a download URL is not an allowed update URL.
[[nodiscard]] std::optional<SelectedAssets> SelectAssets(const Release& release,
                                                         std::string_view target);

struct ChecksumEntry {
    std::string sha256;  // 64 lowercase hex characters
    std::string filename;

    bool operator==(const ChecksumEntry&) const = default;
};
// Parses `sha256sum` output: "<64 hex>  <name>" or "<64 hex> *<name>" per
// line. Malformed lines are skipped; hex is normalized to lowercase.
[[nodiscard]] std::vector<ChecksumEntry> ParseSha256Sums(std::string_view text);
// The checksum listed for |filename|, or std::nullopt (also when the file is
// listed twice with different sums).
[[nodiscard]] std::optional<std::string> FindChecksum(std::string_view sums_text,
                                                      std::string_view filename);

// Only https URLs on github.com, api.github.com, or *.githubusercontent.com
// (default port, no credentials) are fetched.
[[nodiscard]] bool IsAllowedUpdateUrl(std::string_view url);
// Resolves a redirect Location against the URL that returned it (absolute,
// or host-relative "/path"); std::nullopt unless the result is allowed.
[[nodiscard]] std::optional<std::string> ResolveRedirect(std::string_view from,
                                                         std::string_view location);

// ---------------------------------------------------------------------------
// Install location
// ---------------------------------------------------------------------------

enum class InstallSupport : std::uint8_t {
    kSupported,
    kUnsupportedPlatform,  // no release target for this host
    kBuildTree,            // a CMake build directory, not a packaged install
    kUnrecognizedLayout,   // not a packaged Island install
    kNotWritable,          // the install or its parent directory is read-only
};

struct InstallLocation {
    InstallSupport support = InstallSupport::kUnsupportedPlatform;
    Platform platform = Platform::kLinux;
    // The directory the update replaces: the .app bundle on macOS, the
    // directory holding island_browser(.exe) elsewhere.
    std::filesystem::path install_root;
    // The executable's file name (island_browser, island_browser.exe).
    std::string executable_name;
    // Sibling of install_root, so the swap is a same-volume rename.
    std::filesystem::path staging_dir;
};

// Classifies the install that |executable| belongs to. Probes the file system
// (ancestor CMakeCache.txt, bundle/package markers, writability) but changes
// nothing permanently.
[[nodiscard]] InstallLocation DetectInstallLocation(Platform platform,
                                                    const std::filesystem::path& executable);
// The user-facing reason updates cannot be installed; "" for kSupported.
[[nodiscard]] std::string_view InstallSupportMessage(InstallSupport support) noexcept;

// ---------------------------------------------------------------------------
// Apply script
// ---------------------------------------------------------------------------

struct InstallPlan {
    Platform platform = Platform::kLinux;
    std::int64_t pid = 0;           // the browser process the script waits for
    std::string version;            // the version being installed
    std::filesystem::path archive;  // verified archive inside staging_dir
    std::filesystem::path staging_dir;
    std::filesystem::path install_root;
    std::string executable_name;

    [[nodiscard]] std::filesystem::path extract_dir() const { return staging_dir / "extracted"; }
    [[nodiscard]] std::filesystem::path backup_path() const {
        return staging_dir / "previous" / install_root.filename();
    }
    [[nodiscard]] std::filesystem::path result_file() const { return staging_dir / "result.txt"; }
    [[nodiscard]] std::filesystem::path log_file() const { return staging_dir / "update.log"; }
    [[nodiscard]] std::filesystem::path script_path() const {
        return staging_dir /
               (platform == Platform::kWindows ? "apply_update.cmd" : "apply_update.sh");
    }
};

// POSIX sh single-quoting: 'it'\''s'.
[[nodiscard]] std::string ShellQuote(std::string_view text);
// The text for a cmd.exe `set "NAME=<value>"` line (with delayed expansion
// off): '%' doubled. std::nullopt for characters cmd cannot carry safely
// there (", CR, LF, NUL).
[[nodiscard]] std::optional<std::string> CmdSetValue(std::string_view text);
// The apply script for |plan| (sh on macOS/Linux, cmd on Windows), or
// std::nullopt when a path cannot be embedded safely.
[[nodiscard]] std::optional<std::string> GenerateInstallScript(const InstallPlan& plan);
// UTF-8 text of a path (std::filesystem::path::string() is lossy on Windows).
[[nodiscard]] std::string PathUtf8(const std::filesystem::path& path);
[[nodiscard]] std::filesystem::path PathFromUtf8(std::string_view text);

// Starts `sh <script>` (cmd.exe /c on Windows) detached from the browser:
// its own session, no inherited stdio. False when the launch failed.
[[nodiscard]] bool LaunchDetachedScript(Platform platform, const std::filesystem::path& script);
[[nodiscard]] std::int64_t CurrentProcessId() noexcept;

// The outcome the apply script records ("ok <version>" / "failed <reason>").
struct InstallOutcome {
    bool ok = false;
    std::string detail;  // the installed version, or the failure reason

    bool operator==(const InstallOutcome&) const = default;
};
[[nodiscard]] std::optional<InstallOutcome> ParseInstallOutcome(std::string_view text);

// ---------------------------------------------------------------------------
// Download sink
// ---------------------------------------------------------------------------

// Streams a download to a file (or memory, with an empty path) while hashing
// it, enforcing a byte cap. Append returns false once the cap is exceeded or
// a write fails; Discard removes the partial file.
class DownloadSink final {
  public:
    DownloadSink() = default;
    ~DownloadSink();
    DownloadSink(const DownloadSink&) = delete;
    DownloadSink& operator=(const DownloadSink&) = delete;

    [[nodiscard]] bool Open(const std::filesystem::path& file, std::int64_t max_bytes);
    [[nodiscard]] bool Append(const void* data, std::size_t size);
    // Flushes and closes the file; false on a write error. Returns the hash.
    [[nodiscard]] bool Close(std::string* sha256_hex);
    void Discard();

    [[nodiscard]] std::int64_t size() const noexcept { return size_; }
    [[nodiscard]] const std::string& body() const noexcept { return body_; }
    [[nodiscard]] std::string TakeBody() { return std::move(body_); }
    [[nodiscard]] bool overflowed() const noexcept { return overflowed_; }

  private:
    bool open_ = false;
    std::filesystem::path path_;
    std::FILE* file_ = nullptr;
    Sha256 hasher_;
    std::string body_;
    std::int64_t size_ = 0;
    std::int64_t max_bytes_ = 0;
    bool overflowed_ = false;
    bool failed_ = false;
};

// ---------------------------------------------------------------------------
// Fetcher seam
// ---------------------------------------------------------------------------

struct FetchRequest {
    std::string url;
    std::string accept;
    std::int64_t max_bytes = 0;
    // Empty: keep the body in memory (FetchResult::body). Otherwise stream it
    // to this file.
    std::filesystem::path destination;
};

struct FetchResult {
    bool ok = false;  // 2xx response fully received within the cap
    int http_status = 0;
    std::string body;        // in-memory requests only
    std::string sha256_hex;  // of the received body
    std::int64_t size = 0;
    std::string error;  // user-facing reason when !ok
};

class UpdateFetcher {
  public:
    using Progress = std::function<void(std::int64_t received, std::int64_t total)>;
    using Done = std::function<void(FetchResult)>;

    virtual ~UpdateFetcher() = default;
    // One request at a time. |done| runs exactly once unless Cancel() runs
    // first; callbacks arrive on the caller's (UI) thread.
    virtual void Start(FetchRequest request, Progress progress, Done done) = 0;
    virtual void Cancel() = 0;
};

// ---------------------------------------------------------------------------
// Check policy
// ---------------------------------------------------------------------------

// True when ISLAND_DISABLE_UPDATES is set to anything but "" or "0".
[[nodiscard]] bool UpdatesDisabledByEnvironment();
// Whether the automatic (startup) check is due: enabled, and the last check
// is at least kAutoCheckIntervalSeconds old (or in the future, after a clock
// change). Unix seconds.
[[nodiscard]] bool AutoCheckDue(bool auto_check_enabled, std::int64_t last_check_unix,
                                std::int64_t now_unix) noexcept;

// ---------------------------------------------------------------------------
// Updater
// ---------------------------------------------------------------------------

enum class UpdateStatus : std::uint8_t {
    kIdle,
    kChecking,
    kUpToDate,
    // GitHub answered 403/404 (e.g. the repository is private or rate
    // limited): no update information, which is not an error.
    kUnavailable,
    kAvailable,
    kDownloading,
    kReady,
    kError,
};

[[nodiscard]] std::string_view UpdateStatusName(UpdateStatus status) noexcept;

struct UpdateSnapshot {
    UpdateStatus status = UpdateStatus::kIdle;
    std::string current_version;
    std::string latest_version;  // set from kAvailable on
    std::string release_name;
    std::string release_url;
    int progress_percent = -1;  // kDownloading; -1 when the size is unknown
    std::string error;          // kError
    // Why "Download and install" is unavailable here ("" when it is).
    std::string install_blocked_reason;
    // The outcome of the previous update, reported once after a relaunch.
    std::string notice;
};

class Updater final {
  public:
    struct Config {
        std::string current_version;
        std::string target;  // "" on unsupported hosts
        InstallLocation install;
        std::string releases_url = ReleasesApiUrl();
    };

    Updater(Config config, std::unique_ptr<UpdateFetcher> fetcher, std::function<void()> on_change);
    ~Updater();
    Updater(const Updater&) = delete;
    Updater& operator=(const Updater&) = delete;

    // Starts a release check; ignored while a check or download runs. False
    // when nothing started.
    bool Check(bool include_prereleases);
    // Downloads and verifies the available update; false unless an update is
    // available and this install can be updated.
    bool StartDownload();
    // kReady only: re-verifies the archive, writes the apply script, and
    // returns its path for LaunchDetachedScript. Sets kError on failure.
    [[nodiscard]] std::optional<std::filesystem::path> PrepareInstall(std::int64_t pid);
    // Reads the previous update's outcome (if any), reports it once through
    // snapshot().notice, and removes the staging directory with its backup.
    std::optional<InstallOutcome> FinalizePreviousInstall();
    // Stops any in-flight request without further callbacks.
    void Cancel();

    [[nodiscard]] const UpdateSnapshot& snapshot() const noexcept { return snapshot_; }
    [[nodiscard]] const Config& config() const noexcept { return config_; }
    // The snapshot as the Settings page's `settings.update` object (without
    // the preference toggles, which the window adds).
    [[nodiscard]] json::Value StateJson() const;
    void ClearNotice() { snapshot_.notice.clear(); }

  private:
    void SetError(std::string message);
    void Changed();
    void OnReleasesFetched(FetchResult result, bool include_prereleases);
    void OnChecksumsFetched(FetchResult result);
    void OnArchiveProgress(std::int64_t received, std::int64_t total);
    void OnArchiveFetched(FetchResult result, std::string expected_sha256);
    [[nodiscard]] std::filesystem::path ArchivePath() const;
    void RefreshBlockedReason();

    Config config_;
    std::unique_ptr<UpdateFetcher> fetcher_;
    std::function<void()> on_change_;
    UpdateSnapshot snapshot_;
    std::optional<SemVer> current_;
    std::optional<Release> release_;
    std::optional<SelectedAssets> assets_;
    std::string verified_sha256_;
};

}  // namespace island::update

#endif  // ISLAND_UPDATER_H_
