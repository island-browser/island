#include <windows.h>

#include <shellapi.h>

#include <array>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>
#include <utility>

#include "../app_runtime.h"
#include "../dev_capture.h"
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

// Only the developer flags the dev-capture parser knows are ASCII, so a lossy narrowing of the
// other arguments is harmless.
std::optional<int> ParseRemoteDebuggingPort() {
    int argument_count = 0;
    wchar_t** arguments = CommandLineToArgvW(GetCommandLineW(), &argument_count);
    if (arguments == nullptr) {
        return std::nullopt;
    }
    std::vector<std::string> narrow;
    for (int index = 0; index < argument_count; ++index) {
        std::string text;
        for (const wchar_t* c = arguments[index]; *c != L'\0'; ++c) {
            text.push_back(*c < 0x80 ? static_cast<char>(*c) : '?');
        }
        narrow.push_back(std::move(text));
    }
    LocalFree(arguments);
    std::vector<std::string_view> views(narrow.begin(), narrow.end());
    return island::DevCaptureOptions::Parse(views).remote_debugging_port;
}

int RunMain(HINSTANCE instance, void* sandbox_info) {
    CefMainArgs main_args(instance);
    const island::StartupOptions startup_options = ParseStartupOptions();
    const std::optional<int> remote_debugging_port = ParseRemoteDebuggingPort();
    CefRefPtr<island::IslandApp> app(new island::IslandApp(startup_options, remote_debugging_port));

    const int exit_code = CefExecuteProcess(main_args, app, sandbox_info);
    if (exit_code >= 0) {
        return exit_code;
    }

    return island::RunIslandMainProcess(main_args, std::move(app), nullptr, sandbox_info,
                                        remote_debugging_port);
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
