#include "report_store.h"

#include <algorithm>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include <optional>
#include <sstream>
#include <system_error>

#include "json_util.h"

namespace island {
namespace {

ReportLoadResult Fail(ReportError error) { return {error, ReportState{}}; }

// Clips `text` to at most `limit` bytes without splitting a UTF-8 sequence.
std::string ClipUtf8(std::string text, std::size_t limit) {
    if (text.size() <= limit) {
        return text;
    }
    std::size_t end = limit;
    while (end > 0 && (static_cast<unsigned char>(text[end]) & 0xC0U) == 0x80U) {
        --end;
    }
    text.resize(end);
    return text;
}

ReportLoadResult ParseState(const json::Value& root) {
    if (!root.IsObject() || !root.HasRequiredInt("version") ||
        root.FindMember("version")->int_val != ReportState::kCurrentSchemaVersion ||
        !root.HasRequiredArray("reports")) {
        return Fail(ReportError::kSchemaError);
    }
    ReportState state;
    for (const json::Value& value : root.FindMember("reports")->array_val) {
        if (!value.IsObject() || !value.HasRequiredString("id") ||
            !value.HasRequiredString("title") || !value.HasRequiredString("question") ||
            !value.HasRequiredString("markdown") || !value.HasRequiredInt("created_ms")) {
            return Fail(ReportError::kSchemaError);
        }
        ResearchReport report;
        report.id = value.FindMember("id")->string_val;
        if (!ReportStore::IsValidId(report.id)) {
            return Fail(ReportError::kSchemaError);
        }
        report.title = value.FindMember("title")->string_val;
        report.question = value.FindMember("question")->string_val;
        report.markdown = value.FindMember("markdown")->string_val;
        report.created_ms = value.FindMember("created_ms")->int_val;
        state.reports.push_back(std::move(report));
    }
    return {ReportError::kNone, std::move(state)};
}

std::string Serialize(const ReportState& state) {
    std::ostringstream oss;
    json::Writer w(oss);
    w.StartObject();
    w.Key("version");
    w.UintValue(static_cast<std::uint32_t>(state.version));
    w.Key("reports");
    w.StartArray();
    for (const ResearchReport& report : state.reports) {
        w.ArrayElement();
        w.StartObject();
        w.Key("id");
        w.StringValue(report.id);
        w.Key("title");
        w.StringValue(report.title);
        w.Key("question");
        w.StringValue(report.question);
        w.Key("markdown");
        w.StringValue(report.markdown);
        w.Key("created_ms");
        w.IntValue(report.created_ms);
        w.EndObject();
    }
    w.EndArray();
    w.EndObject();
    oss << '\n';
    return oss.str();
}

}  // namespace

ReportLoadResult ReportStore::Load(const std::filesystem::path& path) {
    std::ifstream in(path, std::ios::binary);
    if (!in.is_open()) {
        return Fail(ReportError::kFileReadError);
    }
    std::string content;
    content.assign(std::istreambuf_iterator<char>(in), std::istreambuf_iterator<char>());
    if (in.bad()) {
        return Fail(ReportError::kFileReadError);
    }
    std::optional<json::Value> root = json::Parser(content).Parse();
    if (!root.has_value()) {
        return Fail(ReportError::kParseError);
    }
    return ParseState(*root);
}

ReportError ReportStore::Save(const std::filesystem::path& path, const ReportState& state) {
    const std::string text = Serialize(state);
    std::filesystem::path tmp_path = path;
    tmp_path += ".tmp";

    std::error_code ec;
    std::filesystem::create_directories(path.parent_path(), ec);
    if (ec) {
        return ReportError::kFileWriteError;
    }
    {
        std::ofstream out(tmp_path, std::ios::binary | std::ios::trunc);
        if (!out.is_open()) {
            return ReportError::kFileWriteError;
        }
        out << text;
        if (!out) {
            return ReportError::kFileWriteError;
        }
    }
    std::filesystem::rename(tmp_path, path, ec);
    if (ec) {
        std::filesystem::remove(tmp_path, ec);
        return ReportError::kFileWriteError;
    }
    return ReportError::kNone;
}

bool ReportStore::IsValidId(std::string_view id) noexcept {
    return id.size() >= 2 && id.size() <= 21 && id.front() == 'r' &&
           std::all_of(id.begin() + 1, id.end(), [](char c) { return c >= '0' && c <= '9'; });
}

std::string ReportStore::MakeId(const ReportState& state, std::int64_t created_ms) {
    std::int64_t stamp = std::max<std::int64_t>(created_ms, 0);
    while (Find(state, "r" + std::to_string(stamp)) != nullptr) {
        ++stamp;
    }
    return "r" + std::to_string(stamp);
}

std::string ReportStore::Add(ReportState& state, ResearchReport report) {
    if (!IsValidId(report.id) || Find(state, report.id) != nullptr) {
        report.id = MakeId(state, report.created_ms);
    }
    report.markdown = ClipUtf8(std::move(report.markdown), ReportState::kMaxMarkdownBytes);
    std::string id = report.id;
    state.reports.insert(state.reports.begin(), std::move(report));
    if (state.reports.size() > ReportState::kMaxReports) {
        state.reports.resize(ReportState::kMaxReports);
    }
    return id;
}

const ResearchReport* ReportStore::Find(const ReportState& state, std::string_view id) {
    for (const ResearchReport& report : state.reports) {
        if (report.id == id) {
            return &report;
        }
    }
    return nullptr;
}

bool ReportStore::Remove(ReportState& state, std::string_view id) {
    const auto it = std::find_if(state.reports.begin(), state.reports.end(),
                                 [&](const ResearchReport& r) { return r.id == id; });
    if (it == state.reports.end()) {
        return false;
    }
    state.reports.erase(it);
    return true;
}

std::filesystem::path ReportStore::DefaultReportsFilePath() {
#if defined(_WIN32)
    const char* const app_data = std::getenv("APPDATA");
    std::filesystem::path base =
        app_data != nullptr && *app_data != '\0'
            ? std::filesystem::path(app_data)
            : std::filesystem::path(std::getenv("USERPROFILE")) / "AppData" / "Roaming";
    return base / "Island" / "reports.json";
#elif defined(__APPLE__)
    const char* const home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') {
        return std::filesystem::path("reports.json");
    }
    return std::filesystem::path(home) / "Library" / "Application Support" / "Island" /
           "reports.json";
#else
    const char* const xdg_data_home = std::getenv("XDG_DATA_HOME");
    if (xdg_data_home != nullptr && *xdg_data_home != '\0') {
        return std::filesystem::path(xdg_data_home) / "Island" / "reports.json";
    }
    const char* const home = std::getenv("HOME");
    if (home == nullptr || *home == '\0') {
        return std::filesystem::path("reports.json");
    }
    return std::filesystem::path(home) / ".local" / "share" / "Island" / "reports.json";
#endif
}

}  // namespace island
