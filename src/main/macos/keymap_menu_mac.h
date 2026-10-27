#ifndef ISLAND_MACOS_KEYMAP_MENU_MAC_H_
#define ISLAND_MACOS_KEYMAP_MENU_MAC_H_

#include "keymap.h"

#ifdef __OBJC__
@class NSMenuItem;
#endif

namespace island {

#ifdef __OBJC__
void ApplyMacMenuKeyBinding(NSMenuItem* item, KeyAction action, const Keymap& keymap);
#endif

// Reapplies the live keymap to all configurable Island items in NSApp's menu tree.
void RefreshMacMenuKeymap(const Keymap& keymap);

}  // namespace island

#endif  // ISLAND_MACOS_KEYMAP_MENU_MAC_H_
