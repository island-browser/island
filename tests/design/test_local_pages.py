"""Static guards for Island's built-in HTML pages (`src/main/pages/*.html`).

Local pages must never request an external resource, must speak the shared
native bridge, and the agent panel's Markdown renderer must be safe: all model
text is escaped, only generated tags appear, and only http, https and mailto
links survive. The renderer is exercised through Node when it is installed
(CI runners and developer machines have it; the dependency is optional, so those
checks skip without it) and statically otherwise.
"""

from __future__ import annotations

import json
import re
import shutil
import subprocess
import unittest
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
PAGES = REPOSITORY / "src/main/pages"
PAGE_FILES = sorted(PAGES.glob("*.html"))
# Scripts the build inlines into pages (cmake/CMakeLists.txt, "@@ISLAND_MARKDOWN@@").
SCRIPT_FILES = sorted(PAGES.glob("*.js"))
PLACEHOLDER = "  // @@ISLAND_MARKDOWN@@"

FORBIDDEN_FRAGMENTS = (
    "<script src",
    "<link",
    "@import",
    "url(",
    "<img",
    "<iframe",
    "<object",
    "<embed",
    "fetch(",
    "XMLHttpRequest",
    "WebSocket",
    "sendBeacon",
    "import(",
)

MARKDOWN_BLOCK = re.compile(
    r"// --- Markdown \(BEGIN MARKDOWN\) ---\n(?P<code>.*?)// --- Markdown \(END MARKDOWN\) ---",
    re.DOTALL,
)
NODE = shutil.which("node")

ALLOWED_TAGS = {
    "p", "br", "strong", "em", "del", "code", "pre", "ul", "ol", "li", "blockquote", "hr",
    "h1", "h2", "h3", "h4", "h5", "h6", "a", "table", "thead", "tbody", "tr", "th", "td", "div",
}
TAG = re.compile(r"<(/?)([a-zA-Z][a-zA-Z0-9]*)((?:\s+[a-zA-Z-]+=\"[^\"<>]*\")*)\s*/?>")
ALLOWED_ATTRIBUTES = {"href", "rel", "title", "style", "start", "class"}

THEME_VARIABLES = {
    "fill": "--fill",
    "border_2": "--border-2",
    "text_3": "--text-3",
    "selection": "--selection",
    "accent_soft": "--accent-soft",
    "accent_text": "--accent-text",
    "warn_bg": "--warn-bg",
    "warn_border": "--warn-border",
    "warn_text": "--warn-text",
    "success": "--success",
    "danger": "--danger",
}
BRANCH_VARIABLES = ("--branch-a", "--branch-b", "--branch-c", "--branch-d")
# The namespace the Plan page names for its inline SVG; it identifies XML, it is not fetched.
SVG_NAMESPACE = "http://www.w3.org/2000/svg"


def page(name: str) -> str:
    return (PAGES / name).read_text(encoding="utf-8")


def built_page(name: str) -> str:
    """The page as the build embeds it: the shared renderer replaces the placeholder."""
    return page(name).replace(PLACEHOLDER, page("markdown.js").rstrip("\n"))


def markdown_source() -> str:
    match = MARKDOWN_BLOCK.search(page("markdown.js"))
    assert match, "markdown.js must delimit its renderer with BEGIN/END MARKDOWN markers"
    return match.group("code")


def render_markdown(cases: list[str]) -> list[str]:
    harness = (
        "const render = new Function("
        + json.dumps(markdown_source() + "\nreturn islandMarkdown;")
        + ")();\n"
        "let input = '';\n"
        "process.stdin.on('data', (chunk) => { input += chunk; });\n"
        "process.stdin.on('end', () => {\n"
        "  process.stdout.write(JSON.stringify(JSON.parse(input).map(render)));\n"
        "});\n"
    )
    result = subprocess.run(
        [NODE, "-e", harness],
        input=json.dumps(cases),
        capture_output=True,
        text=True,
        check=True,
        timeout=30,
    )
    return json.loads(result.stdout)


class EveryLocalPage(unittest.TestCase):
    def test_pages_exist(self) -> None:
        self.assertEqual(
            [path.name for path in PAGE_FILES],
            ["agent.html", "plan.html", "report.html", "settings.html", "start.html",
             "tab_overview.html"],
        )
        self.assertEqual([path.name for path in SCRIPT_FILES], ["markdown.js"])

    def test_no_page_requests_an_external_resource(self) -> None:
        for path in PAGE_FILES + SCRIPT_FILES:
            text = path.read_text(encoding="utf-8")
            for fragment in FORBIDDEN_FRAGMENTS:
                with self.subTest(page=path.name, fragment=fragment):
                    self.assertNotIn(fragment, text)
            for match in re.finditer(r"https?://[^\s\"'<>)]*", text):
                if match.group(0) == SVG_NAMESPACE:
                    continue  # an XML namespace identifier that createElementNS needs, not a request
                self.fail(f"{path.name} embeds an absolute URL: {match.group(0)}")

    def test_scripts_are_ascii(self) -> None:
        for path in SCRIPT_FILES:
            path.read_text(encoding="utf-8").encode("ascii")

    def test_the_shared_renderer_is_inlined_into_the_pages_that_render_agent_text(self) -> None:
        for name in ("agent.html", "report.html"):
            with self.subTest(page=name):
                self.assertEqual(page(name).count(PLACEHOLDER), 1)
                built = built_page(name)
                self.assertNotIn("@@ISLAND_MARKDOWN@@", built)
                self.assertIn("function islandMarkdown(", built)
                self.assertNotIn("function islandMarkdown(", page(name))
        for name in ("settings.html", "start.html", "tab_overview.html"):
            with self.subTest(page=name):
                self.assertNotIn(PLACEHOLDER, page(name))

    @unittest.skipUnless(NODE, "node is not installed")
    def test_built_page_scripts_parse(self) -> None:
        for path in PAGE_FILES:
            built = built_page(path.name)
            scripts = re.findall(r"<script>(.*?)</script>", built, re.DOTALL)
            self.assertEqual(len(scripts), 1, path.name)
            with self.subTest(page=path.name):
                result = subprocess.run(
                    [NODE, "--check", "-"],
                    input=scripts[0],
                    capture_output=True,
                    text=True,
                    timeout=30,
                )
                self.assertEqual(result.returncode, 0, result.stderr)

    def test_pages_are_ascii_and_speak_the_shared_bridge(self) -> None:
        for path in PAGE_FILES:
            text = path.read_text(encoding="utf-8")
            with self.subTest(page=path.name):
                text.encode("ascii")
                self.assertTrue(text.startswith("<!doctype html>"))
                self.assertIn('"\\u0001island:"', text)
                self.assertIn("window.islandRender = function", text)
                self.assertIn("window.islandFocus = function", text)
                self.assertIn('post({ type: "ready" })', text)

    def test_pages_map_every_theme_key(self) -> None:
        checked_pages = 0
        for path in PAGE_FILES:
            text = path.read_text(encoding="utf-8")
            marker = "function applyTheme(theme)"
            if marker not in text:
                continue
            checked_pages += 1
            apply_theme = text[
                text.index(marker) : text.index("window.islandRender", text.index(marker))
            ]
            with self.subTest(page=path.name):
                for key, variable in THEME_VARIABLES.items():
                    self.assertIn(f'"{key}"', apply_theme)
                    self.assertIn(f'"{variable}"', apply_theme)
                self.assertIn("theme.branch", apply_theme)
                for variable in BRANCH_VARIABLES:
                    self.assertIn(f'"{variable}"', apply_theme)
        self.assertGreater(checked_pages, 0)


class StartPage(unittest.TestCase):
    def setUp(self) -> None:
        self.html = page("start.html")

    def test_has_one_input_and_the_three_modes(self) -> None:
        self.assertEqual(self.html.count("<input"), 1)
        self.assertIn('data-mode="search"', self.html)
        self.assertIn('data-mode="agent"', self.html)
        self.assertIn('data-mode="research"', self.html)
        self.assertIn(">Search</button>", self.html)
        self.assertIn(">Ask Agent</button>", self.html)
        self.assertIn(">Research</button>", self.html)

    def test_research_mode_speaks_the_documented_messages(self) -> None:
        for message in (
            'type: "research", question: text',
            'post({ type: "cancel_research" })',
            "post({ type: action })",
            '"open_report"',
            '"open_settings"',
        ):
            self.assertIn(message, self.html)
        # A refusal never clears what the user typed; only a started task does.
        submit = self.html[self.html.index("function submit()") :]
        submit = submit[: submit.index("render();")]
        self.assertNotIn('input.value = ""', submit)
        self.assertIn('fresh && state.research.state === "running"', self.html)
        self.assertIn("window.islandNote = function", self.html)

    def test_research_status_is_rendered_as_text_not_markup(self) -> None:
        # Questions and agent errors reach the page; they must stay text nodes.
        self.assertNotIn("innerHTML", self.html)

    def test_keeps_the_prompt_until_the_agent_accepts_it(self) -> None:
        # A busy agent refuses the turn; the page must say so instead of
        # clearing the input and claiming it was sent.
        self.assertIn("window.islandAgentResult = function (sent)", self.html)
        self.assertIn("The agent is busy", self.html)
        submit = self.html[self.html.index("function submit()") :]
        submit = submit[: submit.index("render();")]
        self.assertNotIn('input.value = ""', submit)

    def test_posts_the_documented_bridge_messages(self) -> None:
        for message in (
            'type: "search"',
            'type: "ask_agent"',
            'type: "set_start_mode"',
            'type: "research"',
        ):
            self.assertIn(message, self.html)

    def test_never_navigates_by_itself(self) -> None:
        for fragment in ("location", "window.open", "<a ", "<form action", "target="):
            self.assertNotIn(fragment, self.html)

    def test_follows_the_app_theme_with_a_system_fallback(self) -> None:
        self.assertIn("prefers-color-scheme: dark", self.html)
        self.assertIn(':root:not([data-theme])', self.html)
        self.assertIn('setAttribute("data-theme"', self.html)
        for token in ("--bg", "--surface", "--surface-2", "--text", "--text-2", "--border", "--accent"):
            self.assertIn(token + ":", self.html)

    def test_uses_the_native_system_font_stack(self) -> None:
        # Native has no bundled face: the system stack, as settings.html does.
        self.assertIn('-apple-system, BlinkMacSystemFont, "SF Pro Text"', self.html)
        self.assertIn('ui-monospace, "SF Mono"', self.html)
        self.assertNotIn("Geist", self.html)

    def test_placeholders_follow_the_mode_verbatim(self) -> None:
        self.assertIn('placeholder="Search with Google"', self.html)
        self.assertIn('"Search with " + state.engine', self.html)
        self.assertIn('"Ask about this tab or anything"', self.html)
        self.assertIn('"Research a question \\u2014 you\'ll see the plan first"', self.html)

    def test_tab_cycles_the_segments_only_from_the_field(self) -> None:
        self.assertIn('input.addEventListener("keydown"', self.html)
        self.assertIn('event.key !== "Tab"', self.html)
        self.assertIn("event.shiftKey ? -1 : 1", self.html)
        self.assertIn("% MODES.length", self.html)
        self.assertIn("event.preventDefault()", self.html)

    def test_speed_dial_tiles_are_buttons_with_accessible_names(self) -> None:
        self.assertIn('id="tiles"', self.html)
        self.assertIn("state.tiles", self.html)
        self.assertIn('document.createElement("button")', self.html)
        self.assertNotIn('document.createElement("a")', self.html)
        self.assertIn('setAttribute("aria-label"', self.html)
        self.assertIn('type: "open_tile"', self.html)
        # Titles come from the web: text nodes only, and no fetched favicons.
        self.assertNotIn("<img", self.html)
        self.assertNotIn("favicon", self.html.lower())

    def test_tiles_row_hides_when_there_is_no_history(self) -> None:
        self.assertIn('tiles.hidden = !list.length', self.html)

    def test_the_shortcut_list_is_gone_and_the_page_ends_after_the_speed_dial(self) -> None:
        # The Native start page has no shortcut section (spec 4.1, S2-Start).
        self.assertNotIn("shortcuts", self.html)
        self.assertNotIn("<kbd", self.html)
        self.assertNotIn("Cmd/Ctrl", self.html)
        self.assertNotIn("Command palette", self.html)
        self.assertTrue(self.html.index('id="tiles"') < self.html.index("</main>"))

    def test_ip_literal_hosts_get_a_globe_instead_of_a_digit(self) -> None:
        # 127.0.0.1 and [::1] say nothing about the site; a neutral globe stands in for them.
        self.assertIn(r"/^\d{1,3}(\.\d{1,3}){3}$/", self.html)
        self.assertIn('host.charAt(0) === "["', self.html)
        self.assertIn('<template id="globe">', self.html)
        self.assertIn('getElementById("globe")', self.html)
        self.assertIn("isIpLiteral(host)", self.html)


class ReportPage(unittest.TestCase):
    def setUp(self) -> None:
        self.html = page("report.html")
        self.built = built_page("report.html")

    def test_has_the_three_actions(self) -> None:
        self.assertIn(">Copy Markdown</button>", self.html)
        self.assertIn(">Export</button>", self.html)
        self.assertIn(">Continue in agent panel</button>", self.html)
        for message in ("copy_report", "export", "continue_in_agent", '"open_url"'):
            self.assertIn(message, self.html)

    def test_export_menu_offers_csv_json_and_markdown(self) -> None:
        self.assertIn('id="export-menu"', self.html)
        for label in (">CSV (.csv)</button>", ">JSON (.json)</button>", ">Markdown (.md)</button>"):
            self.assertIn(label, self.html)
        for format_name in ("csv", "json", "md"):
            self.assertIn(f'data-format="{format_name}"', self.html)
        # CSV and JSON need a table: they are disabled, with the reason, when there is none.
        self.assertIn("report.has_table", self.html)
        self.assertIn("No table in this report", self.html)
        self.assertIn('id="export-csv"', self.html)
        self.assertIn('id="export-json"', self.html)
        # The menu is neutral: no accent fill on hover or focus.
        menu_css = self.html[self.html.index(".menu button:hover"):]
        self.assertNotIn("var(--accent)", menu_css[:200])

    def test_a_collection_shows_a_table_icon(self) -> None:
        self.assertIn('report.kind === "collection"', self.html)
        self.assertIn('id="kind-icon"', self.html)

    def test_never_navigates_by_itself(self) -> None:
        for fragment in ("location", "window.open", "target=", "<form", "href=\""):
            self.assertNotIn(fragment, self.html)
        # Links go to native, which opens them in new tabs; only http(s) is forwarded.
        self.assertIn("event.preventDefault()", self.html)
        self.assertIn('/^https?:\\/\\//i.test(href)', self.html)

    def test_report_text_only_reaches_the_dom_through_the_safe_renderer(self) -> None:
        self.assertIn("doc.innerHTML = islandMarkdown(report.markdown", self.html)
        self.assertIn("doc.textContent = report.markdown", self.html)
        assignments = re.findall(r"innerHTML\s*=\s*([^;]+);", self.built)
        other = [a for a in assignments if not a.startswith("islandMarkdown(")]
        self.assertEqual(other, [])
        # Titles, questions and dates are text nodes.
        self.assertIn('document.getElementById("title").textContent', self.html)

    def test_follows_the_app_theme_with_a_system_fallback(self) -> None:
        self.assertIn(':root:not([data-theme])', self.html)
        self.assertIn('setAttribute("data-theme"', self.html)
        for token in ("--bg", "--surface", "--surface-2", "--text", "--text-2", "--border", "--accent"):
            self.assertIn(token + ":", self.html)

    def test_reports_the_outcome_of_copy_and_save(self) -> None:
        self.assertIn("window.islandReportResult = function", self.html)
        for kind in ('"copied"', '"saved"', '"cancelled"'):
            self.assertIn(kind, self.html)



class PlanPage(unittest.TestCase):
    def setUp(self) -> None:
        self.html = page("plan.html")

    def test_has_the_toolbar_graph_list_tidy_up_and_the_run_actions(self) -> None:
        for element_id in ("view-graph", "view-list", "tidy", "run", "run-branch", "cancel"):
            with self.subTest(id=element_id):
                self.assertIn(f'id="{element_id}"', self.html)
        self.assertIn("Tidy up", self.html)
        self.assertIn('role="tree"', self.html)

    def test_posts_the_documented_messages(self) -> None:
        for message in ("select", "skip", "restore", "instruction", "branch_from", "expand",
                        "run", "run_branch", "cancel", "view"):
            with self.subTest(message=message):
                self.assertIn(f'"{message}"', self.html)

    def test_places_nodes_on_the_documented_grid(self) -> None:
        self.assertIn("NODE_W = 148", self.html)
        self.assertIn("NODE_H = 92", self.html)
        self.assertIn("PITCH_X = 188", self.html)
        self.assertIn("PITCH_Y = 112", self.html)
        self.assertIn("PAD = 16", self.html)
        self.assertIn("radial-gradient(circle, var(--border-2) 1px", self.html)

    def test_branch_from_is_limited_by_the_depth_the_native_side_sends(self) -> None:
        self.assertIn("node.depth >= state.depth_limit", self.html)
        self.assertIn("Plans branch at most 3 levels deep", self.html)

    def test_graph_builds_an_accessible_add_branch_ghost_for_the_selection(self) -> None:
        self.assertRegex(self.html, r'el\("button", "node add", "\\u002b Add a branch"\)')
        self.assertIn('setAttribute("aria-label", "Add a branch after " + selected.title)', self.html)
        self.assertIn('post({ type: "branch_from", id: selected.id })', self.html)
        self.assertIn("selected.depth >= state.depth_limit", self.html)
        ghost = self.html[self.html.index("function addBranchButton(selected, position)") :]
        ghost = ghost[: ghost.index("function renderList()")]
        self.assertNotIn('setAttribute("role", "treeitem")', ghost)

    def test_drag_offsets_are_page_state_and_tidy_clears_them(self) -> None:
        self.assertIn("var movedPositions = {}", self.html)
        self.assertIn('addEventListener("pointerdown"', self.html)
        self.assertIn('addEventListener("pointermove"', self.html)
        self.assertIn('addEventListener("pointerup"', self.html)
        tidy = self.html[self.html.index('$("tidy").addEventListener') :]
        tidy = tidy[: tidy.index("function zoomBy")]
        self.assertIn("movedPositions = {}", tidy)
        self.assertIn("zoomLevel = 1", tidy)

    def test_pointerup_only_rebuilds_the_canvas_after_a_drag(self) -> None:
        finish = self.html[self.html.index("function finishDrag(event)") :]
        finish = finish[: finish.index('button.addEventListener("pointerup"')]
        self.assertIn("var dragged = activeDrag.dragged", finish)
        self.assertRegex(finish, r"if \(dragged\)\s+renderCanvas\(\);")

    def test_drag_click_suppression_clears_after_the_synthetic_click(self) -> None:
        finish = self.html[self.html.index("function finishDrag(event)") :]
        finish = finish[: finish.index('button.addEventListener("pointerup"')]
        self.assertIn('setTimeout(function () { suppressClickId = ""; }, 0)', finish)

    def test_select_updates_the_existing_nodes_for_reliable_double_click(self) -> None:
        select = self.html[self.html.index("function select(id)") :]
        select = select[: select.index("function neighbourOf(key)")]
        self.assertNotIn("render();", select)
        self.assertIn('setAttribute("aria-selected"', select)

    def test_narrow_layout_stacks_result_below_join_with_a_vertical_edge(self) -> None:
        self.assertIn("function layoutForCanvas(nodes)", self.html)
        self.assertIn('node.kind === "result"', self.html)
        self.assertIn('parent.kind === "join"', self.html)
        self.assertIn('vertical: !!stackedResults[node.id] && parent.kind === "join"', self.html)
        self.assertRegex(self.html, r'edge\.vertical\s*\?\s*"M"')

    def test_running_states_have_spinner_retry_approval_and_edge_motion(self) -> None:
        self.assertIn('className = "spinner"', self.html)
        self.assertIn('data-state="waiting"', self.html)
        self.assertIn('el("div", "approval-card"', self.html)
        self.assertIn('el("button", "tool retry", "Retry")', self.html)
        self.assertIn('post({ type: "retry", id: node.id })', self.html)
        self.assertIn('path.setAttribute("class", "edge running")', self.html)
        reduced = re.search(r"@media \(prefers-reduced-motion: reduce\)\s*\{([^}]|}(?!\s*</style>))*",
                            self.html)
        self.assertIsNotNone(reduced)
        self.assertIn("animation: none", reduced.group(0))

    def test_plan_close_confirmation_uses_the_required_actions(self) -> None:
        self.assertIn("Stop the research?", self.html)
        self.assertIn(">Keep running</button>", self.html)
        self.assertIn(">Stop</button>", self.html)
        self.assertIn('post({ type: "confirm_close", stop: false })', self.html)
        self.assertIn('post({ type: "confirm_close", stop: true })', self.html)
        self.assertIn("window.islandConfirmResearchClose = function", self.html)

    def test_plan_close_confirmation_escape_keeps_running_and_traps_focus(self) -> None:
        keyboard = self.html[self.html.index('document.addEventListener("keydown"') :]
        modal = keyboard[: keyboard.index('(event.metaKey || event.ctrlKey) && event.key === "Enter"')]
        self.assertIn('event.key === "Escape"', modal)
        self.assertIn('$("keep-running").click()', modal)
        self.assertIn('event.key === "Tab"', modal)

    def test_run_shortcut_is_handled_before_inputs_return_early(self) -> None:
        keyboard = self.html[self.html.index('document.addEventListener("keydown"') :]
        shortcut = keyboard.index('(event.metaKey || event.ctrlKey) && event.key === "Enter"')
        input_guard = keyboard.index('event.target.tagName === "INPUT"')
        self.assertLess(shortcut, input_guard)

    def test_toolbar_has_a_graph_icon_and_paused_uses_accent_tokens(self) -> None:
        self.assertIn('class="graph-icon"', self.html)
        awaiting = re.search(r'\.pill\[data-tone="awaiting"\]\s*\{([^}]*)\}', self.html)
        self.assertIsNotNone(awaiting)
        self.assertIn("var(--accent-soft)", awaiting.group(1))
        self.assertIn("var(--accent-text)", awaiting.group(1))

    def test_never_navigates_by_itself(self) -> None:
        self.assertNotIn("window.open", self.html)
        self.assertNotIn("location.href", self.html)
        self.assertNotIn("location.assign", self.html)

    def test_every_button_it_builds_has_an_accessible_name(self) -> None:
        for match in re.finditer(r'el\("button"|createElement\("button"\)|<button', self.html):
            window = self.html[match.start() : match.start() + 400]
            with self.subTest(at=match.group(0)):
                self.assertIn("aria-label", window)

    def test_uses_only_defined_css_variables(self) -> None:
        defined = set(re.findall(r"(--[a-z0-9-]+)\s*:", self.html))
        defined |= set(re.findall(r'"(--[a-z0-9-]+)"', self.html))
        used = set(re.findall(r"var\((--[a-z0-9-]+)", self.html))
        self.assertEqual(used - defined, set())

    def test_text_reaches_the_dom_as_text_not_markup(self) -> None:
        self.assertNotIn("innerHTML", self.html)
        self.assertNotIn("insertAdjacentHTML", self.html)

    def test_follows_the_app_theme_with_a_system_fallback(self) -> None:
        self.assertIn("prefers-color-scheme: dark", self.html)
        self.assertIn('[data-theme="dark"]', self.html)


class SettingsPage(unittest.TestCase):
    def test_offers_the_search_engine_setting(self) -> None:
        html = page("settings.html")
        self.assertIn('id="engine"', html)
        self.assertIn('type: "set_search_engine"', html)

    def test_offers_local_history_controls(self) -> None:
        html = page("settings.html")
        self.assertIn('id="history-remember"', html)
        self.assertIn('id="history-clear"', html)
        self.assertIn('type: "set_remember_history"', html)
        self.assertIn('type: "clear_history"', html)

    def test_offers_reader_appearance_controls(self) -> None:
        html = page("settings.html")
        self.assertIn('<section id="reader">', html)
        self.assertIn('id="reader-size"', html)
        self.assertIn('id="reader-width"', html)
        self.assertIn('id="reader-theme"', html)
        self.assertIn('type: "set_reader_settings"', html)

    def test_lists_reports_with_open_and_delete(self) -> None:
        html = page("settings.html")
        self.assertIn('id="reports-list"', html)
        self.assertIn('type: "open_report"', html)
        self.assertIn('type: "delete_report"', html)


    def test_settings_shell_has_a_section_list_search_and_version(self) -> None:
        html = page("settings.html")
        self.assertIn('<nav class="pane-nav"', html)
        self.assertIn('aria-label="Settings sections"', html)
        for pane in ("general", "appearance", "privacy", "tabs", "reader", "agent", "shortcuts", "about"):
            with self.subTest(pane=pane):
                self.assertIn(f'data-pane="{pane}"', html)
                self.assertIn(f'<section id="{pane}">', html)
        # The selected item is announced with aria-current, set by the script.
        self.assertIn('setAttribute("aria-current", "page")', html)
        # Up and Down move through the list like a native source list.
        self.assertIn('"ArrowDown"', html)
        self.assertIn('"ArrowUp"', html)
        self.assertIn('<input id="settings-search" type="search"', html)
        self.assertIn('placeholder="Search settings"', html)
        self.assertIn('id="version"', html)
        self.assertIn('<section id="reader">', html)  # existing anchors stay

    def test_appearance_follows_the_native_mockup(self) -> None:
        html = page("settings.html")
        for group in (">Theme<", ">Color<", ">Layout<", ">Text<"):
            with self.subTest(group=group):
                self.assertIn(group, html)
        self.assertIn(">Match system</button>", html)
        self.assertIn('id="space-colors-row"', html)
        self.assertIn('id="tint-row"', html)
        self.assertIn("base_look", html)  # Native hides the space-color rows
        self.assertIn("Changes apply right away to every window.", html)
        # The space copy is only for looks that have space colors.
        self.assertNotIn('<p class="lead">Each space tints', html)
        self.assertIn("Each space tints the sidebar", html)
        self.assertIn('id="reset-appearance"', html)
        self.assertIn("var(--fill", html)
        self.assertIn("var(--border-2", html)
        self.assertIn("var(--accent-soft", html)

    def test_a_focus_request_is_deduplicated_by_its_sequence_number(self) -> None:
        # The same section can be requested twice in a row (privacy shield, click, shield again),
        # so the page compares the native sequence number, not the section name.
        html = page("settings.html")
        self.assertIn("s.focus_seq", html)
        self.assertIn("lastFocusSeq", html)
        self.assertNotIn("s.focus !== lastFocus", html)

    def test_search_matches_section_names_and_keeps_the_whole_pane(self) -> None:
        html = page("settings.html")
        search = html[html.index("function paneLabel("):html.index("// ---- Settings rows")]
        # A pane whose nav label or title matches is shown whole.
        self.assertIn('.pane-title', search)
        self.assertIn('data-pane', search)
        self.assertIn("paneMatch", search)

    def test_search_treats_row_less_groups_as_one_searchable_unit(self) -> None:
        # The Import source cards and the About list have no .row, so they match as a group.
        html = page("settings.html")
        search = html[html.index("function paneLabel("):html.index("// ---- Settings rows")]
        self.assertIn("rows.length === 0", search)
        self.assertIn("group.textContent", search)
        self.assertIn('id="sources"', html)
        self.assertIn('id="about-list"', html)

    def test_selected_section_is_filled_with_semibold_text(self) -> None:
        html = page("settings.html")
        rule = re.search(r'\.nav-item\[aria-current="page"\]\s*\{([^}]*)\}', html)
        self.assertIsNotNone(rule)
        self.assertIn("var(--fill)", rule.group(1))
        self.assertIn("font-weight: 600", rule.group(1))
        self.assertNotIn("var(--selection)", rule.group(1))

    def test_search_fields_use_the_interface_font_not_monospace(self) -> None:
        html = page("settings.html")
        rule = re.search(r"input\.field\.filter\s*\{([^}]*)\}", html)
        self.assertIsNotNone(rule)
        self.assertIn("font: inherit", rule.group(1))
        self.assertIn('id="settings-search" type="search"', html)

    def test_shortcuts_are_two_columns_of_grouped_cards(self) -> None:
        html = page("settings.html")
        self.assertIn("grid-template-columns: repeat(2, minmax(0, 1fr))", html)
        for group in ("Tabs", "Navigation", "Agent", "Spaces", "Window"):
            with self.subTest(group=group):
                self.assertIn(f'"{group}"', html)
        self.assertIn("Search shortcuts", html)
        self.assertIn('id="key-filter"', html)

    def test_shortcuts_record_resolve_conflicts_and_reset_all(self) -> None:
        html = page("settings.html")
        self.assertIn('type: "record_shortcut"', html)
        self.assertIn('type: "replace_shortcut"', html)
        self.assertIn('type: "cancel_shortcut"', html)
        self.assertIn('type: "reset_shortcuts"', html)
        self.assertNotIn('post({ type: "set_binding", action: recording', html)
        self.assertIn('id="key-conflict"', html)
        self.assertIn("var(--warn-bg", html)
        self.assertIn(r"Press keys\u2026", html)
        self.assertIn('"Add shortcut"', html)
        self.assertIn(">Reset all<", html)
        self.assertIn('id="mac-note"', html)
        keycap = re.search(r"\.keycap \{[^}]*\}", html)
        self.assertIsNotNone(keycap)
        self.assertIn("min-width: 24px", keycap.group(0))
        self.assertIn("border-radius: 5px", keycap.group(0))
        self.assertIn("var(--border-2", keycap.group(0))


class AgentTasksStrip(unittest.TestCase):
    def setUp(self) -> None:
        self.html = page("agent.html")

    def test_renders_the_strip_with_move_remove_and_stop_controls(self) -> None:
        self.assertIn('id="tasks"', self.html)
        for glyph in ("\\u2191", "\\u2193", "\\u2715"):
            with self.subTest(glyph=glyph):
                self.assertIn(glyph, self.html)
        self.assertIn('"Stop"', self.html)
        self.assertIn('"Clear finished"', self.html)

    def test_posts_the_documented_task_messages(self) -> None:
        self.assertRegex(self.html, r'type: "task_move", id: task\.id, dir: "up"')
        self.assertRegex(self.html, r'type: "task_move", id: task\.id, dir: "down"')
        self.assertIn('type: "task_remove", id: task.id', self.html)
        self.assertIn('type: "task_stop"', self.html)
        self.assertIn('type: "tasks_clear_finished"', self.html)

    def test_a_queued_message_shows_its_place_in_the_queue(self) -> None:
        self.assertIn('"Queued \\u00b7 #"', self.html)

    def test_the_composer_queues_instead_of_blocking_while_a_task_runs(self) -> None:
        self.assertNotIn("if (!(state.session && state.session.busy)) submit", self.html)
        self.assertIn('"Queue (Enter)"', self.html)

    def test_strip_text_is_set_as_text_and_uses_theme_variables_only(self) -> None:
        node = self.html[self.html.index("function taskNode") :][:1800]
        self.assertNotIn("innerHTML", node)
        css = self.html[self.html.index("/* Tasks strip") :][:2400]
        css = css[: css.index("/* Composer */")]
        self.assertNotRegex(css, r"#[0-9a-fA-F]{3,8}\b")
        self.assertNotRegex(css, r"rgba?\(")


class AgentContextChip(unittest.TestCase):
    def setUp(self) -> None:
        self.html = page("agent.html")

    def test_the_page_chip_has_a_clear_button_and_a_quote_chip(self) -> None:
        for fragment in ('id="context-clear"', 'id="quote-chip"', "state.context_chip"):
            self.assertIn(fragment, self.html)

    def test_clear_posts_clear_context(self) -> None:
        clear = self.html[self.html.index('getElementById("context-clear").addEventListener') :]
        clear = clear[: clear.index("});")]
        self.assertIn('type: "clear_context"', clear)

    def test_the_composer_focus_is_the_border_not_an_accent_ring(self) -> None:
        self.assertRegex(self.html, r"\.composer textarea:focus-visible\s*\{\s*outline:\s*none")
        self.assertRegex(self.html, r"\.composer:focus-within\s*\{\s*border-color:\s*var\(--text-2\)")
        for selector in (".send:focus-visible", ".context-clear:focus-visible"):
            rule = re.search(re.escape(selector) + r"\s*\{([^}]*)\}", self.html)
            self.assertIsNotNone(rule, selector)
            self.assertIn("var(--text-2)", rule.group(1))
            self.assertNotIn("var(--accent)", rule.group(1))
            self.assertIn("border-radius", rule.group(1))

    def test_the_quote_is_set_as_text(self) -> None:
        self.assertIn('getElementById("quote-text").textContent = quote', self.html)


class AgentConfirmationCard(unittest.TestCase):
    def setUp(self) -> None:
        self.html = page("agent.html")

    def test_renders_the_confirm_card_with_its_four_buttons(self) -> None:
        self.assertIn("confirm-card", self.html)
        for label in ("Allow once", "Allow for this task", "Trust this site", "Cancel"):
            with self.subTest(label=label):
                self.assertIn('"' + label + '"', self.html)

    def test_trust_and_task_are_offered_only_when_trusted_is_allowed(self) -> None:
        self.assertIn("trusted_allowed", self.html)
        self.assertRegex(self.html, r"if \(item\.trusted_allowed[^)]*\)")

    def test_posts_the_documented_confirm_message(self) -> None:
        self.assertIn('type: "confirm"', self.html)
        for answer in ("once", "task", "trust", "cancel"):
            with self.subTest(answer=answer):
                self.assertIn('"' + answer + '"', self.html)

    def test_escape_cancels_the_card_before_stopping_the_turn(self) -> None:
        self.assertRegex(
            self.html, r'post\(\{ type: "confirm", id: pending\[0\]\.id, answer: "cancel" \}\)'
        )
        self.assertRegex(
            self.html,
            r'event\.key === "Escape" && menu\.hidden && !event\.isComposing'
            r' && cancelFirstConfirmation\(\)[\s\S]{0,120}\}, true\)',
        )

    def test_card_is_titled_per_spec_and_never_takes_focus(self) -> None:
        self.assertIn('"Allow this action?"', self.html)
        render = self.html[self.html.index("confirmSignature !== confirmRendered") :][:700]
        self.assertNotIn(".focus()", render)

    def test_card_text_is_set_as_text_and_uses_theme_variables_only(self) -> None:
        node = self.html[self.html.index("function confirmationNode") :][:1200]
        self.assertNotIn("innerHTML", node)
        css = self.html[self.html.index(".confirm-card") :][:700]
        self.assertNotRegex(css, r"#[0-9a-fA-F]{3,8}\b")
        self.assertNotRegex(css, r"rgba?\(")


class AgentCollectSheet(unittest.TestCase):
    def setUp(self) -> None:
        self.html = page("agent.html")

    def test_sheet_asks_what_to_collect_with_the_example_placeholder(self) -> None:
        self.assertIn('id="collect-what"', self.html)
        self.assertIn('placeholder="price, rating and delivery time for each product"', self.html)
        self.assertIn('id="collect-scrim"', self.html)
        self.assertIn('role="dialog"', self.html)

    def test_tab_checklist_leaves_pinned_tabs_unchecked(self) -> None:
        self.assertIn('box.checked = !tab.pinned', self.html)
        self.assertIn('type: "collect"', self.html)
        self.assertIn('"collect" in next', self.html)
        self.assertIn("collect.seq === collectSeq", self.html)

    def test_submit_needs_a_question_and_a_tab(self) -> None:
        self.assertIn("!collectWhat.value.trim() || !collectChecked().length", self.html)
        self.assertIn('id="collect-submit" type="submit" disabled', self.html)

    def test_checkboxes_and_focus_are_neutral_not_blue(self) -> None:
        self.assertIn("accent-color: var(--text)", self.html)
        sheet_css = self.html[self.html.index(".collect-scrim {"):self.html.index("</style>")]
        self.assertNotIn("var(--accent", sheet_css)
        self.assertNotIn("var(--selection", sheet_css)

    def test_tab_titles_are_text_nodes(self) -> None:
        self.assertIn("title.textContent = tab.title || tab.url", self.html)
        self.assertIn("url.textContent = tab.url", self.html)


class SettingsTrustedSites(unittest.TestCase):
    def setUp(self) -> None:
        self.html = page("settings.html")

    def test_lists_trusted_sites_with_remove_buttons_and_the_documented_copy(self) -> None:
        self.assertIn("Trusted sites", self.html)
        self.assertIn(
            "The agent can submit forms and run scripts on these sites without asking. "
            "It never types passwords.",
            self.html,
        )
        self.assertIn('type: "remove_trusted_site"', self.html)
        self.assertIn("trusted_sites", self.html)

    def test_remove_buttons_have_accessible_names(self) -> None:
        window = self.html[self.html.index("renderTrustedSites") :][:1200]
        self.assertIn("aria-label", window)


class AgentMarkdownStatic(unittest.TestCase):
    def test_renderer_escapes_before_emitting_and_allows_three_schemes(self) -> None:
        source = markdown_source()
        self.assertIn("function esc(", source)
        self.assertIn("&lt;", source)
        self.assertIn("(https?:\\/\\/|mailto:)", source)
        self.assertIn('rel="noopener noreferrer"', source)
        self.assertNotIn("eval(", source)
        self.assertNotIn("document.write", source)

    def test_panel_falls_back_to_plain_text(self) -> None:
        html = page("agent.html")
        self.assertIn("root.innerHTML = islandMarkdown(text)", html)
        self.assertIn("root.textContent = text", html)
        # innerHTML is only assigned the renderer's output or a constant icon.
        assignments = re.findall(r"innerHTML\s*=\s*([^;]+);", html)
        other = [
            a for a in assignments
            if not (a.startswith(("islandMarkdown(", "busy ?")) or re.fullmatch(r"[A-Z_]+", a.strip()))
        ]
        self.assertEqual(other, [])


@unittest.skipUnless(NODE, "node is not installed")
class AgentMarkdownBehavior(unittest.TestCase):
    def assert_only_generated_markup(self, html: str) -> None:
        for match in TAG.finditer(html):
            self.assertIn(match.group(2).lower(), ALLOWED_TAGS, html)
            for attribute in re.findall(r"\s([a-zA-Z-]+)=", match.group(3)):
                self.assertIn(attribute, ALLOWED_ATTRIBUTES, html)
        # Every '<' that is markup was matched as a generated tag.
        stripped = TAG.sub("", html)
        self.assertNotIn("<", stripped, html)

    def test_html_from_the_model_is_escaped(self) -> None:
        cases = [
            "<script>alert(1)</script>",
            '<img src=x onerror="alert(1)">',
            "<b>bold</b> & <i>it</i>",
            "text </p><script>x</script>",
            "```\n<script>alert(1)</script>\n```",
            "`<img src=x onerror=alert(1)>`",
            "# <script>alert(1)</script>",
            "> <iframe src=x></iframe>",
            "- <svg onload=alert(1)>",
            "| <b>a</b> | b |\n|---|---|\n| <u>1</u> | 2 |",
            "**<script>**",
            "[<img src=x onerror=alert(1)>](https://a.test)",
        ]
        for case, html in zip(cases, render_markdown(cases)):
            with self.subTest(case=case):
                self.assert_only_generated_markup(html)
                self.assertNotIn("<script", html)
                self.assertNotIn("<img", html)
                self.assertNotIn("<iframe", html)
                self.assertNotIn("<svg", html)
                self.assertNotIn("<b>", html)

    def test_unsafe_link_schemes_are_dropped_and_safe_ones_are_hardened(self) -> None:
        cases = [
            "[x](javascript:alert(1))",
            "[x](JaVaScRiPt:alert(1))",
            "[x](data:text/html,<script>1</script>)",
            "[x](file:///etc/passwd)",
            "[x](vbscript:1)",
            "[x]( javascript:alert(1))",
            "<javascript:alert(1)>",
            "[x](https://ok.test/a?b=1&c=2)",
            "[x](mailto:a@b.test)",
            "https://bare.test/path.",
            '[x](https://a.test/" onclick="alert(1))',
        ]
        out = dict(zip(cases, render_markdown(cases)))
        for case in cases[:7]:
            with self.subTest(case=case):
                self.assertNotIn("<a ", out[case])
                self.assertNotIn("href=", out[case])
        for case in cases[7:]:
            with self.subTest(case=case):
                self.assert_only_generated_markup(out[case])
                self.assertIn('rel="noopener noreferrer"', out[case])
                self.assertNotIn("onclick=\"", out[case])
        self.assertIn('href="https://ok.test/a?b=1&amp;c=2"', out[cases[7]])
        self.assertIn('href="mailto:a@b.test"', out[cases[8]])
        self.assertIn(">https://bare.test/path</a>.", out[cases[9]])
        for html in out.values():
            for href in re.findall(r'href="([^"]*)"', html):
                self.assertRegex(href, r"^(https?://|mailto:)")

    def test_blocks_render_to_the_expected_structure(self) -> None:
        document = "\n".join(
            [
                "# Title",
                "",
                "Plain **bold**, *italic*, ~~gone~~ and `code`.",
                "Second line.",
                "",
                "- one",
                "- two",
                "  - nested",
                "",
                "3. three",
                "4. four",
                "",
                "> quoted *text*",
                "",
                "---",
                "",
                "```python",
                "print('<hi>')",
                "```",
                "",
                "| A | B |",
                "|:--|--:|",
                "| 1 | 2 |",
            ]
        )
        (html,) = render_markdown([document])
        self.assert_only_generated_markup(html)
        for expected in (
            "<h1>Title</h1>",
            "<strong>bold</strong>",
            "<em>italic</em>",
            "<del>gone</del>",
            "<code>code</code>",
            "Plain",
            "<br>Second line.",
            "<ul><li>one</li><li>two<ul><li>nested</li></ul></li></ul>",
            '<ol start="3"><li>three</li><li>four</li></ol>',
            "<blockquote><p>quoted <em>text</em></p></blockquote>",
            "<hr>",
            "<pre><code>print(&#39;&lt;hi&gt;&#39;)</code></pre>",
            '<th style="text-align:left">A</th>',
            '<td style="text-align:right">2</td>',
        ):
            self.assertIn(expected, html)

    def test_partial_and_odd_input_never_throws(self) -> None:
        cases = [
            "",
            "```",
            "```js\nunterminated",
            "**",
            "[",
            "[a](",
            "| a |",
            "|---|",
            "> ",
            "1.",
            "- ",
            "#",
            "\\",
            "\x00\x01",
            "a" * 5000,
            "*" * 500,
            "[" * 200 + "]" * 200,
        ]
        for case, html in zip(cases, render_markdown(cases)):
            with self.subTest(case=case[:20]):
                self.assertIsInstance(html, str)
                self.assert_only_generated_markup(html)


if __name__ == "__main__":
    unittest.main()
