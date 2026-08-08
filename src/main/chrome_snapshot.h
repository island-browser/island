#ifndef ISLAND_CHROME_SNAPSHOT_H_
#define ISLAND_CHROME_SNAPSHOT_H_

#include <string>

namespace island {

struct DipRect {
    int x = 0;
    int y = 0;
    int width = 0;
    int height = 0;

    bool operator==(const DipRect&) const = default;
};

enum class FocusTarget {
    kBack,
    kForward,
    kReload,
    kAddress,
    kActiveTab,
};

struct ChromeSnapshot {
    FocusTarget focus_target = FocusTarget::kBack;
    DipRect rail_bounds;
    DipRect content_bounds;
    bool back_enabled = false;
    bool forward_enabled = false;
    std::string active_page_title;

    bool operator==(const ChromeSnapshot&) const = default;
};

class ChromeObserver {
  public:
    virtual ~ChromeObserver() = default;

    virtual void OnChromeChanged(const ChromeSnapshot& snapshot) = 0;
};

}  // namespace island

#endif
