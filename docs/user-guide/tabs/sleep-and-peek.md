# Tab sleep and Peek

**Partly written.** The Peek section below is current. The tab sleep section is still planned.

## Peek

Peek shows a link in a floating card over your page, so you can read it without leaving the page.

* **Cmd/Ctrl+click** a link to open it in Peek. The middle button also opens Peek: the browser
  reports a middle-click and a Cmd/Ctrl+click the same way, so Island cannot tell them apart.
* **Right-click** a link and choose **Open in peek**.
* A card that is open can be moved into the current space with **Open as tab** (the plus icon).
  The page is reopened as a new tab next to the current one. Its history and scroll position are
  not carried over.
* **Close** (the × icon), or Esc, closes the card.

The card covers most of the window: 80% of its width and 85% of its height, centered. It resizes
with the window while it is open.

Other clicks do not open Peek. A plain click on a link that opens in a new tab (target=_blank, or
a site's window.open after a click) and a Shift+click open an ordinary tab in front, next to the
current tab.

Pop-ups that a page opens without a click are blocked.

## Tab sleep

**Planned.** This section is not written yet.

Tab sleep frees memory from tabs you are not using.

## What this page will cover (tab sleep)

* How sleep works and what a sleeping tab keeps (URL, title, favicon, scroll position)
* Choosing 15, 30 or 60 minutes, or Never, in Settings > Tabs
* Which tabs never sleep
* Sleeping tabs now from the command palette
