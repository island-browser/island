#ifndef ISLAND_MAIN_DEV_CAPTURE_H_
#define ISLAND_MAIN_DEV_CAPTURE_H_

// Developer-only startup flags that drive the window into a state and save a
// PNG of it, so the native chrome can be reviewed without screen-recording
// permission (a process may always capture its own windows):
//
//   --island-dev-actions=welcome,theme:dark,command_palette
//       Runs each action in order, a short pause apart, once the window is up.
//       Besides the menu-backed names, `navigate:<address>` loads an address in the active
//       tab and `sidebar:expanded|collapsed` sets the persistent sidebar state. `peek:<address>`
//       opens the peek card, `sleep_tabs` puts eligible inactive tabs to sleep and
//       `sleep_report` logs the sleeping-tab count, `favorite:global:<i>` or
//       `favorite:space:<i>` shows the i-th (zero-based) sidebar favorite, `wait:<seconds>`
//       pauses the sequence,
//       `research:<question>` starts a Research task and `open_report` opens the newest stored
//       report in a new tab, `plan_fixture` opens a Plan tab with the sample research plan
//       waiting for approval, `agent_pages:<count>[:<gap>]` opens that many agent pages through
//       the agent's open-tab path (after `plan_fixture`; gap `serial` or milliseconds), and
//       `cheatsheet` opens the shortcut cheatsheet. `agent_fixture:confirm` shows a sample agent
//       confirmation card in the panel. `agent_fixture:queue` shows one running and two queued
//       agent tasks in the panel. `agent_fixture:quote` shows a page chip with a quote chip.
//       `agent_fixture:collect` opens the Collect sheet. `agent_fixture:thread` fills the panel
//       with a sample conversation (a user message, three tool rows, a Markdown reply) and one
//       queued message. `report_fixture:collection` opens a collection report with its Export
//       menu open.
//       `settings:<section>` opens Settings on one section (appearance,
//       shortcuts, ...). `appearance:<key>=<value>` (sidebar_tint, sidebar_side, card_radius,
//       density, font,
//       card_shadow, full_bleed, reset) and `space_color:<index>=#RRGGBB` run the
//       Settings > Appearance path. `space_icon:<index>=<id>` chooses a space icon.
//   --island-dev-capture=/tmp/shot.png
//       Saves the window (with any overlays) after the actions, then quits.
//   --island-dev-delay=4
//       Seconds to wait after the last action before capturing (default 3).
//   --island-dev-keep-open
//       Do not quit after capturing.
//
// Run captures with a scratch HOME so theme actions never touch real prefs.

#include <cstddef>
#include <optional>
#include <span>
#include <string>
#include <string_view>
#include <vector>

namespace island {

struct DevCaptureOptions {
    std::vector<std::string> actions;
    std::string capture_path;
    double delay_seconds = 3.0;
    bool keep_open = false;

    [[nodiscard]] bool enabled() const { return !actions.empty() || !capture_path.empty(); }

    // Unknown or malformed --island-dev-* values are ignored (with defaults
    // kept), never fatal: these flags must not be able to break a normal run.
    [[nodiscard]] static DevCaptureOptions Parse(std::span<const std::string_view> arguments);
};

// The menu selector a menu-backed action name runs ("cheatsheet" -> "showCheatsheet:"), or ""
// for parameterized and unknown actions.
[[nodiscard]] std::string_view DevActionSelectorName(std::string_view action);

// The Settings section a `settings:<section>` action opens ("settings:shortcuts" -> "shortcuts"),
// or "" when the action is not one.
[[nodiscard]] std::string_view DevSettingsSectionName(std::string_view action);

// The fixture a `agent_fixture:<name>` action shows ("agent_fixture:confirm" -> "confirm"), or ""
// when the action is not one. `confirm` opens the agent panel with a sample confirmation card.
[[nodiscard]] std::string_view DevAgentFixtureName(std::string_view action);

struct DevSpaceIconAction {
    std::size_t index = 0;
    std::string icon_id;

    bool operator==(const DevSpaceIconAction&) const = default;
};

// Parses `space_icon:<index>=<known-id>`; `none` clears it. Malformed and unknown ids are ignored.
[[nodiscard]] std::optional<DevSpaceIconAction> ParseDevSpaceIconAction(std::string_view action);

}  // namespace island

#endif  // ISLAND_MAIN_DEV_CAPTURE_H_
