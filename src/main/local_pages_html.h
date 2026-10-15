#ifndef ISLAND_LOCAL_PAGES_HTML_H_
#define ISLAND_LOCAL_PAGES_HTML_H_

#include <string_view>

#include "local_page.h"

namespace island {

// The built-in pages under src/main/pages/, embedded at configure time by the
// root CMakeLists.txt into generated local_page_html_*.cc files.
[[nodiscard]] std::string_view AgentPageHtml() noexcept;
[[nodiscard]] std::string_view SettingsPageHtml() noexcept;
[[nodiscard]] std::string_view TabOverviewPageHtml() noexcept;
[[nodiscard]] std::string_view StartPageHtml() noexcept;
// Reader view assets under src/main/reader/: the document template and the extraction script
// the window injects into the page being read.
[[nodiscard]] std::string_view ReaderPageHtml() noexcept;
[[nodiscard]] std::string_view ReaderExtractScript() noexcept;
[[nodiscard]] std::string_view ReportPageHtml() noexcept;

[[nodiscard]] inline std::string_view LocalPageHtml(LocalPageKind kind) noexcept {
    switch (kind) {
        case LocalPageKind::kAgent:
            return AgentPageHtml();
        case LocalPageKind::kSettings:
            return SettingsPageHtml();
        case LocalPageKind::kTabOverview:
            return TabOverviewPageHtml();
        case LocalPageKind::kStart:
            return StartPageHtml();
        case LocalPageKind::kReport:
            return ReportPageHtml();
    }
    return AgentPageHtml();
}

}  // namespace island

#endif  // ISLAND_LOCAL_PAGES_HTML_H_
