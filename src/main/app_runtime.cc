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
