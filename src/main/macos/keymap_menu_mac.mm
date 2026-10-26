#import "macos/keymap_menu_mac.h"

#import <Cocoa/Cocoa.h>

#include <optional>

namespace island {
namespace {

NSString* const kActionPrefix = @"island-key-action:";

NSEventModifierFlags ModifierMask(const MacKeyEquivalent& equivalent) {
    NSEventModifierFlags mask = 0;
    if (equivalent.command) mask |= NSEventModifierFlagCommand;
    if (equivalent.control) mask |= NSEventModifierFlagControl;
    if (equivalent.shift) mask |= NSEventModifierFlagShift;
    if (equivalent.option) mask |= NSEventModifierFlagOption;
    return mask;
}

void RefreshMenu(NSMenu* menu, const Keymap& keymap) {
    for (NSMenuItem* item in [menu itemArray]) {
        NSString* marker = [item representedObject];
        if ([marker isKindOfClass:[NSString class]] && [marker hasPrefix:kActionPrefix]) {
            const std::string id([[marker substringFromIndex:[kActionPrefix length]] UTF8String]);
            if (const std::optional<KeyAction> action = KeyActionFromId(id)) {
                ApplyMacMenuKeyBinding(item, *action, keymap);
            }
        }
        if ([item submenu] != nil) {
            RefreshMenu([item submenu], keymap);
        }
    }
}

}  // namespace

void ApplyMacMenuKeyBinding(NSMenuItem* item, KeyAction action, const Keymap& keymap) {
    const std::string action_id(KeyActionInfoFor(action).id);
    [item setRepresentedObject:[kActionPrefix
                                   stringByAppendingString:[NSString
                                                               stringWithUTF8String:action_id
                                                                                        .c_str()]]];
    const std::optional<MacKeyEquivalent> equivalent = MacKeyEquivalentForAction(keymap, action);
    if (!equivalent.has_value()) {
        [item setKeyEquivalent:@""];
        [item setKeyEquivalentModifierMask:0];
        return;
    }
    [item setKeyEquivalent:[NSString stringWithUTF8String:equivalent->key.c_str()]];
    [item setKeyEquivalentModifierMask:ModifierMask(*equivalent)];
}

void RefreshMacMenuKeymap(const Keymap& keymap) {
    if ([NSApp mainMenu] != nil) {
        RefreshMenu([NSApp mainMenu], keymap);
    }
}

}  // namespace island
