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
- (void)focusAddress:(id)sender;
- (void)openSearchPalette:(id)sender;
- (void)openCommandPalette:(id)sender;
- (void)toggleSidebar:(id)sender;
- (void)newTab:(id)sender;
- (void)closeTab:(id)sender;
- (void)selectNextTab:(id)sender;
- (void)selectPreviousTab:(id)sender;
- (void)selectNumberedTab:(id)sender;
- (void)newSpace:(id)sender;
- (void)closeSpace:(id)sender;
- (void)beginSpaceRenaming:(id)sender;
- (void)moveSpaceLeft:(id)sender;
- (void)moveSpaceRight:(id)sender;
- (void)toggleSplit:(id)sender;
- (void)moveDividerLeft:(id)sender;
- (void)moveDividerRight:(id)sender;
- (void)showWelcome:(id)sender;
- (void)useSystemTheme:(id)sender;
- (void)useLightTheme:(id)sender;
- (void)useDarkTheme:(id)sender;
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

- (void)focusAddress:(id)sender {
    if (app_ != nullptr) {
        app_->BeginAddressEditing();
    }
}

- (void)openSearchPalette:(id)sender {
    if (app_ != nullptr) {
        app_->ShowSearchPalette();
    }
}

- (void)openCommandPalette:(id)sender {
    if (app_ != nullptr) {
        app_->ShowCommandPalette();
    }
}

- (void)beginSpaceRenaming:(id)sender {
    if (app_ != nullptr) {
        app_->BeginSpaceRenaming();
    }
}

- (void)moveSpaceLeft:(id)sender {
    if (app_ != nullptr) {
        app_->MoveSpaceLeft();
    }
}

- (void)moveSpaceRight:(id)sender {
    if (app_ != nullptr) {
        app_->MoveSpaceRight();
    }
}

- (void)toggleSplit:(id)sender {
    if (app_ != nullptr) {
        app_->ExecuteCommand(island::BrowserCommand::kToggleSplit);
    }
}

- (void)moveDividerLeft:(id)sender {
    if (app_ != nullptr) {
        app_->ExecuteCommand(island::BrowserCommand::kMoveDividerLeft);
    }
}

- (void)moveDividerRight:(id)sender {
    if (app_ != nullptr) {
        app_->ExecuteCommand(island::BrowserCommand::kMoveDividerRight);
    }
}

- (void)showWelcome:(id)sender {
    if (app_ != nullptr) {
        app_->ShowWelcomeFlow();
    }
}

- (void)useSystemTheme:(id)sender {
    if (app_ != nullptr) {
        app_->SetThemePreference(island::ThemePreference::kSystem);
    }
}

- (void)useLightTheme:(id)sender {
    if (app_ != nullptr) {
        app_->SetThemePreference(island::ThemePreference::kLight);
    }
}

- (void)useDarkTheme:(id)sender {
    if (app_ != nullptr) {
        app_->SetThemePreference(island::ThemePreference::kDark);
    }
}

- (void)toggleSidebar:(id)sender {
    if (app_ != nullptr) {
        app_->ToggleSidebar();
    }
}

- (void)newTab:(id)sender {
    if (app_ != nullptr) {
        app_->ExecuteCommand(island::BrowserCommand::kNewTab);
    }
}

- (void)closeTab:(id)sender {
    if (app_ != nullptr) {
        app_->ExecuteCommand(island::BrowserCommand::kCloseTab);
    }
}

// Cmd/Ctrl+Shift+[ and Cmd/Ctrl+Shift+] are previous/next tab: Cmd+[ and Cmd+]
// already own Back/Forward, so the plain bracket keys stay navigation-only.
- (void)selectNextTab:(id)sender {
    if (app_ != nullptr) {
        app_->ExecuteCommand(island::BrowserCommand::kNextTab);
    }
}

- (void)selectPreviousTab:(id)sender {
    if (app_ != nullptr) {
        app_->ExecuteCommand(island::BrowserCommand::kPreviousTab);
    }
}

- (void)selectNumberedTab:(id)sender {
    if (app_ != nullptr && [sender isKindOfClass:[NSMenuItem class]]) {
        app_->SelectActiveSpaceTabIndex(static_cast<std::size_t>([sender tag] - 1));
    }
}

- (void)newSpace:(id)sender {
    if (app_ != nullptr) {
        app_->ExecuteCommand(island::BrowserCommand::kNewSpace);
    }
}

- (void)closeSpace:(id)sender {
    if (app_ != nullptr) {
        app_->ExecuteCommand(island::BrowserCommand::kCloseSpace);
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

// Creates a Browser-menu item targeting |menu_actions| and appends it to
// |menu|. The caller releases the returned item once the menu owns it; a zero
// |tag| leaves the item untagged. Pass an empty |key_equivalent| for items the
// design fixes no key for — they stay reachable from the menu without a
// shortcut.
NSMenuItem* AddBrowserMenuItem(NSMenu* menu, IslandMenuActions* menu_actions, NSString* title,
                               SEL action, NSString* key_equivalent, NSEventModifierFlags modifiers,
                               NSInteger tag) {
    NSMenuItem* item = [[NSMenuItem alloc] initWithTitle:title
                                                  action:action
                                           keyEquivalent:key_equivalent];
    [item setKeyEquivalentModifierMask:modifiers];
    [item setTarget:menu_actions];
    if (tag != 0) {
        [item setTag:tag];
    }
    [menu addItem:item];
    return item;
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
    NSMenuItem* focus_address_menu_item = [[NSMenuItem alloc] initWithTitle:@"Focus Address"
                                                                     action:@selector(focusAddress:)
                                                              keyEquivalent:@"l"];
    NSMenuItem* toggle_sidebar_menu_item =
        [[NSMenuItem alloc] initWithTitle:@"Toggle Sidebar"
                                   action:@selector(toggleSidebar:)
                            keyEquivalent:@"b"];
    [toggle_sidebar_menu_item setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
    [toggle_sidebar_menu_item setTarget:menu_actions];
    [focus_address_menu_item setKeyEquivalentModifierMask:NSEventModifierFlagCommand];
    [back_menu_item setTarget:menu_actions];
    [forward_menu_item setTarget:menu_actions];
    [reload_menu_item setTarget:menu_actions];
    [focus_address_menu_item setTarget:menu_actions];
    [browser_menu addItem:back_menu_item];
    [browser_menu addItem:forward_menu_item];
    [browser_menu addItem:reload_menu_item];
    [browser_menu addItem:focus_address_menu_item];
    // CefWindow::SetAccelerator never dispatches on macOS, where NSMenu key
    // equivalents own the command keys, so the palettes get the same menu route
    // Focus Address already uses. The Phase 3 design fixes Cmd+K on the command
    // palette; the search palette keeps Cmd+Shift+K. AddBrowserMenuItem inserts
    // each item as it builds it, so the palettes land between Focus Address and
    // Toggle Sidebar.
    NSMenuItem* command_palette_menu_item =
        AddBrowserMenuItem(browser_menu, menu_actions, @"Command Palette",
                           @selector(openCommandPalette:), @"k", NSEventModifierFlagCommand, 0);
    NSMenuItem* search_menu_item =
        AddBrowserMenuItem(browser_menu, menu_actions, @"Search", @selector(openSearchPalette:),
                           @"k", NSEventModifierFlagCommand | NSEventModifierFlagShift, 0);
    [browser_menu addItem:toggle_sidebar_menu_item];
    [browser_menu addItem:[NSMenuItem separatorItem]];

    NSMenuItem* new_tab_menu_item =
        AddBrowserMenuItem(browser_menu, menu_actions, @"New Tab", @selector(newTab:), @"t",
                           NSEventModifierFlagCommand, 0);
    NSMenuItem* close_tab_menu_item =
        AddBrowserMenuItem(browser_menu, menu_actions, @"Close Tab", @selector(closeTab:), @"w",
                           NSEventModifierFlagCommand, 0);
    // Cmd+[ and Cmd+] already own Back/Forward, so previous/next tab take the
    // shifted brackets.
    NSMenuItem* previous_tab_menu_item = AddBrowserMenuItem(
        browser_menu, menu_actions, @"Select Previous Tab", @selector(selectPreviousTab:), @"[",
        NSEventModifierFlagCommand | NSEventModifierFlagShift, 0);
    NSMenuItem* next_tab_menu_item = AddBrowserMenuItem(
        browser_menu, menu_actions, @"Select Next Tab", @selector(selectNextTab:), @"]",
        NSEventModifierFlagCommand | NSEventModifierFlagShift, 0);
    [browser_menu addItem:[NSMenuItem separatorItem]];
    // Cmd+1..9 switch directly to the tab at that position of the active
    // space; out-of-range positions are a no-op.
    NSMutableArray<NSMenuItem*>* numbered_tab_items = [NSMutableArray arrayWithCapacity:9];
    for (NSInteger position = 1; position <= 9; ++position) {
        NSString* title =
            [NSString stringWithFormat:@"Select Tab %ld", static_cast<long>(position)];
        NSString* key = [NSString stringWithFormat:@"%ld", static_cast<long>(position)];
        [numbered_tab_items addObject:AddBrowserMenuItem(browser_menu, menu_actions, title,
                                                         @selector(selectNumberedTab:), key,
                                                         NSEventModifierFlagCommand, position)];
    }
    [browser_menu addItem:[NSMenuItem separatorItem]];
    NSMenuItem* new_space_menu_item =
        AddBrowserMenuItem(browser_menu, menu_actions, @"New Space", @selector(newSpace:), @"",
                           NSEventModifierFlagCommand, 0);
    NSMenuItem* close_space_menu_item =
        AddBrowserMenuItem(browser_menu, menu_actions, @"Close Space", @selector(closeSpace:), @"",
                           NSEventModifierFlagCommand, 0);
    // Rename and reorder stay reachable without memorizing a shortcut: menu
    // entries are their entry point, and F2 is the cross-platform rename
    // accelerator registered in BrowserWindow.
    NSMenuItem* rename_space_menu_item =
        AddBrowserMenuItem(browser_menu, menu_actions, @"Rename Space…",
                           @selector(beginSpaceRenaming:), @"", NSEventModifierFlagCommand, 0);
    NSMenuItem* move_space_left_menu_item =
        AddBrowserMenuItem(browser_menu, menu_actions, @"Move Space Left",
                           @selector(moveSpaceLeft:), @"", NSEventModifierFlagCommand, 0);
    NSMenuItem* move_space_right_menu_item =
        AddBrowserMenuItem(browser_menu, menu_actions, @"Move Space Right",
                           @selector(moveSpaceRight:), @"", NSEventModifierFlagCommand, 0);
    [browser_menu addItem:[NSMenuItem separatorItem]];
    // U6 split view. The toggle carries Cmd+Shift+S, matching the accelerator
    // registered in BrowserWindow; the divider nudges stay menu-only here
    // because Shift+Cmd+Arrow collides with text selection in the address
    // field (Windows/Linux dispatch them through CefWindow::SetAccelerator).
    NSMenuItem* toggle_split_menu_item = AddBrowserMenuItem(
        browser_menu, menu_actions, @"Split with Adjacent Tab", @selector(toggleSplit:), @"s",
        NSEventModifierFlagCommand | NSEventModifierFlagShift, 0);
    NSMenuItem* move_divider_left_menu_item =
        AddBrowserMenuItem(browser_menu, menu_actions, @"Move Split Divider Left",
                           @selector(moveDividerLeft:), @"", NSEventModifierFlagCommand, 0);
    NSMenuItem* move_divider_right_menu_item =
        AddBrowserMenuItem(browser_menu, menu_actions, @"Move Split Divider Right",
                           @selector(moveDividerRight:), @"", NSEventModifierFlagCommand, 0);
    [browser_menu addItem:[NSMenuItem separatorItem]];
    // The welcome flow reopens without resetting anything; the appearance
    // entries write the preference and re-theme the window immediately.
    NSMenuItem* welcome_menu_item =
        AddBrowserMenuItem(browser_menu, menu_actions, @"Show Welcome…", @selector(showWelcome:),
                           @"", NSEventModifierFlagCommand, 0);
    NSMenuItem* theme_system_item =
        AddBrowserMenuItem(browser_menu, menu_actions, @"Appearance: System",
                           @selector(useSystemTheme:), @"", NSEventModifierFlagCommand, 0);
    NSMenuItem* theme_light_item =
        AddBrowserMenuItem(browser_menu, menu_actions, @"Appearance: Light",
                           @selector(useLightTheme:), @"", NSEventModifierFlagCommand, 0);
    NSMenuItem* theme_dark_item =
        AddBrowserMenuItem(browser_menu, menu_actions, @"Appearance: Dark",
                           @selector(useDarkTheme:), @"", NSEventModifierFlagCommand, 0);
    [browser_menu_item setSubmenu:browser_menu];
    [main_menu addItem:browser_menu_item];

    [NSApp setMainMenu:main_menu];

    [theme_dark_item release];
    [theme_light_item release];
    [theme_system_item release];
    [welcome_menu_item release];
    [move_divider_right_menu_item release];
    [move_divider_left_menu_item release];
    [toggle_split_menu_item release];
    [move_space_right_menu_item release];
    [move_space_left_menu_item release];
    [rename_space_menu_item release];
    [close_space_menu_item release];
    [new_space_menu_item release];
    for (NSMenuItem* item in numbered_tab_items) {
        [item release];
    }
    [next_tab_menu_item release];
    [previous_tab_menu_item release];
    [close_tab_menu_item release];
    [new_tab_menu_item release];
    [toggle_sidebar_menu_item release];
    [search_menu_item release];
    [command_palette_menu_item release];
    [focus_address_menu_item release];
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
