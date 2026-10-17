# The sidebar

The sidebar holds your pinned tabs, your tabs, the space switcher and, in the Native look, the Agent button. This page shows how to change how it looks and where it sits.

## Choose a look

Island has three looks. The default is **Native**.

| Look | What it is |
| --- | --- |
| **Native** | A macOS-style source list, 248 DIP wide, with the system font, one blue accent and a 52 DIP toolbar. Spaces have no color. |
| **Arc** | A frameless window (macOS) with a canvas tinted by the active space's color and a rounded, shadowed page card. |
| **Graphite** | A neutral look with the Geist font. |

To switch, open **Settings > Appearance** and choose a value under **Look**. The change applies at once. If you then change any other control there, the preset reads **Custom**.

If you used Island before the Native look arrived and had saved Appearance settings, you keep Arc until you choose another look.

## Collapse the sidebar

Press ⌘B (Ctrl+B). The sidebar shrinks to a 52 DIP icon rail:

* one button per tab of the active space, with pinned tabs first and the active tab highlighted;
* a **+** button for a new tab;
* the spaces at the bottom.

Hover a tab button to see the page title. Island remembers the choice across launches. Press ⌘B again to expand.

When you focus the address field from the rail with ⌘L (Ctrl+L), the sidebar expands for the length of the edit.

## Move the sidebar to the right

Open **Settings > Appearance > Layout** and set **Sidebar position** to **Left** or **Right**.

## Change density, corners and shadow

All of these are under **Settings > Appearance > Layout**:

* **Sidebar density**: Comfortable or Compact.
* **Page corner radius**: 0, 6, 10 or 14 (macOS).
* **Full bleed**: no inset, no rounding, no shadow.
* **Page card shadow**: one soft shadow under the page.

See [Appearance](../settings/appearance.md) for every control.

## The Agent button

In the Native look, a full-width **Agent** button sits at the bottom of the sidebar. It is tinted while the agent panel is open. Click it, or press ⌘J (Ctrl+J), to open the [agent panel](../agent/agent-panel.md).

## Address field indicators

The Native address field shows small indicators:

* A **shield** on the left with the number of blocked requests. Click it to open Settings > Privacy & blocking. It is hidden at 0.
* **Reader** on the right, only on pages that have a reader view.
* An **agent dot**: blue while the agent works, orange while it waits for your answer. Click it to open the agent panel.
