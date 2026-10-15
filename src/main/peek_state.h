#ifndef ISLAND_MAIN_PEEK_STATE_H_
#define ISLAND_MAIN_PEEK_STATE_H_

#include <cstdint>
#include <optional>
#include <string>
#include <utility>

namespace island {

constexpr int kPeekEscapeKeyCode = 0x1B;

// The CEF-free state of the floating peek card ("Little Arc"): at most one page is peeked at
// a time. The card's browser reports its title and address as it navigates; the title bar
// shows them. `generation` changes on every open and close so asynchronous callbacks of an
// earlier peek (a late title, a late browser-created) can tell they are stale.
class PeekState {
  public:
    // Opens |url|, replacing whatever is peeked. An empty url is ignored.
    bool Open(std::string url) {
        if (url.empty()) {
            return false;
        }
        open_ = true;
        url_ = std::move(url);
        title_.clear();
        ++generation_;
        return true;
    }

    // Idempotent. Returns whether a peek was open.
    bool Close() {
        if (!open_) {
            return false;
        }
        open_ = false;
        url_.clear();
        title_.clear();
        ++generation_;
        return true;
    }

    // "Open as tab": the address to open in the active space; the peek closes. Nothing when no
    // peek is open.
    std::optional<std::string> TakeForTab() {
        if (!open_) {
            return std::nullopt;
        }
        std::string url = url_;
        Close();
        return url;
    }

    // Escape closes the peek. Returns whether the key was consumed.
    bool HandleKey(int virtual_key) {
        if (open_ && virtual_key == kPeekEscapeKeyCode) {
            Close();
            return true;
        }
        return false;
    }

    // Navigation inside the card (a link followed in the peek) updates the address.
    void SetUrl(std::string url) {
        if (open_ && !url.empty()) {
            url_ = std::move(url);
        }
    }
    void SetTitle(std::string title) {
        if (open_) {
            title_ = std::move(title);
        }
    }

    [[nodiscard]] bool open() const noexcept { return open_; }
    [[nodiscard]] const std::string& url() const noexcept { return url_; }
    [[nodiscard]] const std::string& title() const noexcept { return title_; }
    [[nodiscard]] std::uint64_t generation() const noexcept { return generation_; }

  private:
    bool open_ = false;
    std::string url_;
    std::string title_;
    std::uint64_t generation_ = 0;
};

}  // namespace island

#endif  // ISLAND_MAIN_PEEK_STATE_H_
