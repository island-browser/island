# Split view

Split view shows two tabs side by side in the same window.

## Split two tabs

1. Select the first tab.
2. Press ⇧⌘S (Ctrl+Shift+S).

Island pairs the active tab with the tab to its right. If the active tab is the last one, it pairs with the tab to its left. You need at least two tabs in the space. On macOS you can also use **Browser > Split with Adjacent Tab**.

To pair two specific tabs, put them next to each other first.

## Leave split view

Press ⇧⌘S (Ctrl+Shift+S) again. The tabs stay open as normal tabs.

## Move the divider

Move the divider with the keyboard:

* ⌥⌘[ (Ctrl+Alt+[) moves it left.
* ⌥⌘] (Ctrl+Alt+]) moves it right.

On macOS, the same actions are in the Browser menu: **Move Split Divider Left** and **Move Split Divider Right**.

{% hint style="info" %}
**macOS only:** you cannot drag the divider with the mouse. The pinned CEF distribution exposes no mouse events on custom views, so the divider moves by keyboard and menu only.
{% endhint %}

## Good to know

* Tabs in a split pair never go to sleep.
* A quit keeps your split pairs, and Island restores them on the next launch.
* You can change the shortcuts in [Shortcuts](../settings/shortcuts.md).
