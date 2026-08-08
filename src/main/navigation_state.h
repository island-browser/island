#ifndef ISLAND_NAVIGATION_STATE_H_
#define ISLAND_NAVIGATION_STATE_H_

#include <cstdint>
#include <optional>
#include <string>

namespace island {

enum class LoadPhase : std::uint8_t {
    kNotStarted,
    kLoading,
    kCompleted,
    kFailed,
    kClosed,
};

struct NavigationSnapshot {
    std::uint64_t revision = 0;
    std::string url;
    std::string page_title;
    std::string display_title = "Island";
    LoadPhase load_phase = LoadPhase::kNotStarted;
    bool can_go_back = false;
    bool can_go_forward = false;
    std::optional<int> http_status;
    std::optional<int> network_error;

    bool operator==(const NavigationSnapshot&) const = default;
};

class NavigationObserver {
  public:
    virtual ~NavigationObserver() = default;

    virtual void OnNavigationChanged(const NavigationSnapshot& snapshot) = 0;
};

class NavigationState {
  public:
    NavigationState() = default;

    [[nodiscard]] const NavigationSnapshot& snapshot() const noexcept;

    void SetObserver(NavigationObserver* observer);
    void SetAddress(std::string url);
    void OnLoadStart(std::string url);
    void OnTitleChange(std::string page_title);
    void OnLoadEnd(int http_status);
    void OnLoadError(int network_error);
    void OnLoadingStateChange(bool can_go_back, bool can_go_forward);
    void Close();

  private:
    void PublishIfChanged(const NavigationSnapshot& previous);

    NavigationSnapshot snapshot_;
    NavigationObserver* observer_ = nullptr;
};

}  // namespace island

#endif
