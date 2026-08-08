#import <Cocoa/Cocoa.h>

#include <span>
#include <string_view>
#include <utility>
#include <vector>

#include "app_runtime.h"
#include "browser_command.h"
#include "include/base/cef_logging.h"
#include "include/cef_application_mac.h"
#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_library_loader.h"
#include "island_app.h"
#include "startup_options.h"

@interface IslandApplication : NSApplication <CefAppProtocol> {
  @private
    BOOL handling_send_event_;
}

- (BOOL)isHandlingSendEvent;
- (void)setHandlingSendEvent:(BOOL)handling_send_event;
@end

@implementation IslandApplication
- (BOOL)isHandlingSendEvent {
    return handling_send_event_;
}

- (void)setHandlingSendEvent:(BOOL)handling_send_event {
    handling_send_event_ = handling_send_event;
}

- (void)sendEvent:(NSEvent*)event {
    CefScopedSendingEvent sending_event;
    [super sendEvent:event];
}

- (void)terminate:(id)sender {
    NSArray<NSWindow*>* windows = [NSApp windows];
    if ([windows count] == 0) {
        CefQuitMessageLoop();
        return;
    }

    for (NSWindow* window in windows) {
        [window performClose:sender];
    }
}
@end

@interface IslandMenuActions : NSObject {
  @private
    island::IslandApp* app_;
}

- (instancetype)initWithApp:(island::IslandApp*)app;
- (void)invalidate;
- (void)goBack:(id)sender;
- (void)goForward:(id)sender;
- (void)reload:(id)sender;
@end

@implementation IslandMenuActions
- (instancetype)initWithApp:(island::IslandApp*)app {
    self = [super init];
    if (self != nil) {
        app_ = app;
    }
    return self;
}

- (void)invalidate {
    app_ = nullptr;
}

- (void)goBack:(id)sender {
    if (app_ != nullptr) {
        app_->ExecuteCommand(island::BrowserCommand::kBack);
    }
}

- (void)goForward:(id)sender {
    if (app_ != nullptr) {
        app_->ExecuteCommand(island::BrowserCommand::kForward);
    }
}

- (void)reload:(id)sender {
    if (app_ != nullptr) {
        app_->ExecuteCommand(island::BrowserCommand::kReload);
    }
}
@end

namespace {

class MenuActionsReleaseObserver final : public island::IslandAppReleaseObserver {
  public:
    explicit MenuActionsReleaseObserver(IslandMenuActions* menu_actions)
        : menu_actions_(menu_actions) {}

    void OnBeforeIslandAppRelease() override { [menu_actions_ invalidate]; }

  private:
    IslandMenuActions* menu_actions_;
};

island::StartupOptions ParseStartupOptions(int argc, char* argv[]) {
    std::vector<std::string_view> arguments;
    arguments.reserve(static_cast<std::size_t>(argc));
    for (int argument_index = 0; argument_index < argc; ++argument_index) {
        arguments.emplace_back(argv[argument_index]);
    }

    return island::StartupOptions::Parse(std::span<const std::string_view>(arguments));
}

void InstallMainMenu(IslandMenuActions* menu_actions) {
    NSMenu* main_menu = [[NSMenu alloc] init];
    NSMenuItem* application_menu_item = [[NSMenuItem alloc] init];
    NSMenu* application_menu = [[NSMenu alloc] init];
    NSMenuItem* quit_menu_item = [[NSMenuItem alloc] initWithTitle:@"Quit Island"
                                                            action:@selector(terminate:)
                                                     keyEquivalent:@"q"];
    [quit_menu_item setTarget:NSApp];
    [application_menu addItem:quit_menu_item];
    [application_menu_item setSubmenu:application_menu];
    [main_menu addItem:application_menu_item];

    NSMenuItem* browser_menu_item = [[NSMenuItem alloc] initWithTitle:@"Browser"
                                                               action:nil
                                                        keyEquivalent:@""];
    NSMenu* browser_menu = [[NSMenu alloc] initWithTitle:@"Browser"];
    NSMenuItem* back_menu_item = [[NSMenuItem alloc] initWithTitle:@"Back"
                                                            action:@selector(goBack:)
                                                     keyEquivalent:@"["];
    NSMenuItem* forward_menu_item = [[NSMenuItem alloc] initWithTitle:@"Forward"
                                                               action:@selector(goForward:)
                                                        keyEquivalent:@"]"];
    NSMenuItem* reload_menu_item = [[NSMenuItem alloc] initWithTitle:@"Reload"
                                                              action:@selector(reload:)
                                                       keyEquivalent:@"r"];
    [back_menu_item setTarget:menu_actions];
    [forward_menu_item setTarget:menu_actions];
    [reload_menu_item setTarget:menu_actions];
    [browser_menu addItem:back_menu_item];
    [browser_menu addItem:forward_menu_item];
    [browser_menu addItem:reload_menu_item];
    [browser_menu_item setSubmenu:browser_menu];
    [main_menu addItem:browser_menu_item];

    [NSApp setMainMenu:main_menu];

    [reload_menu_item release];
    [forward_menu_item release];
    [back_menu_item release];
    [browser_menu release];
    [browser_menu_item release];
    [quit_menu_item release];
    [application_menu release];
    [application_menu_item release];
    [main_menu release];
}

}  // namespace

int main(int argc, char* argv[]) {
    CefScopedLibraryLoader library_loader;
    if (!library_loader.LoadInMain()) {
        return 1;
    }

    CefMainArgs main_args(argc, argv);
    const island::StartupOptions startup_options = ParseStartupOptions(argc, argv);

    @autoreleasepool {
        [IslandApplication sharedApplication];
        CHECK([NSApp isKindOfClass:[IslandApplication class]]);

        CefRefPtr<island::IslandApp> app(new island::IslandApp(startup_options));
        IslandMenuActions* menu_actions = [[IslandMenuActions alloc] initWithApp:app.get()];
        MenuActionsReleaseObserver release_observer(menu_actions);
        InstallMainMenu(menu_actions);

        const int exit_code =
            island::RunIslandMainProcess(main_args, std::move(app), &release_observer, nullptr);

        [NSApp setMainMenu:nil];
        [menu_actions invalidate];
        [menu_actions release];
        return exit_code;
    }
}
