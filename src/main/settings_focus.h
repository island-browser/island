#ifndef ISLAND_MAIN_SETTINGS_FOCUS_H_
#define ISLAND_MAIN_SETTINGS_FOCUS_H_

#include <cstdint>
#include <string>
#include <utility>

namespace island {

// The pending request to open one Settings section. The page opens a section only when its
// sequence number is new, so asking for a section that is already the selected one (privacy
// shield, then a click on Tabs, then the shield again) still moves the page. The section is
// cleared once the page has loaded and shown it; the sequence keeps counting.
class SettingsFocus {
  public:
    void Request(std::string section) {
        section_ = std::move(section);
        ++sequence_;
    }

    void Clear() { section_.clear(); }

    [[nodiscard]] const std::string& section() const noexcept { return section_; }
    [[nodiscard]] std::uint64_t sequence() const noexcept { return sequence_; }

  private:
    std::string section_;
    std::uint64_t sequence_ = 0;
};

}  // namespace island

#endif  // ISLAND_MAIN_SETTINGS_FOCUS_H_
