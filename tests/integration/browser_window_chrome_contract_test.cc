#include <cassert>

#include "browser_window.h"
#include "include/views/cef_browser_view.h"
#include "include/views/cef_view.h"
#include "include/views/cef_window.h"

namespace {

void AssertSingleBrowserChromeTree() {
    const island::ChromeViewTreeNode tree = island::BrowserChrome::ViewTreeContract();

    assert(tree.id == island::ChromeViewId::kRoot);
    assert(tree.children.size() == 2U);
    assert(tree.children[0].id == island::ChromeViewId::kRail);
    assert(tree.children[1].id == island::ChromeViewId::kBrowserContent);
    assert(tree.children[1].children.size() == 1U);
    assert(tree.children[1].children[0].id == island::ChromeViewId::kBrowserView);
}

void AssertPrimaryBackgroundClassifiesTheme() {
    assert(island::ClassifyChromeTheme(CefColorSetARGB(255, 16, 24, 32)) ==
           island::ChromeTheme::kDark);
    assert(island::ClassifyChromeTheme(CefColorSetARGB(255, 232, 240, 248)) ==
           island::ChromeTheme::kLight);
}

}  // namespace

int main() {
    AssertSingleBrowserChromeTree();
    AssertPrimaryBackgroundClassifiesTheme();
}
