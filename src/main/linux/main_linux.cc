#include "include/base/cef_build.h"

#if defined(CEF_X11)
#include <X11/Xlib.h>
#endif

#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "app_runtime.h"
#include "include/base/cef_logging.h"
#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "island_app.h"
#include "startup_options.h"

namespace {

#if defined(CEF_X11)
int IslandXErrorHandler(Display* display, XErrorEvent* event) {
    LOG(WARNING) << "X error received: type " << event->type << ", serial " << event->serial
                 << ", error code " << static_cast<int>(event->error_code) << ", request code "
                 << static_cast<int>(event->request_code) << ", minor code "
                 << static_cast<int>(event->minor_code);
    return 0;
}

int IslandXIoErrorHandler(Display* display) { return 0; }
#endif

}  // namespace

NO_STACK_PROTECTOR
int main(int argc, char* argv[]) {
    CefMainArgs main_args(argc, argv);
    const int exit_code = CefExecuteProcess(main_args, nullptr, nullptr);
    if (exit_code >= 0) {
        return exit_code;
    }

#if defined(CEF_X11)
    XSetErrorHandler(IslandXErrorHandler);
    XSetIOErrorHandler(IslandXIoErrorHandler);
#endif

    std::vector<std::string_view> arguments;
    for (int index = 0; index < argc; ++index) {
        arguments.emplace_back(argv[index]);
    }
    const island::StartupOptions startup_options =
        island::StartupOptions::Parse(std::span<const std::string_view>(arguments));

    CefRefPtr<island::IslandApp> app(new island::IslandApp(startup_options));
    return island::RunIslandMainProcess(main_args, std::move(app), nullptr, nullptr);
}
