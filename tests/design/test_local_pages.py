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
            ["agent.html", "report.html", "settings.html", "start.html", "tab_overview.html"],
        )
        self.assertEqual([path.name for path in SCRIPT_FILES], ["markdown.js"])

    def test_no_page_requests_an_external_resource(self) -> None:
        for path in PAGE_FILES + SCRIPT_FILES:
            text = path.read_text(encoding="utf-8")
            for fragment in FORBIDDEN_FRAGMENTS:
                with self.subTest(page=path.name, fragment=fragment):
                    self.assertNotIn(fragment, text)
            for match in re.finditer(r"https?://[^\s\"'<>)]*", text):
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


class StartPage(unittest.TestCase):
    def setUp(self) -> None:
        self.html = page("start.html")

    def test_has_one_input_and_the_three_modes(self) -> None:
        self.assertEqual(self.html.count("<input"), 1)
        self.assertIn('data-mode="search"', self.html)
        self.assertIn('data-mode="agent"', self.html)
        self.assertIn('data-mode="research"', self.html)
        self.assertIn(">Search</button>", self.html)
        self.assertIn(">Ask agent</button>", self.html)
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

    def test_uses_the_graphite_type_stack(self) -> None:
        self.assertIn("Geist, ui-sans-serif", self.html)
        self.assertIn('"Geist Mono"', self.html)


class ReportPage(unittest.TestCase):
    def setUp(self) -> None:
        self.html = page("report.html")
        self.built = built_page("report.html")

    def test_has_the_three_actions(self) -> None:
        self.assertIn(">Copy Markdown</button>", self.html)
        self.assertIn(">Save as .md...</button>", self.html)
        self.assertIn(">Continue in agent panel</button>", self.html)
        for message in ("copy_report", "save_report", "continue_in_agent", '"open_url"'):
            self.assertIn(message, self.html)

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
