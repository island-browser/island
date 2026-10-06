#ifndef ISLAND_AGENT_PANEL_H_
#define ISLAND_AGENT_PANEL_H_

// The sidebar agent panel's view: a CefBrowserView showing the embedded
// agent_panel.html page. Native -> page goes through ExecuteJavaScript
// (window.islandRender(state)); page -> native goes through console messages
// carrying a fixed prefix, which only this browser's client listens to.
// The page can never navigate away; links are handed to the delegate.

#include <optional>
#include <string>
#include <string_view>

#include "include/views/cef_browser_view.h"
#include "json_util.h"

namespace island {

class AgentPanelDelegate {
  public:
    virtual ~AgentPanelDelegate() = default;
    // One decoded {"type": ...} message from the page (UI thread).
    virtual void OnAgentPanelMessage(const json::Value& message) = 0;
};

class AgentPanel final {
  public:
    explicit AgentPanel(AgentPanelDelegate& delegate);
    ~AgentPanel();

    AgentPanel(const AgentPanel&) = delete;
    AgentPanel& operator=(const AgentPanel&) = delete;

    [[nodiscard]] CefRefPtr<CefBrowserView> view() const { return view_; }
    // Sends a state update ({"session":..., "theme":..., ...}) to the page.
    // Updates sent before the page loaded are replayed once it is ready.
    void Render(const std::string& state_json);
    void FocusInput();
    // Stops delivering messages and closes the panel's browser.
    void Close();

    // The data: URL the panel browser loads.
    [[nodiscard]] static std::string PageUrl();
    // Decodes a console line from the page; std::nullopt for anything that
    // is not a well-formed panel message.
    [[nodiscard]] static std::optional<json::Value> DecodeMessage(std::string_view console_text);
    // Wraps a state object as the JavaScript call that applies it.
    [[nodiscard]] static std::string RenderScript(std::string_view state_json);

    static constexpr std::string_view kMessagePrefix = "\x01island-agent:";

    class Client;

  private:
    CefRefPtr<Client> client_;
    CefRefPtr<CefBrowserView> view_;
};

}  // namespace island

#endif  // ISLAND_AGENT_PANEL_H_
