#include "keymap.h"

#include <cctype>

namespace island {

namespace {

constexpr std::array<KeyActionInfo, kKeyActionCount> kActions = {{
#if defined(__APPLE__)
    // macOS browsers use Cmd+[ / Cmd+] for history.
    {KeyAction::kBack, "back", "Back", "Mod+["},
    {KeyAction::kForward, "forward", "Forward", "Mod+]"},
#else
    {KeyAction::kBack, "back", "Back", "Alt+Left"},
    {KeyAction::kForward, "forward", "Forward", "Alt+Right"},
#endif
    {KeyAction::kReload, "reload", "Reload", "Mod+R"},
    {KeyAction::kFocusAddress, "focus_address", "Focus address", "Mod+L"},
    {KeyAction::kCommandPalette, "command_palette", "Command palette", "Mod+K"},
    {KeyAction::kSearchPalette, "search_palette", "Search palette", "Mod+Shift+K"},
    {KeyAction::kToggleSidebar, "toggle_sidebar", "Show or hide the sidebar", "Mod+B"},
    {KeyAction::kNewTab, "new_tab", "New tab", "Mod+T"},
    {KeyAction::kCloseTab, "close_tab", "Close tab", "Mod+W"},
    {KeyAction::kNextTab, "next_tab", "Next tab", "Mod+Shift+]"},
    {KeyAction::kPreviousTab, "previous_tab", "Previous tab", "Mod+Shift+["},
    {KeyAction::kNewSpace, "new_space", "New space", "Mod+Alt+N"},
    {KeyAction::kRenameSpace, "rename_space", "Rename space", "F2"},
    {KeyAction::kToggleSplit, "toggle_split", "Split view with the next tab", "Mod+Shift+S"},
    {KeyAction::kMoveDividerLeft, "divider_left", "Move split divider left", "Mod+Alt+["},
    {KeyAction::kMoveDividerRight, "divider_right", "Move split divider right", "Mod+Alt+]"},
    {KeyAction::kTogglePinTab, "toggle_pin", "Pin or unpin tab", "Mod+D"},
    {KeyAction::kToggleAgentPanel, "toggle_agent", "Show or hide the agent", "Mod+J"},
    {KeyAction::kTabOverview, "tab_overview", "All tabs", "Mod+Shift+A"},
    {KeyAction::kSettings, "settings", "Settings", "Mod+,"},
}};

struct NamedKey {
    std::string_view name;
    int code;
    // UTF-8 key equivalent for NSMenuItem (function keys use the private-use
    // NSFunctionKey characters).
    std::string_view mac;
};

constexpr std::array<NamedKey, 26> kNamedKeys = {{
    {"Left", 0x25, "\xEF\x9C\x82"},   // U+F702
    {"Up", 0x26, "\xEF\x9C\x80"},     // U+F700
    {"Right", 0x27, "\xEF\x9C\x83"},  // U+F703
    {"Down", 0x28, "\xEF\x9C\x81"},   // U+F701
    {"Space", 0x20, " "},
    {"Enter", 0x0D, "\r"},
    {"Tab", 0x09, "\t"},
    {"Backspace", 0x08, "\x08"},
    {"Delete", 0x2E, "\xEF\x9C\xA8"},  // U+F728
    {"Escape", 0x1B, "\x1B"},
    {"Home", 0x24, "\xEF\x9C\xA9"},      // U+F729
    {"End", 0x23, "\xEF\x9C\xAB"},       // U+F72B
    {"PageUp", 0x21, "\xEF\x9C\xAC"},    // U+F72C
    {"PageDown", 0x22, "\xEF\x9C\xAD"},  // U+F72D
    {",", 0xBC, ","},
    {".", 0xBE, "."},
    {"/", 0xBF, "/"},
    {";", 0xBA, ";"},
    {"'", 0xDE, "'"},
    {"[", 0xDB, "["},
    {"]", 0xDD, "]"},
    {"\\", 0xDC, "\\"},
    {"-", 0xBD, "-"},
    {"=", 0xBB, "="},
    {"`", 0xC0, "`"},
    {"Esc", 0x1B, "\x1B"},
}};

bool EqualsIgnoreCase(std::string_view a, std::string_view b) {
    if (a.size() != b.size()) return false;
    for (std::size_t i = 0; i < a.size(); ++i) {
        if (std::tolower(static_cast<unsigned char>(a[i])) !=
            std::tolower(static_cast<unsigned char>(b[i]))) {
            return false;
        }
    }
    return true;
}

std::string_view Trim(std::string_view text) {
    while (!text.empty() && text.front() == ' ') text.remove_prefix(1);
    while (!text.empty() && text.back() == ' ') text.remove_suffix(1);
    return text;
}

bool IsFunctionKey(int code) { return code >= 0x70 && code <= 0x87; }

std::optional<int> KeyCodeForName(std::string_view name) {
    if (name.size() == 1) {
        const char c = name[0];
        if (std::isalpha(static_cast<unsigned char>(c))) {
            return std::toupper(static_cast<unsigned char>(c));
        }
        if (c >= '0' && c <= '9') return c;
    }
    if (name.size() >= 2 && (name[0] == 'F' || name[0] == 'f')) {
        int number = 0;
        for (std::size_t i = 1; i < name.size(); ++i) {
            if (!std::isdigit(static_cast<unsigned char>(name[i]))) return std::nullopt;
            number = number * 10 + (name[i] - '0');
        }
        if (number >= 1 && number <= 24) return 0x70 + number - 1;
        return std::nullopt;
    }
    for (const NamedKey& key : kNamedKeys) {
        if (EqualsIgnoreCase(key.name, name)) return key.code;
    }
    return std::nullopt;
}

std::string KeyName(int code) {
    if ((code >= 'A' && code <= 'Z') || (code >= '0' && code <= '9')) {
        return std::string(1, static_cast<char>(code));
    }
    if (IsFunctionKey(code)) return "F" + std::to_string(code - 0x70 + 1);
    for (const NamedKey& key : kNamedKeys) {
        if (key.code == code) return std::string(key.name);
    }
    return "Key" + std::to_string(code);
}

void AppendUtf8(std::string& out, std::uint32_t cp) {
    if (cp < 0x80) {
        out += static_cast<char>(cp);
    } else if (cp < 0x800) {
        out += static_cast<char>(0xC0 | (cp >> 6));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    } else {
        out += static_cast<char>(0xE0 | (cp >> 12));
        out += static_cast<char>(0x80 | ((cp >> 6) & 0x3F));
        out += static_cast<char>(0x80 | (cp & 0x3F));
    }
}

std::size_t IndexOf(KeyAction action) { return static_cast<std::size_t>(action); }

}  // namespace

std::span<const KeyActionInfo> KeyActions() noexcept { return kActions; }

const KeyActionInfo& KeyActionInfoFor(KeyAction action) noexcept {
    return kActions[IndexOf(action)];
}

std::optional<KeyAction> KeyActionFromId(std::string_view id) noexcept {
    for (const KeyActionInfo& info : kActions) {
        if (info.id == id) return info.action;
    }
    return std::nullopt;
}

std::optional<KeyBinding> ParseKeyBinding(std::string_view text) {
    text = Trim(text);
    if (text.empty()) return std::nullopt;
    KeyBinding binding;
    std::optional<int> key;
    std::size_t start = 0;
    while (start <= text.size()) {
        std::size_t plus = text.find('+', start);
        // A trailing "+" token (e.g. "Mod++") is not supported; use "=".
        if (plus == start && plus + 1 == text.size()) return std::nullopt;
        const std::string_view token =
            Trim(text.substr(start, plus == std::string_view::npos ? text.npos : plus - start));
        if (token.empty()) return std::nullopt;
        const bool last = plus == std::string_view::npos;
        if (!last) {
            if (EqualsIgnoreCase(token, "Mod") || EqualsIgnoreCase(token, "Cmd") ||
                EqualsIgnoreCase(token, "Ctrl") || EqualsIgnoreCase(token, "Control") ||
                EqualsIgnoreCase(token, "Primary") || EqualsIgnoreCase(token, "Command")) {
                binding.primary = true;
            } else if (EqualsIgnoreCase(token, "Shift")) {
                binding.shift = true;
            } else if (EqualsIgnoreCase(token, "Alt") || EqualsIgnoreCase(token, "Option")) {
                binding.alt = true;
            } else {
                return std::nullopt;
            }
            start = plus + 1;
            continue;
        }
        key = KeyCodeForName(token);
        break;
    }
    if (!key) return std::nullopt;
    binding.key_code = *key;
    // Plain typing must never be captured: require a modifier other than
    // Shift, except for function keys.
    if (!binding.primary && !binding.alt && !IsFunctionKey(binding.key_code)) return std::nullopt;
    return binding;
}

std::string FormatKeyBinding(const KeyBinding& binding, bool mac) {
    std::string text;
    if (binding.primary) text += mac ? "Cmd+" : "Mod+";
    if (binding.alt) text += mac ? "Option+" : "Alt+";
    if (binding.shift) text += "Shift+";
    text += KeyName(binding.key_code);
    return text;
}

std::optional<MacKeyEquivalent> ToMacKeyEquivalent(const KeyBinding& binding) {
    MacKeyEquivalent equivalent{
        .key = {}, .command = binding.primary, .shift = binding.shift, .option = binding.alt};
    const int code = binding.key_code;
    if (code >= 'A' && code <= 'Z') {
        equivalent.key = std::string(1, static_cast<char>(code - 'A' + 'a'));
    } else if (code >= '0' && code <= '9') {
        equivalent.key = std::string(1, static_cast<char>(code));
    } else if (IsFunctionKey(code)) {
        AppendUtf8(equivalent.key, 0xF704U + static_cast<std::uint32_t>(code - 0x70));
    } else {
        for (const NamedKey& key : kNamedKeys) {
            if (key.code == code) {
                equivalent.key = std::string(key.mac);
                break;
            }
        }
    }
    if (equivalent.key.empty()) return std::nullopt;
    return equivalent;
}

Keymap Keymap::Defaults() {
    Keymap keymap;
    for (const KeyActionInfo& info : kActions) {
        keymap.bindings_[IndexOf(info.action)] = ParseKeyBinding(info.default_binding);
    }
    return keymap;
}

Keymap Keymap::WithOverrides(const std::vector<std::pair<std::string, std::string>>& overrides,
                             std::vector<std::string>* errors) {
    Keymap keymap = Defaults();
    for (const auto& [id, text] : overrides) {
        const std::optional<KeyAction> action = KeyActionFromId(id);
        if (!action) {
            if (errors != nullptr) errors->push_back("Unknown shortcut action '" + id + "'");
            continue;
        }
        if (Trim(text).empty()) {
            keymap.SetBinding(*action, std::nullopt);
            continue;
        }
        const std::optional<KeyBinding> binding = ParseKeyBinding(text);
        if (!binding) {
            if (errors != nullptr) errors->push_back("Invalid shortcut '" + text + "' for " + id);
            continue;
        }
        keymap.SetBinding(*action, binding);
    }
    return keymap;
}

std::optional<KeyBinding> Keymap::Binding(KeyAction action) const {
    return bindings_[IndexOf(action)];
}

void Keymap::SetBinding(KeyAction action, std::optional<KeyBinding> binding) {
    bindings_[IndexOf(action)] = binding;
}

void Keymap::ResetToDefault(KeyAction action) {
    bindings_[IndexOf(action)] = ParseKeyBinding(KeyActionInfoFor(action).default_binding);
}

std::optional<KeyAction> Keymap::ActionFor(const KeyBinding& binding) const {
    for (const KeyActionInfo& info : kActions) {
        if (bindings_[IndexOf(info.action)] == binding) return info.action;
    }
    return std::nullopt;
}

std::vector<std::pair<KeyAction, KeyAction>> Keymap::Conflicts() const {
    std::vector<std::pair<KeyAction, KeyAction>> conflicts;
    for (std::size_t a = 0; a < kActions.size(); ++a) {
        if (!bindings_[a]) continue;
        for (std::size_t b = a + 1; b < kActions.size(); ++b) {
            if (bindings_[b] == bindings_[a]) {
                conflicts.emplace_back(kActions[a].action, kActions[b].action);
            }
        }
    }
    return conflicts;
}

std::vector<std::pair<std::string, std::string>> Keymap::Overrides() const {
    const Keymap defaults = Defaults();
    std::vector<std::pair<std::string, std::string>> overrides;
    for (const KeyActionInfo& info : kActions) {
        const std::optional<KeyBinding>& current = bindings_[IndexOf(info.action)];
        if (current == defaults.bindings_[IndexOf(info.action)]) continue;
        overrides.emplace_back(std::string(info.id),
                               current ? FormatKeyBinding(*current) : std::string());
    }
    return overrides;
}

}  // namespace island
