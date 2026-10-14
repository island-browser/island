#ifndef ISLAND_MAIN_DOWNLOAD_PATH_H_
#define ISLAND_MAIN_DOWNLOAD_PATH_H_

// CEF-free download destination policy. Island saves downloads straight into
// the user's Downloads folder without a dialog (Chrome's default). CEF does
// not de-duplicate names, so a second "report.pdf" would overwrite the first;
// this picks "report (1).pdf", "report (2).pdf", ... instead.

#include <filesystem>
#include <string>
#include <string_view>

namespace island {

// The user's Downloads folder: $HOME/Downloads (macOS, Linux) or
// %USERPROFILE%\Downloads (Windows). Empty when the home directory is unknown.
[[nodiscard]] std::filesystem::path DefaultDownloadsDirectory();

// Reduces a server-suggested name to a plain file name: strips every directory
// component (both separator styles), control characters and trailing dots or
// spaces, and falls back to "download" when nothing usable is left (empty,
// ".", "..", only separators).
[[nodiscard]] std::string SanitizeDownloadFileName(std::string_view suggested_name);

// Full destination path inside `downloads_dir` for `suggested_name`: the
// sanitized name, or "stem (N).ext" with the smallest N >= 1 that does not
// exist yet. Does not create the file or the directory.
[[nodiscard]] std::filesystem::path ResolveDownloadPath(const std::filesystem::path& downloads_dir,
                                                        std::string_view suggested_name);

}  // namespace island

#endif  // ISLAND_MAIN_DOWNLOAD_PATH_H_
