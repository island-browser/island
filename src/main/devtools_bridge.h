#ifndef ISLAND_DEVTOOLS_BRIDGE_H_
#define ISLAND_DEVTOOLS_BRIDGE_H_

// Runs DevTools protocol methods against a tab's CefBrowser for the agent
// tools (page read, snapshot, click, type, screenshot). Uses
// CefBrowserHost::ExecuteDevToolsMethod plus one CefDevToolsMessageObserver
// per browser, so no remote-debugging session is involved.
//
// UI thread only. Every Call() reports exactly once: with the method's result,
// with the DevTools error, on timeout, or when the browser goes away.

#include <memory>
#include <string>
#include <string_view>

#include "browser_tools.h"
#include "include/cef_browser.h"

namespace island {

class DevToolsBridge {
  public:
    DevToolsBridge();
    ~DevToolsBridge();

    DevToolsBridge(const DevToolsBridge&) = delete;
    DevToolsBridge& operator=(const DevToolsBridge&) = delete;

    void Call(CefRefPtr<CefBrowser> browser, std::string_view method, json::Value params,
              agent::DevToolsCallback done);
    // Fails every pending call for the browser and drops its observer; call
    // from OnBeforeClose.
    void ForgetBrowser(CefRefPtr<CefBrowser> browser);
    // Fails everything and stops accepting calls.
    void Shutdown();

    // A page script that never settles (awaitPromise) must not hold a tool
    // call forever.
    static constexpr int kTimeoutMs = 30000;

    struct State;

  private:
    std::shared_ptr<State> state_;
};

}  // namespace island

#endif  // ISLAND_DEVTOOLS_BRIDGE_H_
