#include "app_runtime.h"

#include "font_registry.h"
#include "island_app.h"
#include "lifecycle_config.h"

namespace island {

int RunIslandMainProcess(const CefMainArgs& main_args, CefRefPtr<IslandApp>&& app,
                         IslandAppReleaseObserver* release_observer, void* sandbox_info) {
    FontRegistry font_registry = FontRegistry::ForCurrentProcess();
    font_registry.Register();

    CefSettings settings;
    settings.remote_debugging_port = kRemoteDebuggingPort;
    // Test-friendly isolated cache root: never touch the OS keychain via the
    // Chromium Safe Storage helper. Pointing root_cache_path at a dedicated
    // per-process directory keeps CEF state out of the user's profile and stops
    // the "wants to use your confidential information stored in Chromium Safe
    // Storage" prompt from firing during tests and smoke runs.
    CefString(&settings.root_cache_path) = "/tmp/island_cef_cache";
    // Disable session-cookie persistence so CEF never encrypts cookies to disk
    // through Chromium Safe Storage; this is the macOS keychain popup users hit
    // during tests.
    settings.persist_session_cookies = false;
#if !defined(CEF_USE_SANDBOX) && !defined(CEF_USE_BOOTSTRAP)
    settings.no_sandbox = true;
#endif

    if (!CefInitialize(main_args, settings, app, sandbox_info)) {
        font_registry.Unregister();
        return CefGetExitCode();
    }

    CefRunMessageLoop();
    if (release_observer != nullptr) {
        release_observer->OnBeforeIslandAppRelease();
    }
    app = nullptr;
    CefShutdown();
    font_registry.Unregister();
    return 0;
}

}  // namespace island
