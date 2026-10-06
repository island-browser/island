#ifndef ISLAND_LOCAL_PAGE_H_
#define ISLAND_LOCAL_PAGE_H_

// Island's built-in pages (the agent panel, Settings, and the all-tabs
// overview): each is an embedded HTML document shown in its own
// CefBrowserView. Native -> page goes through ExecuteJavaScript
// (window.islandRender(state)); page -> native goes through console messages
// carrying a fixed prefix, which only these pages' client listens to. The
// pages never navigate away; links are handed to the delegate as "open_url".

#include <cstdint>
#include <optional>
#include <string>
#include <string_view>

#include "include/views/cef_browser_view.h"
#include "json_util.h"

namespace island {

enum class LocalPageKind : std::uint8_t {
    kAgent,
    kSettings,
    kTabOverview,
};

class LocalPageDelegate {
  public:
    virtual ~LocalPageDelegate() = default;
    // One decoded {"type": ...} message from a page (UI thread).
    virtual void OnLocalPageMessage(LocalPageKind kind, const json::Value& message) = 0;
};

class LocalPage final {
  public:
    LocalPage(LocalPageKind kind, LocalPageDelegate& delegate);
    ~LocalPage();

    LocalPage(const LocalPage&) = delete;
    LocalPage& operator=(const LocalPage&) = delete;

    [[nodiscard]] LocalPageKind kind() const noexcept { return kind_; }
    [[nodiscard]] CefRefPtr<CefBrowserView> view() const { return view_; }
    // Sends a state update to the page. Updates sent before the page loaded
    // are replayed once it is ready.
    void Render(const std::string& state_json);
    // Focuses the page and asks it to focus its primary input.
    void Focus();
    // Stops delivering messages and closes the page's browser.
    void Close();

    // The data: URL a page's browser loads.
    [[nodiscard]] static std::string PageUrl(LocalPageKind kind);
    // Decodes a console line from a page; std::nullopt for anything that is
    // not a well-formed page message.
    [[nodiscard]] static std::optional<json::Value> DecodeMessage(std::string_view console_text);
    // Wraps a state object as the JavaScript call that applies it.
    [[nodiscard]] static std::string RenderScript(std::string_view state_json);

    static constexpr std::string_view kMessagePrefix = "\x01island:";

    class Client;

  private:
    LocalPageKind kind_;
    CefRefPtr<Client> client_;
    CefRefPtr<CefBrowserView> view_;
};

}  // namespace island

#endif  // ISLAND_LOCAL_PAGE_H_
