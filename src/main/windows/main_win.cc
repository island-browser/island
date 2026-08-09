#include <windows.h>

#include <shellapi.h>

#include <array>
#include <span>
#include <string_view>
#include <utility>

#include "../app_runtime.h"
#include "../island_app.h"
#include "../startup_options.h"
#include "include/cef_app.h"
#include "include/cef_sandbox_win.h"

namespace {

island::StartupOptions ParseStartupOptions() {
    int argument_count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (arguments == nullptr) {
        return island::StartupOptions::Parse({});
    }

    bool is_smoke_test = false;
    for (int index = 0; index < argument_count; ++index) {
        if (std::wstring_view(arguments[index]) == L"--island-smoke-test") {
            is_smoke_test = true;
            break;
        }
    }

    LocalFree(arguments);

    if (!is_smoke_test) {
        return island::StartupOptions::Parse({});
    }

    constexpr std::array<std::string_view, 1> smoke_test_arguments = {"--island-smoke-test"};
    return island::StartupOptions::Parse(smoke_test_arguments);
}

int RunMain(HINSTANCE instance, void* sandbox_info) {
    CefMainArgs main_args(instance);
    const island::StartupOptions startup_options = ParseStartupOptions();
    CefRefPtr<island::IslandApp> app(new island::IslandApp(startup_options));

    const int exit_code = CefExecuteProcess(main_args, app, sandbox_info);
    if (exit_code >= 0) {
        return exit_code;
    }

    return island::RunIslandMainProcess(main_args, std::move(app), nullptr, sandbox_info);
}

}  // namespace

#if defined(CEF_USE_BOOTSTRAP)

CEF_BOOTSTRAP_EXPORT int RunWinMain(HINSTANCE instance, LPTSTR, int, void* sandbox_info,
                                    cef_version_info_t*) {
    return RunMain(instance, sandbox_info);
}

#else

int APIENTRY wWinMain(HINSTANCE instance, HINSTANCE, LPTSTR, int) {
    void* sandbox_info = nullptr;
#if defined(CEF_USE_SANDBOX)
    CefScopedSandboxInfo scoped_sandbox;
    sandbox_info = scoped_sandbox.sandbox_info();
#endif
    return RunMain(instance, sandbox_info);
}

#endif
