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
            ["agent.html", "bookmarks.html", "plan.html", "report.html", "settings.html",
             "start.html", "tab_overview.html"],
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
        for name in ("bookmarks.html", "settings.html", "start.html", "tab_overview.html"):
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


class BookmarksPage(unittest.TestCase):
    def setUp(self) -> None:
        self.html = page("bookmarks.html")

    def test_documents_the_native_state_and_message_table(self) -> None:
        for key in (
            "folders:", "bookmarks:", "current_space:", "selected_folder:",
            "import_sources:", "status:", "agent_naming?:bool",
        ):
            self.assertIn(key, self.html)
        for message in (
            'type: "bookmarks:open"', 'type: "bookmarks:open-new-tab"',
            'type: "bookmarks:edit"', 'type: "bookmarks:delete"',
            'type: "bookmarks:move"', 'type: "folder:create"',
            'type: "folder:rename"', 'type: "folder:delete"',
            'type: "favorite:toggle-global"', 'type: "favorite:toggle-space"',
            'type: "import"', 'type: "retry"', 'type: "select-folder"',
        ):
            with self.subTest(message=message):
                self.assertIn(message, self.html)

    def test_uses_theme_tokens_and_neutral_accessible_states(self) -> None:
        stylesheet = self.html[self.html.index("<style>") : self.html.index("</style>")]
        self.assertNotRegex(stylesheet, r"#[0-9a-fA-F]{3,8}|rgb|hsl")
        self.assertIn(":focus-visible", stylesheet)
        self.assertIn("box-shadow: 0 0 0 2px var(--text-2)", stylesheet)
        self.assertIn(".search:focus-within", stylesheet)
        self.assertIn("@media (prefers-reduced-motion: reduce)", stylesheet)
        self.assertIn("var(--danger)", stylesheet)

    def test_has_live_status_retry_and_folder_delete_confirmation(self) -> None:
        self.assertIn('role="status" aria-live="polite"', self.html)
        self.assertIn('id="retry">Retry</button>', self.html)
        self.assertIn('role="dialog" aria-modal="true"', self.html)
        self.assertIn("Move bookmarks to Unfiled", self.html)
        self.assertIn("Delete bookmarks permanently?", self.html)
        self.assertIn("function trapConfirm", self.html)

    def test_keeps_editing_and_agent_naming_safe(self) -> None:
        self.assertIn('event.key === "Escape"', self.html)
        self.assertIn("state.agent_naming", self.html)
        self.assertIn("agent.disabled = true", self.html)
        self.assertIn('aria-current", "page"', self.html)

    @unittest.skipUnless(NODE, "node is not installed")
    def test_node_harness_keeps_edit_draft_focus_and_two_step_delete_safe(self) -> None:
        """The page script is run against a small DOM harness, not just parsed."""
        script = re.search(r"<script>(.*?)</script>", self.html, re.DOTALL).group(1)
        harness = r'''
class Node {
  constructor(tag) { this.tagName = tag; this.children = []; this.listeners = {}; this.dataset = {};
    this.style = { setProperty() {} }; this.attributes = {}; this.hidden = false; this.value = "";
    this.textContent = ""; this.className = ""; this.type = ""; }
  appendChild(node) { this.children.push(node); return node; }
  addEventListener(type, fn) { (this.listeners[type] ||= []).push(fn); }
  dispatch(type, event = {}) { for (const fn of this.listeners[type] || []) fn({ preventDefault() {}, ...event }); }
  setAttribute(key, value) { this.attributes[key] = String(value); }
  getAttribute(key) { return this.attributes[key]; }
  querySelector(selector) { return find(this, selector); }
  querySelectorAll(selector) { return findAll(this, selector); }
  focus() { document.activeElement = this; }
  select() { this.selected = true; }
}
function hasClass(node, name) { return node.className.split(/\s+/).includes(name); }
function match(node, selector) {
  if (selector === "button" || selector === "input") return node.tagName === selector;
  if (selector === ".edit") return hasClass(node, "edit");
  if (selector === ".folder-tools") return hasClass(node, "folder-tools");
  if (selector === '[data-edit-field="title"]') return node.dataset.editField === "title";
  if (selector === '[data-edit-field="url"]') return node.dataset.editField === "url";
  if (selector === "button:not([disabled])") return node.tagName === "button" && !node.disabled;
  return false;
}
function findAll(node, selector) { let out = []; for (const child of node.children) { if (match(child, selector)) out.push(child); out = out.concat(findAll(child, selector)); } return out; }
function find(node, selector) {
  if (selector === ".folder-tools input") { const tools = find(node, ".folder-tools"); return tools && find(tools, "input"); }
  return findAll(node, selector)[0] || null;
}
const ids = {}; const document = { activeElement: null, documentElement: new Node("html"),
  createElement(tag) { return new Node(tag); }, getElementById(id) { return ids[id]; },
  querySelector(selector) { for (const node of Object.values(ids)) { if (match(node, selector)) return node.querySelector(selector) || (match(node, selector) ? node : null); } return null; },
  listeners: {}, addEventListener(type, fn) { (this.listeners[type] ||= []).push(fn); },
  dispatch(type, event = {}) { for (const fn of this.listeners[type] || []) fn({ preventDefault() {}, ...event }); } };
for (const id of ["query", "new-folder", "new-folder-name", "folder-list", "folder-total", "bookmark-list", "results-title", "result-count", "empty", "import-menu", "import-button", "status", "retry", "confirm-title", "confirm-copy", "confirm-move", "confirm-delete", "confirm-backdrop", "confirm-cancel", "new-folder-button", "cancel-folder"]) ids[id] = new Node(id === "query" || id.includes("name") ? "input" : "div");
ids.status.appendChild(new Node("span")); ids["confirm-backdrop"].appendChild(ids["confirm-cancel"]); ids["confirm-backdrop"].appendChild(ids["confirm-move"]); ids["confirm-backdrop"].appendChild(ids["confirm-delete"]);
const messages = []; const console = { log(value) { messages.push(JSON.parse(value.slice(8))); } }; const window = {}; global.setTimeout = (fn) => fn();
'''
        result = subprocess.run(
            [NODE, "-e", harness + script + r'''
const base = { folders: [{ id: "f", name: "Folder", count: 1 }], bookmarks: [{ id: "b", title: "Old", url: "https://old.test", host: "old.test", folder_id: "f", spaces: [] }], selected_folder: "all", import_sources: [] };
window.islandRender(base);
findAll(ids["bookmark-list"], "button").find((node) => node.textContent === "Edit").dispatch("click");
let edit = find(ids["bookmark-list"], ".edit"); let title = edit.querySelector('[data-edit-field="title"]'); let url = edit.querySelector('[data-edit-field="url"]');
url.value = "https://draft.test"; url.focus(); url.dispatch("input"); window.islandRender(base);
edit = find(ids["bookmark-list"], ".edit"); url = edit.querySelector('[data-edit-field="url"]');
if (url.value !== "https://draft.test" || document.activeElement !== url) throw new Error("draft or URL focus lost");
findAll(ids["folder-list"], "button").find((node) => node.textContent === "More").dispatch("click");
findAll(find(ids["folder-list"], ".folder-tools"), "button").find((node) => node.textContent === "Delete").dispatch("click");
ids["confirm-delete"].dispatch("click"); ids["confirm-delete"].dispatch("click");
if (messages.filter((message) => message.type === "folder:delete" && message.mode === "delete").length !== 1) throw new Error("delete did not send exactly once");
findAll(ids["folder-list"], "button").find((node) => node.textContent === "Hide").dispatch("click");
findAll(ids["folder-list"], "button").find((node) => node.textContent === "More").dispatch("click");
let rename = find(ids["folder-list"], ".folder-tools").querySelector("input");
rename.value = "Folder draft"; rename.focus(); rename.dispatch("input"); window.islandRender(base);
rename = find(ids["folder-list"], ".folder-tools").querySelector("input");
if (rename.value !== "Folder draft" || document.activeElement !== rename) throw new Error("folder draft or focus lost");
findAll(find(ids["folder-list"], ".folder-tools"), "button").find((node) => node.textContent === "Delete").dispatch("click");
ids["confirm-cancel"].dispatch("click");
findAll(ids["folder-list"], "button").find((node) => node.textContent === "Hide").dispatch("click");
findAll(ids["folder-list"], "button").find((node) => node.textContent === "More").dispatch("click");
findAll(find(ids["folder-list"], ".folder-tools"), "button").find((node) => node.textContent === "Delete").dispatch("click");
document.dispatch("keydown", { key: "Escape" });
if (messages.filter((message) => message.type === "folder:delete").length !== 1) throw new Error("cancel or escape sent delete");
'''],
            capture_output=True,
            text=True,
            timeout=30,
        )
        self.assertEqual(result.returncode, 0, result.stderr)


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

    def test_native_labels_are_sentence_case_system_text(self) -> None:
        self.assertIn('root.setAttribute("data-look", theme.look || "")', self.html)
        native = re.search(
            r':root\[data-look="native"\] \.label,\s*'
            r':root\[data-look="native"\] \.doc thead th\s*\{([^}]*)\}',
            self.html,
        )
        self.assertIsNotNone(native)
        self.assertIn("font: 600 11px/1.4 var(--font)", native.group(1))
        self.assertIn("text-transform: none", native.group(1))

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
        # <svg> has no `hidden` IDL property: assigning it would leave the icon visible.
        self.assertIn('toggleAttribute("hidden", !collection)', self.html)
        self.assertNotIn("kind-icon\").hidden", self.html)

    def test_never_navigates_by_itself(self) -> None:
        for fragment in ("location", "window.open", "target=", "<form", "href=\""):
            self.assertNotIn(fragment, self.html)
        # Links go to native, which opens them in new tabs; only http(s) is forwarded.
        self.assertIn("event.preventDefault()", self.html)
        self.assertIn('/^https?:\\/\\//i.test(href)', self.html)

    def test_a_first_h1_that_repeats_the_title_is_dropped_from_the_view_only(self) -> None:
        self.assertIn("dropRepeatedTitle(report.title)", self.html)
        drop = self.html[self.html.index("function dropRepeatedTitle") :][:420]
        self.assertIn('doc.querySelector("h1")', drop)
        self.assertIn("heading.remove()", drop)
        self.assertIn("clean(heading.textContent) === clean(title)", drop)
        # Copy and Export read the report's Markdown, not the rendered view, so they keep the H1.
        self.assertIn('act("copy_report")', self.html)
        self.assertNotIn("heading", self.html[self.html.index("function act(") :][:200])

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

    def test_native_kickers_and_legend_are_sentence_case_system_text(self) -> None:
        self.assertIn('root.setAttribute("data-look", theme.look || "")', self.html)
        kicker = re.search(
            r':root\[data-look="native"\] \.kicker\s*\{([^}]*)\}', self.html
        )
        legend = re.search(
            r':root\[data-look="native"\] \.legend\s*\{([^}]*)\}', self.html
        )
        self.assertIsNotNone(kicker)
        self.assertIsNotNone(legend)
        self.assertIn("font: 700 10px/1.3 var(--font)", kicker.group(1))
        self.assertIn("text-transform: none", kicker.group(1))
        self.assertIn("font: 700 10px/1.3 var(--font)", legend.group(1))

    def test_selected_node_and_row_use_a_neutral_plate_not_the_accent(self) -> None:
        # Selection is a neutral plate (the owner's rule: accent only marks the active tab, the
        # address focus ring, the agent's bubble and send button), so no blue ring or fill.
        node = re.search(r'\.node\[aria-selected="true"\]\s*\{([^}]*)\}', self.html)
        row = re.search(r'\.row\[aria-selected="true"\]\s*\{([^}]*)\}', self.html)
        self.assertIsNotNone(node)
        self.assertIsNotNone(row)
        for rule in (node.group(1), row.group(1)):
            with self.subTest(rule=rule):
                self.assertIn("var(--segment-selected)", rule)
                self.assertNotIn("--selection", rule)
                self.assertNotIn("--accent", rule)
        self.assertNotIn("#FFFFFF", row.group(1))

    def test_join_chip_deduplicates_branch_letters(self) -> None:
        join = self.html[self.html.index('if (node.kind === "join")') :]
        join = join[: join.index('button.appendChild(chips)')]
        self.assertIn("var letters = []", join)
        self.assertIn("if (letters.indexOf(letter) < 0) letters.push(letter)", join)
        self.assertIn('"Joins " + letters.join(" + ")', join)

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

    def test_primary_and_selected_states_are_neutral_not_the_accent(self) -> None:
        # Owner rule: accent marks only the active tab, the address focus ring, the agent's bubble
        # and the send button. Primary buttons, the selected radio, "on" switches, the range
        # thumb and the selected swatch use the text colour (or a neutral plate), in every look.
        html = page("settings.html")
        css = html[html.index("<style>") : html.index("</style>")]
        selected = re.compile(
            r'\.primary|:checked|\[aria-pressed="true"\]|\.selected|\[aria-current|type="range"'
        )
        checked = 0
        for match in re.finditer(r"([^{}]+)\{([^{}]*)\}", css):
            selector, body = match.group(1).strip(), match.group(2)
            if not selected.search(selector):
                continue
            checked += 1
            with self.subTest(selector=selector):
                self.assertNotIn("var(--accent", body)
                self.assertNotIn("var(--selection", body)
        self.assertGreaterEqual(checked, 5)
        self.assertIn(".btn.primary { background: var(--text); border-color: var(--text);", html)
        self.assertIn('input[type="range"] { width: 180px; accent-color: var(--text); }', html)


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

    def test_every_task_row_states_its_status_in_words(self) -> None:
        # "Running · 3/7", "Queued · #1", "Needs you": colour is never the only cue.
        self.assertIn('meta = "Running \\u00b7 " + task.done + "/" + task.total', self.html)
        self.assertIn('meta = "Needs you \\u00b7 " + task.done + "/" + task.total', self.html)
        self.assertIn('meta = "Queued \\u00b7 #" + task.position', self.html)
        self.assertIn('waiting: "Needs you"', self.html)

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
        self.assertRegex(self.html, r"textarea:focus-visible\s*\{\s*outline:\s*none")
        self.assertRegex(self.html, r"\.composer:focus-within\s*\{\s*border-color:\s*var\(--text-2\)")
        # Buttons take a neutral 2 px ring (a box-shadow that follows the radius), never blue.
        focus = re.search(r"button:focus-visible, a:focus-visible, input:focus-visible\s*\{([^}]*)\}",
                          self.html)
        self.assertIsNotNone(focus)
        self.assertIn("outline: none", focus.group(1))
        self.assertIn("box-shadow: 0 0 0 2px var(--text-2)", focus.group(1))
        self.assertNotIn("var(--accent)", focus.group(1))

    def test_the_clear_button_sits_inside_the_page_chip_plate(self) -> None:
        # The ✕ belongs to the page chip: one plate holds the share toggle and the clear button,
        # and the clear button is never nested inside the toggle (that would be invalid HTML).
        start = self.html.index('class="context-plate"')
        plate = self.html[start : self.html.index("</div>", start)]
        self.assertIn('id="context"', plate)
        self.assertIn('id="context-clear"', plate)
        toggle = plate[plate.index('id="context"') : plate.index("</button>", plate.index('id="context"'))]
        self.assertNotIn('id="context-clear"', toggle)
        self.assertRegex(self.html, r"\.context-plate \{[^}]*background: var\(--fill\)")
        self.assertRegex(self.html, r"\.context \{[^}]*background: transparent")

    def test_the_quote_is_set_as_text(self) -> None:
        self.assertIn('getElementById("quote-text").textContent = quote', self.html)


def agent_component_css(html: str) -> str:
    """The agent page's CSS without its two pre-theme :root fallback blocks."""
    css = html[html.index("<style>") : html.index("</style>")]
    return re.sub(r":root[^{]*\{[^}]*\}", "", css)


class AgentPanelLook(unittest.TestCase):
    def setUp(self) -> None:
        self.html = page("agent.html")

    def test_the_header_pill_has_amber_working_idle_and_plain_tones(self) -> None:
        self.assertIn('class="status-pill" id="status-pill" data-tone="plain"', self.html)
        self.assertRegex(self.html, r"\.status-pill\[data-tone=\"warn\"\][^{]*\{[^}]*var\(--warn-bg\)")
        self.assertIn('"Waiting for approval"', self.html)
        self.assertIn('"Working"', self.html)
        self.assertIn('"Idle"', self.html)
        self.assertIn('pills.setAttribute("data-tone", pill.tone)', self.html)
        # The existing wording of the error and not-running states is kept.
        self.assertIn("pill = { tone: \"plain\", text: session.status || \"\" }", self.html)

    def test_the_send_button_is_round_and_takes_the_accent(self) -> None:
        rule = re.search(r"\n\.send \{([^}]*)\}", self.html)
        self.assertIsNotNone(rule)
        self.assertIn("border-radius: 50%", rule.group(1))
        self.assertIn("background: var(--accent)", rule.group(1))
        self.assertIn("color: #FFF", rule.group(1))
        self.assertRegex(self.html, r"\.send:disabled \{ opacity: \.4;")

    def test_the_composer_placeholder_names_the_tab(self) -> None:
        self.assertIn('placeholder="Ask about this tab or anything"', self.html)
        self.assertNotIn("Ask anything, or tell it what to do", self.html)

    def test_the_tab_chip_and_quote_chip_share_the_chip_style(self) -> None:
        self.assertRegex(self.html, r"\.context-plate \{[^}]*height: 22px")
        self.assertRegex(self.html, r"\.context-plate \{[^}]*border-radius: 5px")
        self.assertRegex(self.html, r"\.quote-chip \{[^}]*height: 22px")
        self.assertRegex(self.html, r"\.quote-chip \{[^}]*border-radius: 5px")

    def test_only_neutral_focus_rules_exist(self) -> None:
        css = agent_component_css(self.html)
        for match in re.finditer(r"([^{}]*:focus[^{}]*)\{([^}]*)\}", css):
            for outline in re.findall(r"outline:\s*([^;]+);", match.group(2)):
                with self.subTest(selector=match.group(1).strip()):
                    self.assertEqual(outline.strip(), "none")
            self.assertNotIn("var(--accent)", match.group(2))

    def test_component_css_uses_theme_variables_and_no_hex_but_white(self) -> None:
        css = agent_component_css(self.html)
        for match in re.finditer(r"(?<![\w-])#[0-9a-fA-F]{3,8}\b", css):
            with self.subTest(color=match.group(0)):
                self.assertIn(match.group(0).upper(), ("#FFF", "#FFFFFF"))

    def test_the_send_button_stays_on_the_right_when_the_chip_row_is_hidden(self) -> None:
        # With #chiprow hidden, the send button is the row's only child: it must still sit right.
        rule = re.search(r"\n\.send \{([^}]*)\}", self.html)
        self.assertIsNotNone(rule)
        self.assertIn("margin-left: auto", rule.group(1))
        self.assertIn('id="chiprow" hidden', self.html)

    def test_a_queued_bubble_has_a_one_pixel_dashed_neutral_border(self) -> None:
        self.assertIn(".user.queued .bubble { opacity: .65; border: 1px dashed var(--border-2); }", self.html)
        self.assertNotIn("border-style: dashed", self.html)

    def test_every_working_pill_pulses_whatever_the_session_state(self) -> None:
        self.assertRegex(
            self.html,
            r'\.status-pill\[data-tone="busy"\] \.dot, [^{]*\{\s*animation: pulse',
        )

    def test_a_long_tool_name_ellipsizes_and_the_host_takes_the_rest(self) -> None:
        name = re.search(r"\.tool \.name \{([^}]*)\}", self.html)
        self.assertIsNotNone(name)
        for fragment in ("overflow: hidden", "text-overflow: ellipsis", "min-width: 0", "max-width: 70%"):
            self.assertIn(fragment, name.group(1))
        self.assertRegex(self.html, r"\.tool \.host \{[^}]*flex: 1 1 0")

    def test_a_collect_checkbox_rings_its_row_on_keyboard_focus(self) -> None:
        self.assertIn(".collect-row:has(input:focus-visible) { box-shadow: 0 0 0 2px var(--text-2); }", self.html)

    def test_an_mcp_tool_row_shows_the_tool_name_and_keeps_the_full_name_in_the_tooltip(self) -> None:
        node = self.html[self.html.index("function toolNode") :][:1000]
        self.assertIn('full.replace(/^mcp__\\S+?__(?=\\S)/, "")', node)
        self.assertIn("node.title = title", node)
        # The prefix regex strips the server only: a hyphenated server name still goes.
        pattern = re.compile(r"^mcp__\S+?__(?=\S)")
        self.assertEqual(pattern.sub("", "mcp__island-browser__page_read_text"), "page_read_text")
        self.assertEqual(pattern.sub("", "page_snapshot"), "page_snapshot")

    def test_the_tool_row_is_one_line_with_a_spoken_status(self) -> None:
        node = self.html[self.html.index("function toolNode") :][:900]
        self.assertIn('el("span", "name"', node)
        self.assertIn('el("span", "host"', node)
        self.assertIn('"visually-hidden"', node)
        self.assertNotIn("innerHTML", node)


class AgentConfirmationCard(unittest.TestCase):
    def setUp(self) -> None:
        self.html = page("agent.html")

    def test_the_card_sits_on_the_composer_inset_with_a_full_hairline(self) -> None:
        self.assertIn("#confirmations { margin: 12px 12px 0; }", self.html)
        card = re.search(r"\.card\.confirm-card \{([^}]*)\}", self.html)
        self.assertIsNotNone(card)
        self.assertIn("margin: 0", card.group(1))
        self.assertIn("border: 1px solid var(--warn-border)", card.group(1))
        self.assertRegex(self.html, r"\.composer \{ margin: 12px;")

    def test_native_kicker_is_sentence_case_and_the_four_answers_form_even_rows(self) -> None:
        self.assertIn('root.setAttribute("data-look", theme.look || "")', self.html)
        kicker = re.search(r':root\[data-look="native"\] \.card\.confirm-card h4 \{([^}]*)\}', self.html)
        self.assertIsNotNone(kicker)
        self.assertIn("text-transform: none", kicker.group(1))
        self.assertIn("var(--font)", kicker.group(1))
        grid = re.search(r"\.confirm-card \.actions \{([^}]*)\}", self.html)
        self.assertIsNotNone(grid)
        self.assertIn("grid-template-columns: 1fr 1fr", grid.group(1))

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

    def test_uppercase_label_rule_does_not_reach_the_tab_rows(self) -> None:
        # The tab rows are <label> elements too: URLs must keep their case.
        self.assertIn('.collect-sheet label[for="collect-what"]', self.html)
        self.assertNotIn(".collect-sheet label,", self.html)
        self.assertNotIn(".collect-sheet label {", self.html)

    def test_each_checked_tab_is_sent_with_the_url_the_sheet_showed(self) -> None:
        self.assertIn("box.dataset.url = tab.url", self.html)
        self.assertIn('return { index: Number(box.value), url: box.dataset.url || "" };', self.html)
        self.assertIn('post({ type: "collect", what: what, tabs: tabs });', self.html)


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
