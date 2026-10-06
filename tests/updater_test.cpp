// CEF-free coverage of the in-browser updater core (src/main/updater.h) and
// SHA-256 (src/main/sha256.h).
#include "updater.h"

#include <gtest/gtest.h>

#include <deque>
#include <filesystem>
#include <fstream>
#include <memory>
#include <string>
#include <string_view>
#include <system_error>
#include <vector>

#include "json_util.h"
#include "sha256.h"

#if !defined(_WIN32)
#include <unistd.h>
#endif

namespace island::update {
namespace {

namespace fs = std::filesystem;

SemVer V(std::string_view text) {
    const std::optional<SemVer> parsed = ParseSemVer(text);
    EXPECT_TRUE(parsed.has_value()) << text;
    return parsed.value_or(SemVer{});
}

// ---------------------------------------------------------------------------
// SHA-256
// ---------------------------------------------------------------------------

TEST(Sha256Test, KnownVectors) {
    EXPECT_EQ(Sha256::HexOf(""),
              "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    EXPECT_EQ(Sha256::HexOf("abc"),
              "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    EXPECT_EQ(Sha256::HexOf("abcdbcdecdefdefgefghfghighijhijkijkljklmklmnlmnomnopnopq"),
              "248d6a61d20638b8e5c026930c3e6039a33ce45964ff2167f6ecedd419db06c1");
    EXPECT_EQ(Sha256::HexOf(std::string(1000000, 'a')),
              "cdc76e5c9914fb9281a1c7e284d73e67f1809a48a497200e046d39ccc7112cd0");
}

TEST(Sha256Test, StreamingMatchesOneShotAcrossBlockBoundaries) {
    std::string data;
    for (int i = 0; i < 1000; ++i) data += static_cast<char>(i * 7);
    for (const std::size_t chunk : {1U, 3U, 55U, 56U, 63U, 64U, 65U, 127U, 999U}) {
        Sha256 hasher;
        for (std::size_t offset = 0; offset < data.size(); offset += chunk) {
            hasher.Update(std::string_view(data).substr(offset, chunk));
        }
        EXPECT_EQ(hasher.FinishHex(), Sha256::HexOf(data)) << chunk;
    }
    // Finish resets the hasher.
    Sha256 hasher;
    hasher.Update("garbage");
    static_cast<void>(hasher.Finish());
    hasher.Update("abc");
    EXPECT_EQ(hasher.FinishHex(), Sha256::HexOf("abc"));
}

// ---------------------------------------------------------------------------
// SemVer
// ---------------------------------------------------------------------------

TEST(SemVerTest, ParsesStrictSemVer) {
    EXPECT_EQ(V("0.4.0").ToString(), "0.4.0");
    EXPECT_EQ(V("1.2.3-beta.1").ToString(), "1.2.3-beta.1");
    EXPECT_EQ(V("1.2.3-beta.1+build.5").ToString(), "1.2.3-beta.1");
    EXPECT_EQ(V("1.2.3+sha.abc").ToString(), "1.2.3");
    for (const std::string_view bad :
         {"", "1", "1.2", "1.2.3.4", "01.2.3", "1.02.3", "1.2.03", "1.2.3-", "1.2.3-01",
          "1.2.3-a..b", "1.2.3+", "1.2.3-a_b", "v1.2.3", "a.b.c", "1.2.-3", " 1.2.3", "1.2.3 ",
          "1.2.3+a..b", "99999999999999999999.0.0"}) {
        EXPECT_FALSE(ParseSemVer(bad).has_value()) << bad;
    }
    // A leading zero is only illegal in numeric identifiers.
    EXPECT_TRUE(ParseSemVer("1.2.3-0a").has_value());
    EXPECT_TRUE(ParseSemVer("1.2.3-0").has_value());
}

TEST(SemVerTest, PrecedenceFollowsSemVer2) {
    // The ordered example from semver.org section 11, plus core ordering.
    const std::vector<std::string_view> ordered = {
        "0.3.9",      "0.4.0-alpha",  "0.4.0-alpha.1", "0.4.0-alpha.beta",
        "0.4.0-beta", "0.4.0-beta.2", "0.4.0-beta.11", "0.4.0-rc.1",
        "0.4.0",      "0.4.1",        "0.10.0",        "1.0.0",
        "2.0.0",      "10.0.0",
    };
    for (std::size_t i = 0; i < ordered.size(); ++i) {
        for (std::size_t j = 0; j < ordered.size(); ++j) {
            const int expected = i < j ? -1 : (i == j ? 0 : 1);
            EXPECT_EQ(CompareSemVer(V(ordered[i]), V(ordered[j])), expected)
                << ordered[i] << " vs " << ordered[j];
        }
    }
    EXPECT_EQ(CompareSemVer(V("1.0.0+a"), V("1.0.0+b")), 0);
    EXPECT_LT(CompareSemVer(V("1.0.0-1"), V("1.0.0-a")), 0);
    EXPECT_LT(CompareSemVer(V("1.0.0-a.1"), V("1.0.0-a.1.0")), 0);
}

TEST(SemVerTest, ReleaseTagsNeedVPrefixAndSemVer) {
    EXPECT_EQ(ParseReleaseTag("v0.5.0"), V("0.5.0"));
    EXPECT_EQ(ParseReleaseTag("v0.5.0-beta.1"), V("0.5.0-beta.1"));
    for (const std::string_view bad : {"nightly", "0.5.0", "v", "v0.5", "V0.5.0", "vnightly"}) {
        EXPECT_FALSE(ParseReleaseTag(bad).has_value()) << bad;
    }
}

// ---------------------------------------------------------------------------
// Releases
// ---------------------------------------------------------------------------

std::string AssetJson(std::string_view name, std::int64_t size = 10) {
    return "{\"name\":\"" + std::string(name) + "\",\"size\":" + std::to_string(size) +
           ",\"browser_download_url\":\"https://github.com/island-browser/island/releases/"
           "download/x/" +
           std::string(name) + "\"}";
}

std::string ReleaseJson(std::string_view tag, bool prerelease = true, bool draft = false,
                        std::string assets = "[]") {
    return "{\"tag_name\":\"" + std::string(tag) + "\",\"name\":\"Island " + std::string(tag) +
           " (unsigned)\",\"draft\":" + (draft ? "true" : "false") +
           ",\"prerelease\":" + (prerelease ? "true" : "false") +
           ",\"html_url\":\"https://github.com/island-browser/island/releases/tag/" +
           std::string(tag) + "\",\"assets\":" + assets + "}";
}

TEST(ReleasesTest, ParseSkipsDraftsNightlyAndNonSemVerTags) {
    const std::string text =
        "[" + ReleaseJson("nightly") + "," + ReleaseJson("v0.6.0", true, true) + "," +
        ReleaseJson("v0.5.0", true, false, "[" + AssetJson("a.zip") + ",{\"name\":7}]") + "," +
        ReleaseJson("release-0.7") + "," + ReleaseJson("v0.4.1") + ",42]";
    const std::optional<std::vector<Release>> releases = ParseReleases(text);
    ASSERT_TRUE(releases.has_value());
    ASSERT_EQ(releases->size(), 2U);
    EXPECT_EQ((*releases)[0].tag, "v0.5.0");
    EXPECT_TRUE((*releases)[0].prerelease);
    ASSERT_EQ((*releases)[0].assets.size(), 1U);
    EXPECT_EQ((*releases)[0].assets[0].name, "a.zip");
    EXPECT_EQ((*releases)[0].assets[0].size, 10);
    EXPECT_EQ((*releases)[1].version, V("0.4.1"));

    EXPECT_FALSE(ParseReleases("{}").has_value());
    EXPECT_FALSE(ParseReleases("not json").has_value());
    EXPECT_FALSE(ParseReleases(std::string(kMaxReleasesJsonBytes + 1, ' ')).has_value());
    EXPECT_TRUE(ParseReleases("[]")->empty());
}

TEST(ReleasesTest, ParseIsBounded) {
    std::string text = "[";
    for (std::size_t i = 0; i < kMaxReleases + 20; ++i) {
        text += (i == 0 ? "" : ",") + ReleaseJson("v1.0." + std::to_string(i));
    }
    text += "]";
    EXPECT_EQ(ParseReleases(text)->size(), kMaxReleases);
}

std::vector<Release> Releases(std::initializer_list<std::string_view> tags) {
    std::vector<Release> releases;
    for (const std::string_view tag : tags) {
        Release release;
        release.tag = std::string(tag);
        release.version = *ParseReleaseTag(tag);
        release.prerelease = true;
        releases.push_back(release);
    }
    return releases;
}

TEST(ReleasesTest, SelectsNewestStrictlyNewerRelease) {
    const std::vector<Release> releases = Releases({"v0.4.0", "v0.5.0", "v0.4.2", "v0.3.0"});
    EXPECT_EQ(SelectUpdate(releases, V("0.4.0"), false)->tag, "v0.5.0");
    EXPECT_FALSE(SelectUpdate(releases, V("0.5.0"), false).has_value());
    EXPECT_FALSE(SelectUpdate(releases, V("0.6.0"), false).has_value());
    EXPECT_FALSE(SelectUpdate({}, V("0.4.0"), false).has_value());
}

TEST(ReleasesTest, SemVerPrereleasesNeedOptInOrAPrereleaseBuild) {
    const std::vector<Release> releases = Releases({"v0.4.1", "v0.5.0-beta.1"});
    // GitHub's prerelease flag is set on all of them; it does not matter.
    EXPECT_EQ(SelectUpdate(releases, V("0.4.0"), false)->tag, "v0.4.1");
    EXPECT_EQ(SelectUpdate(releases, V("0.4.0"), true)->tag, "v0.5.0-beta.1");
    // Already on a pre-release: newer pre-releases are offered.
    EXPECT_EQ(SelectUpdate(releases, V("0.5.0-alpha"), false)->tag, "v0.5.0-beta.1");
    // Only a pre-release is newer and the user did not opt in: nothing.
    EXPECT_FALSE(SelectUpdate(Releases({"v0.5.0-beta.1"}), V("0.4.0"), false).has_value());
    // The final release outranks its pre-releases.
    EXPECT_EQ(SelectUpdate(Releases({"v0.5.0-rc.1", "v0.5.0"}), V("0.5.0-beta.1"), false)->tag,
              "v0.5.0");
}

TEST(ReleasesTest, RepositoryConstantDrivesUrls) {
    EXPECT_EQ(ReleasesApiUrl(), "https://api.github.com/repos/island-browser/island/releases");
    EXPECT_EQ(ReleaseNotesUrl("v0.5.0"),
              "https://github.com/island-browser/island/releases/tag/v0.5.0");
}

// ---------------------------------------------------------------------------
// Targets, assets, checksums, URLs
// ---------------------------------------------------------------------------

TEST(AssetsTest, TargetNamesAndArchiveNames) {
    EXPECT_EQ(TargetName(Platform::kMac, Arch::kArm64), "macosarm64");
    EXPECT_EQ(TargetName(Platform::kMac, Arch::kX64), "macosx64");
    EXPECT_EQ(TargetName(Platform::kWindows, Arch::kX64), "windows64");
    EXPECT_EQ(TargetName(Platform::kWindows, Arch::kArm64), "windowsarm64");
    EXPECT_EQ(TargetName(Platform::kLinux, Arch::kX64), "linux64");
    EXPECT_EQ(TargetName(Platform::kLinux, Arch::kArm64), "linuxarm64");
    EXPECT_EQ(ArchiveName("0.5.0", "macosarm64"), "island_browser-0.5.0-macosarm64.zip");
    EXPECT_EQ(ArchiveName("0.5.0", "windowsarm64"), "island_browser-0.5.0-windowsarm64.zip");
    EXPECT_EQ(ArchiveName("0.5.0-beta.1", "linux64"), "island_browser-0.5.0-beta.1-linux64.tar.gz");
    EXPECT_EQ(ArchiveName("0.5.0", "solaris"), "");
    EXPECT_FALSE(CurrentTarget().empty());
}

TEST(AssetsTest, SelectsTheTargetArchiveAndChecksums) {
    std::string assets = "[";
    const std::vector<std::string_view> targets = {"macosx64",     "macosarm64", "windows64",
                                                   "windowsarm64", "linux64",    "linuxarm64"};
    for (const std::string_view target : targets) {
        assets += AssetJson(ArchiveName("0.5.0", target)) + ",";
    }
    assets += AssetJson("SHA256SUMS.txt") + "]";
    const std::vector<Release> releases =
        *ParseReleases("[" + ReleaseJson("v0.5.0", true, false, assets) + "]");
    for (const std::string_view target : targets) {
        const std::optional<SelectedAssets> selected = SelectAssets(releases[0], target);
        ASSERT_TRUE(selected.has_value()) << target;
        EXPECT_EQ(selected->archive.name, ArchiveName("0.5.0", target));
        EXPECT_EQ(selected->checksums.name, "SHA256SUMS.txt");
    }
    EXPECT_FALSE(SelectAssets(releases[0], "beos").has_value());

    // Missing checksums or archive, or a foreign download host: nothing.
    const Release no_sums = (*ParseReleases(
        "[" +
        ReleaseJson("v0.5.0", true, false, "[" + AssetJson(ArchiveName("0.5.0", "linux64")) + "]") +
        "]"))[0];
    EXPECT_FALSE(SelectAssets(no_sums, "linux64").has_value());
    Release foreign = releases[0];
    for (ReleaseAsset& asset : foreign.assets) asset.download_url = "https://evil.example/x";
    EXPECT_FALSE(SelectAssets(foreign, "linux64").has_value());
}

TEST(ChecksumsTest, ParsesSha256SumsLines) {
    const std::string a(64, 'a');
    const std::string upper(64, 'B');
    const std::string text = a + "  island_browser-0.5.0-linux64.tar.gz\n" + upper +
                             " *island_browser-0.5.0-windows64.zip\r\n"
                             "garbage line\n" +
                             std::string(63, 'c') + "  short.zip\n" + std::string(64, 'g') +
                             "  nothex.zip\n" + a + "\tbad-separator.zip\n" + a + "  ";
    const std::vector<ChecksumEntry> entries = ParseSha256Sums(text);
    ASSERT_EQ(entries.size(), 2U);
    EXPECT_EQ(entries[0], (ChecksumEntry{a, "island_browser-0.5.0-linux64.tar.gz"}));
    EXPECT_EQ(entries[1],
              (ChecksumEntry{std::string(64, 'b'), "island_browser-0.5.0-windows64.zip"}));
    EXPECT_EQ(FindChecksum(text, "island_browser-0.5.0-windows64.zip"), std::string(64, 'b'));
    EXPECT_FALSE(FindChecksum(text, "island_browser-0.5.0-macosx64.zip").has_value());
    // A file listed twice with different sums is not trusted.
    EXPECT_FALSE(FindChecksum(a + "  x.zip\n" + std::string(64, 'd') + "  x.zip\n", "x.zip"));
    EXPECT_EQ(FindChecksum(a + "  x.zip\n" + a + "  x.zip\n", "x.zip"), a);
}

TEST(UrlPolicyTest, OnlyHttpsGithubHostsAreAllowed) {
    for (const std::string_view good :
         {"https://github.com/island-browser/island/releases/download/v1/x.zip",
          "https://api.github.com/repos/island-browser/island/releases",
          "https://objects.githubusercontent.com/github-production-release-asset/1?a=b",
          "https://release-assets.githubusercontent.com/x", "HTTPS://GitHub.com/x",
          "https://github.com:443/x"}) {
        EXPECT_TRUE(IsAllowedUpdateUrl(good)) << good;
    }
    for (const std::string_view bad :
         {"http://github.com/x", "https://github.com.evil.com/x", "https://evilgithub.com/x",
          "https://githubusercontent.com/x", "https://user@github.com/x",
          "https://github.com:8443/x", "https://evil.com/github.com", "https://evil.com?github.com",
          "ftp://github.com/x", "https://", "https://github.com /x",
          "https://.githubusercontent.com/", "https://a..githubusercontent.com/",
          "https://evil.com\\@github.com/", "file:///etc/passwd"}) {
        EXPECT_FALSE(IsAllowedUpdateUrl(bad)) << bad;
    }
}

TEST(UrlPolicyTest, RedirectsStayOnAllowedHosts) {
    EXPECT_EQ(
        ResolveRedirect("https://github.com/a/b", "https://objects.githubusercontent.com/x?sig=1"),
        "https://objects.githubusercontent.com/x?sig=1");
    EXPECT_EQ(ResolveRedirect("https://github.com/a/b", "/c/d"), "https://github.com/c/d");
    EXPECT_FALSE(ResolveRedirect("https://github.com/a", "https://evil.com/x").has_value());
    EXPECT_FALSE(ResolveRedirect("https://github.com/a", "//evil.com/x").has_value());
    EXPECT_FALSE(ResolveRedirect("https://github.com/a", "relative/path").has_value());
    EXPECT_FALSE(ResolveRedirect("https://github.com/a", "http://github.com/x").has_value());
}

TEST(PolicyTest, AutoCheckRunsAtMostDaily) {
    constexpr std::int64_t now = 1'800'000'000;
    EXPECT_TRUE(AutoCheckDue(true, 0, now));
    EXPECT_TRUE(AutoCheckDue(true, now - kAutoCheckIntervalSeconds, now));
    EXPECT_FALSE(AutoCheckDue(true, now - kAutoCheckIntervalSeconds + 1, now));
    EXPECT_FALSE(AutoCheckDue(false, 0, now));
    EXPECT_TRUE(AutoCheckDue(true, now + 3600, now));  // clock moved backwards
}

TEST(PolicyTest, InstallOutcomeParsing) {
    EXPECT_EQ(ParseInstallOutcome("ok 0.5.0\n"), (InstallOutcome{true, "0.5.0"}));
    EXPECT_EQ(ParseInstallOutcome("failed Could not extract the update \r\n"),
              (InstallOutcome{false, "Could not extract the update"}));
    EXPECT_FALSE(ParseInstallOutcome("").has_value());
    EXPECT_FALSE(ParseInstallOutcome("maybe").has_value());
}

// ---------------------------------------------------------------------------
// Install location and scripts
// ---------------------------------------------------------------------------

class TempDir {
  public:
    TempDir() {
        static int counter = 0;
        path_ = fs::temp_directory_path() /
                ("island_updater_test_" + std::to_string(CurrentProcessId()) + "_" +
                 std::to_string(++counter));
        std::error_code error;
        fs::remove_all(path_, error);
        fs::create_directories(path_, error);
    }
    ~TempDir() {
        std::error_code error;
        fs::permissions(path_, fs::perms::owner_all, fs::perm_options::add, error);
        fs::remove_all(path_, error);
    }
    [[nodiscard]] const fs::path& path() const { return path_; }

  private:
    fs::path path_;
};

void Touch(const fs::path& file, std::string_view content = "x") {
    std::error_code error;
    fs::create_directories(file.parent_path(), error);
    std::ofstream(file, std::ios::binary) << content;
}

TEST(InstallLocationTest, MacBundleIsSupported) {
    TempDir temp;
    const fs::path exe =
        temp.path() / "Applications" / "Island.app" / "Contents" / "MacOS" / "island_browser";
    Touch(exe);
    const InstallLocation location = DetectInstallLocation(Platform::kMac, exe);
    EXPECT_EQ(location.support, InstallSupport::kSupported);
    EXPECT_EQ(location.install_root, temp.path() / "Applications" / "Island.app");
    EXPECT_EQ(location.executable_name, "island_browser");
    EXPECT_EQ(location.staging_dir, temp.path() / "Applications" / ".Island.app.island-update");
    EXPECT_TRUE(InstallSupportMessage(location.support).empty());
}

TEST(InstallLocationTest, LinuxPackageNeedsMetadataAndLibcef) {
    TempDir temp;
    const fs::path root = temp.path() / "opt" / "island";
    Touch(root / "island_browser");
    EXPECT_EQ(DetectInstallLocation(Platform::kLinux, root / "island_browser").support,
              InstallSupport::kUnrecognizedLayout);
    Touch(root / "build-metadata.json", "{}");
    EXPECT_EQ(DetectInstallLocation(Platform::kLinux, root / "island_browser").support,
              InstallSupport::kUnrecognizedLayout);
    Touch(root / "libcef.so");
    const InstallLocation location =
        DetectInstallLocation(Platform::kLinux, root / "island_browser");
    EXPECT_EQ(location.support, InstallSupport::kSupported);
    EXPECT_EQ(location.install_root, root);
    EXPECT_EQ(location.staging_dir, temp.path() / "opt" / ".island.island-update");
    // Windows wants libcef.dll instead.
    EXPECT_EQ(DetectInstallLocation(Platform::kWindows, root / "island_browser.exe").support,
              InstallSupport::kUnrecognizedLayout);
}

TEST(InstallLocationTest, CrowdedFoldersAreNeverSwapped) {
    TempDir temp;
    const fs::path root = temp.path() / "Downloads";
    Touch(root / "island_browser");
    Touch(root / "build-metadata.json");
    Touch(root / "libcef.so");
    for (int i = 0; i < 70; ++i) Touch(root / ("file" + std::to_string(i)));
    EXPECT_EQ(DetectInstallLocation(Platform::kLinux, root / "island_browser").support,
              InstallSupport::kUnrecognizedLayout);
}

TEST(InstallLocationTest, BuildTreesRefuseUpdates) {
    TempDir temp;
    Touch(temp.path() / "build" / "CMakeCache.txt");
    const fs::path mac_exe = temp.path() / "build" / "src" / "main" / "island_browser.app" /
                             "Contents" / "MacOS" / "island_browser";
    Touch(mac_exe);
    const InstallLocation mac = DetectInstallLocation(Platform::kMac, mac_exe);
    EXPECT_EQ(mac.support, InstallSupport::kBuildTree);
    EXPECT_EQ(InstallSupportMessage(mac.support), "Updates are managed by your build.");
    const fs::path linux_exe =
        temp.path() / "build" / "src" / "main" / "Release" / "island_browser";
    Touch(linux_exe);
    Touch(linux_exe.parent_path() / "build-metadata.json");
    Touch(linux_exe.parent_path() / "libcef.so");
    EXPECT_EQ(DetectInstallLocation(Platform::kLinux, linux_exe).support,
              InstallSupport::kBuildTree);
    EXPECT_EQ(DetectInstallLocation(Platform::kMac, fs::path()).support,
              InstallSupport::kUnrecognizedLayout);
    EXPECT_EQ(DetectInstallLocation(Platform::kMac, temp.path() / "island_browser").support,
              InstallSupport::kUnrecognizedLayout);
}

TEST(InstallLocationTest, ReadOnlyInstallsRefuseUpdates) {
#if defined(_WIN32)
    GTEST_SKIP() << "POSIX permissions only";
#else
    if (geteuid() == 0) {
        GTEST_SKIP() << "root ignores directory permissions";
    }
    TempDir temp;
    const fs::path parent = temp.path() / "ro";
    const fs::path exe = parent / "Island.app" / "Contents" / "MacOS" / "island_browser";
    Touch(exe);
    fs::permissions(parent, fs::perms::owner_read | fs::perms::owner_exec);
    EXPECT_EQ(DetectInstallLocation(Platform::kMac, exe).support, InstallSupport::kNotWritable);
    fs::permissions(parent, fs::perms::owner_all);
#endif
}

TEST(ScriptTest, ShellAndCmdQuoting) {
    EXPECT_EQ(ShellQuote("plain"), "'plain'");
    EXPECT_EQ(ShellQuote("it's"), "'it'\\''s'");
    EXPECT_EQ(ShellQuote("$(rm -rf /) `x` \"q\""), "'$(rm -rf /) `x` \"q\"'");
    EXPECT_EQ(CmdSetValue("C:\\Program Files\\Island"), "C:\\Program Files\\Island");
    EXPECT_EQ(CmdSetValue("C:\\50% & <off> ^ !x!"), "C:\\50%% & <off> ^ !x!");
    EXPECT_FALSE(CmdSetValue("C:\\a\"b").has_value());
    EXPECT_FALSE(CmdSetValue("C:\\a\nb").has_value());
}

InstallPlan Plan(Platform platform, const fs::path& base, std::string root_name) {
    InstallPlan plan;
    plan.platform = platform;
    plan.pid = 4321;
    plan.version = "0.5.0";
    plan.staging_dir = base / ("." + root_name + ".island-update");
    plan.archive = plan.staging_dir / "island_browser-0.5.0-macosarm64.zip";
    plan.install_root = base / root_name;
    plan.executable_name = platform == Platform::kWindows ? "island_browser.exe" : "island_browser";
    return plan;
}

TEST(ScriptTest, MacScriptQuotesPathsAndSwapsWithRollback) {
    const InstallPlan plan = Plan(Platform::kMac, "/Users/o'neil/My Apps", "Island \"Beta\".app");
    const std::optional<std::string> script = GenerateInstallScript(plan);
    ASSERT_TRUE(script.has_value());
    EXPECT_EQ(script->rfind("#!/bin/sh\n", 0), 0U);
    EXPECT_NE(script->find("PID=4321\n"), std::string::npos);
    EXPECT_NE(script->find("TARGET='/Users/o'\\''neil/My Apps/Island \"Beta\".app'\n"),
              std::string::npos);
    EXPECT_NE(script->find("BACKUP='/Users/o'\\''neil/My Apps/.Island \"Beta\".app.island-update/"
                           "previous/Island \"Beta\".app'\n"),
              std::string::npos);
    EXPECT_NE(script->find("ditto -x -k \"$ARCHIVE\" \"$EXTRACT\""), std::string::npos);
    EXPECT_NE(script->find("NEW_ROOT=\"$EXTRACT/island_browser.app\""), std::string::npos);
    EXPECT_NE(script->find("while kill -0 \"$PID\""), std::string::npos);
    EXPECT_NE(script->find("mv \"$TARGET\" \"$BACKUP\""), std::string::npos);
    EXPECT_NE(script->find("mv \"$BACKUP\" \"$TARGET\""), std::string::npos);
    EXPECT_NE(script->find("open \"$TARGET\""), std::string::npos);
    EXPECT_NE(script->find("record \"ok $VERSION\""), std::string::npos);
    EXPECT_EQ(script->find("tar -xzf"), std::string::npos);
    // The extraction happens before anything is moved.
    EXPECT_LT(script->find("ditto -x -k"), script->find("mv \"$TARGET\" \"$BACKUP\""));
}

TEST(ScriptTest, LinuxScriptUsesTarAndRelaunchesTheBinary) {
    const InstallPlan plan = Plan(Platform::kLinux, "/home/me/apps", "island");
    const std::optional<std::string> script = GenerateInstallScript(plan);
    ASSERT_TRUE(script.has_value());
    EXPECT_NE(script->find("tar -xzf \"$ARCHIVE\" -C \"$EXTRACT\""), std::string::npos);
    EXPECT_NE(script->find("NEW_ROOT=\"$EXTRACT\"\n"), std::string::npos);
    EXPECT_NE(script->find("nohup \"$TARGET/$EXE\""), std::string::npos);
    EXPECT_NE(script->find("EXE='island_browser'\n"), std::string::npos);
    EXPECT_EQ(script->find("ditto"), std::string::npos);
}

TEST(ScriptTest, WindowsScriptPassesPathsThroughTheEnvironment) {
    InstallPlan plan;
    plan.platform = Platform::kWindows;
    plan.pid = 99;
    plan.version = "0.5.0";
    plan.install_root = fs::path("C:\\Users\\Ann & Bob\\100% Island");
    plan.staging_dir = fs::path("C:\\Users\\Ann & Bob\\.100% Island.island-update");
    plan.archive = plan.staging_dir / "island_browser-0.5.0-windows64.zip";
    plan.executable_name = "island_browser.exe";
    const std::optional<std::string> script = GenerateInstallScript(plan);
    ASSERT_TRUE(script.has_value());
    EXPECT_EQ(script->rfind("@echo off\r\n", 0), 0U);
    EXPECT_NE(script->find("setlocal EnableExtensions DisableDelayedExpansion"), std::string::npos);
    EXPECT_NE(script->find("set \"ISLAND_PID=99\"\r\n"), std::string::npos);
    EXPECT_NE(script->find("set \"ISLAND_EXE=island_browser.exe\"\r\n"), std::string::npos);
#if defined(_WIN32)
    EXPECT_NE(script->find("set \"ISLAND_TARGET=C:\\Users\\Ann & Bob\\100%% Island\"\r\n"),
              std::string::npos);
#endif
    EXPECT_NE(script->find("Expand-Archive -LiteralPath $env:ISLAND_ARCHIVE -DestinationPath "
                           "$env:ISLAND_EXTRACT"),
              std::string::npos);
    EXPECT_NE(script->find("tasklist /FI \"PID eq %ISLAND_PID%\""), std::string::npos);
    EXPECT_NE(script->find(":restore\r\n"), std::string::npos);
    EXPECT_NE(script->find("start \"\" \"%ISLAND_TARGET%\\%ISLAND_EXE%\""), std::string::npos);
    // No path text is embedded outside the quoted set lines.
    EXPECT_EQ(script->find("Ann & Bob\" "), std::string::npos);

    plan.install_root = fs::path("C:\\bad\"quote");
    EXPECT_FALSE(GenerateInstallScript(plan).has_value());
}

TEST(ScriptTest, RefusesIncompletePlans) {
    InstallPlan plan = Plan(Platform::kLinux, "/opt", "island");
    plan.pid = 0;
    EXPECT_FALSE(GenerateInstallScript(plan).has_value());
    plan = Plan(Platform::kLinux, "/opt", "island");
    plan.version = "nightly";
    EXPECT_FALSE(GenerateInstallScript(plan).has_value());
    plan = Plan(Platform::kLinux, "/opt", "island");
    plan.executable_name = "../evil";
    EXPECT_FALSE(GenerateInstallScript(plan).has_value());
    plan = Plan(Platform::kLinux, "/opt", "isl\nand");
    EXPECT_FALSE(GenerateInstallScript(plan).has_value());
}

#if !defined(_WIN32)
// Runs the generated Linux script for real against a fake install (with a
// dead pid), exercising extraction, swap, backup, and the outcome file.
TEST(ScriptTest, LinuxScriptSwapsAFakeInstall) {
    if (std::system("command -v tar >/dev/null 2>&1") != 0) {
        GTEST_SKIP() << "tar not available";
    }
    TempDir temp;
    const fs::path base = temp.path() / "it's here";
    InstallPlan plan = Plan(Platform::kLinux, base, "island");
    plan.executable_name = "island_browser_test_exe";
    Touch(plan.install_root / plan.executable_name, "old");
    // Build the "release" archive with the new install contents.
    const fs::path payload = temp.path() / "payload";
    Touch(payload / plan.executable_name, "#!/bin/sh\nexit 0\n");
    fs::permissions(payload / plan.executable_name, fs::perms::owner_all);
    Touch(payload / "build-metadata.json", "{}");
    std::error_code error;
    fs::create_directories(plan.staging_dir, error);
    plan.archive = plan.staging_dir / "island_browser-0.5.0-linux64.tar.gz";
    const std::string tar = "tar -czf " + ShellQuote(plan.archive.string()) + " -C " +
                            ShellQuote(payload.string()) + " .";
    ASSERT_EQ(std::system(tar.c_str()), 0);
    plan.pid = 999999;  // not running
    const std::optional<std::string> script = GenerateInstallScript(plan);
    ASSERT_TRUE(script.has_value());
    // The relaunch would start the fake binary; keep it inert.
    std::ofstream(plan.script_path(), std::ios::binary) << *script;
    ASSERT_EQ(std::system(("/bin/sh " + ShellQuote(plan.script_path().string())).c_str()), 0);
    std::ifstream installed(plan.install_root / plan.executable_name);
    std::string content((std::istreambuf_iterator<char>(installed)), {});
    EXPECT_EQ(content, "#!/bin/sh\nexit 0\n");
    EXPECT_TRUE(fs::exists(plan.install_root / "build-metadata.json"));
    std::ifstream backup(plan.backup_path() / plan.executable_name);
    std::string old((std::istreambuf_iterator<char>(backup)), {});
    EXPECT_EQ(old, "old");
    std::ifstream result(plan.result_file());
    std::string outcome((std::istreambuf_iterator<char>(result)), {});
    EXPECT_EQ(ParseInstallOutcome(outcome), (InstallOutcome{true, "0.5.0"}));
    EXPECT_FALSE(fs::exists(plan.archive));
}

TEST(ScriptTest, LinuxScriptKeepsTheOldInstallWhenExtractionFails) {
    TempDir temp;
    InstallPlan plan = Plan(Platform::kLinux, temp.path(), "island");
    plan.executable_name = "island_browser_test_exe";
    Touch(plan.install_root / plan.executable_name, "old");
    plan.archive = plan.staging_dir / "island_browser-0.5.0-linux64.tar.gz";
    Touch(plan.archive, "not a tarball");
    plan.pid = 999999;
    std::ofstream(plan.script_path(), std::ios::binary) << *GenerateInstallScript(plan);
    EXPECT_NE(std::system(("/bin/sh " + ShellQuote(plan.script_path().string())).c_str()), 0);
    std::ifstream installed(plan.install_root / plan.executable_name);
    std::string content((std::istreambuf_iterator<char>(installed)), {});
    EXPECT_EQ(content, "old");
    std::ifstream result(plan.result_file());
    std::string outcome((std::istreambuf_iterator<char>(result)), {});
    EXPECT_EQ(ParseInstallOutcome(outcome),
              (InstallOutcome{false, "Could not extract the update"}));
}
#endif

// ---------------------------------------------------------------------------
// DownloadSink
// ---------------------------------------------------------------------------

TEST(DownloadSinkTest, HashesAndCapsMemoryAndFileBodies) {
    DownloadSink memory;
    ASSERT_TRUE(memory.Open({}, 5));
    EXPECT_TRUE(memory.Append("abc", 3));
    std::string hex;
    EXPECT_TRUE(memory.Close(&hex));
    EXPECT_EQ(hex, Sha256::HexOf("abc"));
    EXPECT_EQ(memory.body(), "abc");

    DownloadSink capped;
    ASSERT_TRUE(capped.Open({}, 5));
    EXPECT_TRUE(capped.Append("abc", 3));
    EXPECT_FALSE(capped.Append("def", 3));
    EXPECT_TRUE(capped.overflowed());
    EXPECT_FALSE(capped.Close(&hex));

    TempDir temp;
    const fs::path file = temp.path() / "download.part";
    {
        DownloadSink sink;
        ASSERT_TRUE(sink.Open(file, 100));
        EXPECT_TRUE(sink.Append("ab", 2));
        EXPECT_TRUE(sink.Append("c", 1));
        EXPECT_TRUE(sink.Close(&hex));
    }
    EXPECT_EQ(Sha256::HexOfFile(file), Sha256::HexOf("abc"));
    {
        DownloadSink sink;
        ASSERT_TRUE(sink.Open(file, 100));
        EXPECT_TRUE(sink.Append("zz", 2));
        sink.Discard();
    }
    EXPECT_FALSE(fs::exists(file));
    EXPECT_FALSE(Sha256::HexOfFile(file).has_value());
}

// ---------------------------------------------------------------------------
// Updater state machine
// ---------------------------------------------------------------------------

// Scripted fetcher: each Start pops the next response; file requests write the
// body to the destination like the CEF fetcher does.
class FakeFetcher final : public UpdateFetcher {
  public:
    struct Response {
        std::string url_must_contain;
        FetchResult result;
    };
    explicit FakeFetcher(std::deque<Response>* responses, std::vector<FetchRequest>* requests)
        : responses_(responses), requests_(requests) {}

    void Start(FetchRequest request, Progress progress, Done done) override {
        requests_->push_back(request);
        ASSERT_FALSE(responses_->empty()) << request.url;
        Response response = std::move(responses_->front());
        responses_->pop_front();
        EXPECT_NE(request.url.find(response.url_must_contain), std::string::npos) << request.url;
        FetchResult result = std::move(response.result);
        if (!request.destination.empty() && result.ok) {
            std::ofstream(request.destination, std::ios::binary) << result.body;
            result.sha256_hex = Sha256::HexOf(result.body);
            result.size = static_cast<std::int64_t>(result.body.size());
            result.body.clear();
        }
        if (progress) progress(result.size / 2, result.size);
        done(std::move(result));
    }
    void Cancel() override { ++cancels; }
    int cancels = 0;

  private:
    std::deque<Response>* responses_;
    std::vector<FetchRequest>* requests_;
};

FetchResult Ok(std::string body) {
    FetchResult result;
    result.ok = true;
    result.http_status = 200;
    result.size = static_cast<std::int64_t>(body.size());
    result.sha256_hex = Sha256::HexOf(body);
    result.body = std::move(body);
    return result;
}

FetchResult Status(int status) {
    FetchResult result;
    result.http_status = status;
    result.error = "HTTP " + std::to_string(status);
    return result;
}

struct UpdaterHarness {
    explicit UpdaterHarness(InstallSupport support = InstallSupport::kSupported) {
        Updater::Config config;
        config.current_version = "0.4.0";
        config.target = "linux64";
        config.install.support = support;
        config.install.platform = Platform::kLinux;
        config.install.install_root = temp.path() / "island";
        config.install.executable_name = "island_browser";
        config.install.staging_dir = temp.path() / ".island.island-update";
        updater = std::make_unique<Updater>(
            config, std::make_unique<FakeFetcher>(&responses, &requests), [this] { ++changes; });
    }
    std::string ReleasesBody(std::string_view archive_body, std::string_view tag = "v0.5.0") {
        const std::string name = ArchiveName(std::string(tag.substr(1)), "linux64");
        return "[" + ReleaseJson("nightly") + "," +
               ReleaseJson(tag, true, false,
                           "[" + AssetJson(name, static_cast<std::int64_t>(archive_body.size())) +
                               "," + AssetJson("SHA256SUMS.txt") + "]") +
               "," + ReleaseJson("v0.4.0") + "]";
    }

    TempDir temp;
    std::deque<FakeFetcher::Response> responses;
    std::vector<FetchRequest> requests;
    int changes = 0;
    std::unique_ptr<Updater> updater;
};

TEST(UpdaterTest, CheckFindsUpdateDownloadsVerifiesAndPreparesTheScript) {
    UpdaterHarness h;
    const std::string archive = "tarball bytes";
    const std::string name = ArchiveName("0.5.0", "linux64");
    h.responses.push_back(
        {"api.github.com/repos/island-browser/island/releases", Ok(h.ReleasesBody(archive))});
    ASSERT_TRUE(h.updater->Check(false));
    EXPECT_EQ(h.requests[0].accept, "application/vnd.github+json");
    EXPECT_EQ(h.updater->snapshot().status, UpdateStatus::kAvailable);
    EXPECT_EQ(h.updater->snapshot().latest_version, "0.5.0");
    EXPECT_EQ(h.updater->snapshot().release_url,
              "https://github.com/island-browser/island/releases/tag/v0.5.0");
    EXPECT_TRUE(h.updater->snapshot().install_blocked_reason.empty());

    h.responses.push_back({"SHA256SUMS.txt", Ok(Sha256::HexOf(archive) + "  " + name + "\n")});
    h.responses.push_back({name, Ok(archive)});
    ASSERT_TRUE(h.updater->StartDownload());
    EXPECT_EQ(h.updater->snapshot().status, UpdateStatus::kReady);
    EXPECT_EQ(h.requests[2].max_bytes, static_cast<std::int64_t>(archive.size()));
    const fs::path archive_path = h.temp.path() / ".island.island-update" / name;
    EXPECT_TRUE(fs::exists(archive_path));
    EXPECT_FALSE(fs::exists(fs::path(archive_path.string() + ".part")));

    const std::optional<fs::path> script = h.updater->PrepareInstall(1234);
    ASSERT_TRUE(script.has_value());
    EXPECT_EQ(script->filename(), "apply_update.sh");
    std::ifstream in(*script);
    std::string text((std::istreambuf_iterator<char>(in)), {});
    EXPECT_NE(text.find("PID=1234"), std::string::npos);

    const json::Value state = h.updater->StateJson();
    EXPECT_EQ(state.StringOr("status", ""), "ready");
    EXPECT_EQ(state.IntOr("progress", -1), 100);
    EXPECT_TRUE(state.BoolOr("can_install", false));
}

TEST(UpdaterTest, ChecksumMismatchDiscardsTheDownload) {
    UpdaterHarness h;
    const std::string name = ArchiveName("0.5.0", "linux64");
    h.responses.push_back({"releases", Ok(h.ReleasesBody("tampered"))});
    ASSERT_TRUE(h.updater->Check(false));
    h.responses.push_back({"SHA256SUMS.txt", Ok(Sha256::HexOf("original") + "  " + name + "\n")});
    h.responses.push_back({name, Ok("tampered")});
    ASSERT_TRUE(h.updater->StartDownload());
    EXPECT_EQ(h.updater->snapshot().status, UpdateStatus::kError);
    EXPECT_NE(h.updater->snapshot().error.find("checksum"), std::string::npos);
    EXPECT_FALSE(fs::exists(h.temp.path() / ".island.island-update" / name));
    EXPECT_FALSE(h.updater->PrepareInstall(1).has_value());
}

TEST(UpdaterTest, MissingChecksumEntryStopsBeforeTheArchive) {
    UpdaterHarness h;
    h.responses.push_back({"releases", Ok(h.ReleasesBody("x"))});
    ASSERT_TRUE(h.updater->Check(false));
    h.responses.push_back({"SHA256SUMS.txt", Ok(std::string(64, 'a') + "  other.zip\n")});
    ASSERT_TRUE(h.updater->StartDownload());
    EXPECT_EQ(h.updater->snapshot().status, UpdateStatus::kError);
    EXPECT_EQ(h.requests.size(), 2U);  // the archive was never requested
}

TEST(UpdaterTest, TamperedArchiveIsRefusedAtRestart) {
    UpdaterHarness h;
    const std::string archive = "good";
    const std::string name = ArchiveName("0.5.0", "linux64");
    h.responses.push_back({"releases", Ok(h.ReleasesBody(archive))});
    ASSERT_TRUE(h.updater->Check(false));
    h.responses.push_back({"SHA256SUMS.txt", Ok(Sha256::HexOf(archive) + "  " + name + "\n")});
    h.responses.push_back({name, Ok(archive)});
    ASSERT_TRUE(h.updater->StartDownload());
    ASSERT_EQ(h.updater->snapshot().status, UpdateStatus::kReady);
    std::ofstream(h.temp.path() / ".island.island-update" / name, std::ios::binary) << "evil";
    EXPECT_FALSE(h.updater->PrepareInstall(1).has_value());
    EXPECT_EQ(h.updater->snapshot().status, UpdateStatus::kError);
}

TEST(UpdaterTest, UpToDateUnavailableAndErrors) {
    UpdaterHarness h;
    h.responses.push_back(
        {"releases", Ok("[" + ReleaseJson("v0.4.0") + "," + ReleaseJson("nightly") + "]")});
    ASSERT_TRUE(h.updater->Check(false));
    EXPECT_EQ(h.updater->snapshot().status, UpdateStatus::kUpToDate);

    // A private repository answers 404: no information, not an error.
    h.responses.push_back({"releases", Status(404)});
    ASSERT_TRUE(h.updater->Check(false));
    EXPECT_EQ(h.updater->snapshot().status, UpdateStatus::kUnavailable);
    EXPECT_TRUE(h.updater->snapshot().error.empty());
    h.responses.push_back({"releases", Status(403)});
    ASSERT_TRUE(h.updater->Check(false));
    EXPECT_EQ(h.updater->snapshot().status, UpdateStatus::kUnavailable);

    h.responses.push_back({"releases", Status(500)});
    ASSERT_TRUE(h.updater->Check(false));
    EXPECT_EQ(h.updater->snapshot().status, UpdateStatus::kError);

    h.responses.push_back({"releases", Ok("{\"message\":\"nope\"}")});
    ASSERT_TRUE(h.updater->Check(false));
    EXPECT_EQ(h.updater->snapshot().status, UpdateStatus::kError);
    EXPECT_FALSE(h.updater->StartDownload());
}

TEST(UpdaterTest, PrereleasePreferenceIsPassedThrough) {
    UpdaterHarness h;
    const std::string body = "[" + ReleaseJson("v0.5.0-beta.1") + "]";
    h.responses.push_back({"releases", Ok(body)});
    ASSERT_TRUE(h.updater->Check(false));
    EXPECT_EQ(h.updater->snapshot().status, UpdateStatus::kUpToDate);
    h.responses.push_back({"releases", Ok(body)});
    ASSERT_TRUE(h.updater->Check(true));
    EXPECT_EQ(h.updater->snapshot().status, UpdateStatus::kAvailable);
    EXPECT_EQ(h.updater->snapshot().latest_version, "0.5.0-beta.1");
    // No assets for linux64 in that release: it can be seen but not installed.
    EXPECT_FALSE(h.updater->snapshot().install_blocked_reason.empty());
    EXPECT_FALSE(h.updater->StartDownload());
}

TEST(UpdaterTest, BuildTreesCanCheckButNeverInstall) {
    UpdaterHarness h(InstallSupport::kBuildTree);
    EXPECT_EQ(h.updater->snapshot().install_blocked_reason, "Updates are managed by your build.");
    h.responses.push_back({"releases", Ok(h.ReleasesBody("x"))});
    ASSERT_TRUE(h.updater->Check(false));
    EXPECT_EQ(h.updater->snapshot().status, UpdateStatus::kAvailable);
    EXPECT_FALSE(h.updater->StartDownload());
    EXPECT_FALSE(h.updater->StateJson().BoolOr("can_install", true));
}

TEST(UpdaterTest, FinalizeReportsTheOutcomeAndCleansStaging) {
    UpdaterHarness h;
    const fs::path staging = h.temp.path() / ".island.island-update";
    Touch(staging / "result.txt", "ok 0.4.0\n");
    Touch(staging / "previous" / "island" / "island_browser", "old");
    const std::optional<InstallOutcome> outcome = h.updater->FinalizePreviousInstall();
    ASSERT_TRUE(outcome.has_value());
    EXPECT_TRUE(outcome->ok);
    EXPECT_EQ(h.updater->snapshot().notice, "Island was updated to 0.4.0.");
    EXPECT_FALSE(fs::exists(staging));
    EXPECT_FALSE(h.updater->FinalizePreviousInstall().has_value());
}
}  // namespace
}  // namespace island::update
