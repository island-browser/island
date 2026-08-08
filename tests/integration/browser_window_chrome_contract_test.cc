#include <cassert>

#include "browser_chrome.h"

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

}  // namespace

int main() { AssertSingleBrowserChromeTree(); }
