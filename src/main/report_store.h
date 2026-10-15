#ifndef ISLAND_REPORT_STORE_H_
#define ISLAND_REPORT_STORE_H_

// The "Reports" list: finished Research-mode answers, newest first, persisted
// as one JSON file in the support directory next to bookmarks.json. CEF-free.

#include <cstddef>
#include <cstdint>
#include <filesystem>
#include <string>
#include <string_view>
#include <vector>

namespace island {

struct ResearchReport {
    // "r" followed by decimal digits: safe in a URL fragment and in JavaScript.
    std::string id;
    std::string title;
    std::string question;
    std::string markdown;
    std::int64_t created_ms = 0;  // Unix time, milliseconds

    bool operator==(const ResearchReport&) const = default;
};

struct ReportState {
    static constexpr int kCurrentSchemaVersion = 1;
    // Oldest reports beyond this many are dropped on Add().
    static constexpr std::size_t kMaxReports = 100;
    // A single report is clipped to this many bytes (on a UTF-8 boundary).
    static constexpr std::size_t kMaxMarkdownBytes = 400 * 1024;

    int version = kCurrentSchemaVersion;
    std::vector<ResearchReport> reports;  // newest first

    bool operator==(const ReportState&) const = default;
};

enum class ReportError : std::uint8_t {
    kNone = 0,
    kFileReadError,
    kFileWriteError,
    kParseError,
    kSchemaError,
};

struct ReportLoadResult {
    ReportError error = ReportError::kNone;
    // Empty unless error == kNone.
    ReportState state;
};

class ReportStore {
  public:
    // A missing, unreadable, or corrupt file behaves like BookmarkStore: an
    // empty state plus a non-kNone error; callers log and continue.
    static ReportLoadResult Load(const std::filesystem::path& path);
    static ReportError Save(const std::filesystem::path& path, const ReportState& state);

    // True for the ids MakeId() produces.
    [[nodiscard]] static bool IsValidId(std::string_view id) noexcept;
    // A fresh id for a report created at `created_ms`, unique within `state`.
    [[nodiscard]] static std::string MakeId(const ReportState& state, std::int64_t created_ms);

    // Inserts `report` first (assigning an id when it has none, clipping an
    // oversized body, dropping the oldest past kMaxReports) and returns its id.
    static std::string Add(ReportState& state, ResearchReport report);
    [[nodiscard]] static const ResearchReport* Find(const ReportState& state, std::string_view id);
    static bool Remove(ReportState& state, std::string_view id);

    [[nodiscard]] static std::filesystem::path DefaultReportsFilePath();
};

}  // namespace island

#endif  // ISLAND_REPORT_STORE_H_
