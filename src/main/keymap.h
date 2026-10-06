#ifndef ISLAND_KEYMAP_H_
#define ISLAND_KEYMAP_H_

// User-configurable keyboard shortcuts. CEF-free: the window turns the
// keymap into CefWindow accelerators, the macOS menu into key equivalents,
// and Settings edits it. Bindings are written as "Mod+Shift+K", where Mod is
// Cmd on macOS and Ctrl elsewhere.

#include <array>
#include <cstdint>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <utility>
#include <vector>

namespace island {

enum class KeyAction : std::uint8_t {
    kBack,
    kForward,
    kReload,
    kFocusAddress,
    kCommandPalette,
    kSearchPalette,
    kToggleSidebar,
    kNewTab,
    kCloseTab,
    kNextTab,
    kPreviousTab,
    kNewSpace,
    kRenameSpace,
    kToggleSplit,
    kMoveDividerLeft,
    kMoveDividerRight,
    kTogglePinTab,
    kToggleAgentPanel,
    kTabOverview,
    kSettings,
};

inline constexpr std::size_t kKeyActionCount = 20;

struct KeyBinding {
    // Windows virtual-key code (CEF's key_code convention).
    int key_code = 0;
    bool primary = false;  // Cmd on macOS, Ctrl elsewhere
    bool shift = false;
    bool alt = false;

    bool operator==(const KeyBinding&) const = default;
};

struct KeyActionInfo {
    KeyAction action;
    std::string_view id;               // stable key in prefs and Settings
    std::string_view label;            // shown in Settings
    std::string_view default_binding;  // empty = unbound by default
};

[[nodiscard]] std::span<const KeyActionInfo> KeyActions() noexcept;
[[nodiscard]] const KeyActionInfo& KeyActionInfoFor(KeyAction action) noexcept;
[[nodiscard]] std::optional<KeyAction> KeyActionFromId(std::string_view id) noexcept;

// "Mod+Shift+K", "Alt+Left", "F5", "Mod+,". Case-insensitive; "Cmd",
// "Ctrl", and "Primary" all mean Mod; "Option" means Alt. A binding needs a
// modifier unless its key is a function key, so plain typing is never
// captured.
[[nodiscard]] std::optional<KeyBinding> ParseKeyBinding(std::string_view text);
// Canonical text ("Mod+Shift+K"); `mac` spells Mod/Alt as Cmd/Option.
[[nodiscard]] std::string FormatKeyBinding(const KeyBinding& binding, bool mac = false);

// What NSMenuItem needs: the key equivalent (lowercase letter, punctuation,
// or a function-key character as UTF-8) and its modifier mask flags.
struct MacKeyEquivalent {
    std::string key;
    bool command = false;
    bool shift = false;
    bool option = false;
};
[[nodiscard]] std::optional<MacKeyEquivalent> ToMacKeyEquivalent(const KeyBinding& binding);

class Keymap {
  public:
    [[nodiscard]] static Keymap Defaults();
    // Applies user overrides ({action id, binding text}; empty text unbinds)
    // on top of the defaults. Unknown ids and unparseable bindings are
    // skipped and reported in `errors`.
    [[nodiscard]] static Keymap WithOverrides(
        const std::vector<std::pair<std::string, std::string>>& overrides,
        std::vector<std::string>* errors = nullptr);

    [[nodiscard]] std::optional<KeyBinding> Binding(KeyAction action) const;
    void SetBinding(KeyAction action, std::optional<KeyBinding> binding);
    void ResetToDefault(KeyAction action);
    [[nodiscard]] std::optional<KeyAction> ActionFor(const KeyBinding& binding) const;
    // Pairs of actions sharing one binding (the earlier action wins).
    [[nodiscard]] std::vector<std::pair<KeyAction, KeyAction>> Conflicts() const;
    // The differences from the defaults, for persistence.
    [[nodiscard]] std::vector<std::pair<std::string, std::string>> Overrides() const;

    bool operator==(const Keymap&) const = default;

  private:
    std::array<std::optional<KeyBinding>, kKeyActionCount> bindings_{};
};

}  // namespace island

#endif  // ISLAND_KEYMAP_H_
