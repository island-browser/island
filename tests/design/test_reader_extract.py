"""Reader-mode extraction heuristic (`src/main/reader/reader_extract.js`).

The script is injected into arbitrary pages, so its contract is pinned two ways: statically (no
network, no eval, only the DOM basics the node shim provides) and behaviourally through Node
against fixture documents. The behavioural checks skip when Node is not installed.
"""

from __future__ import annotations

import json
import shutil
import subprocess
import unittest
from pathlib import Path

HERE = Path(__file__).resolve().parent
REPOSITORY = HERE.parents[1]
EXTRACT = REPOSITORY / "src/main/reader/reader_extract.js"
NODE = shutil.which("node")

LOREM = (
    "Browsers turn markup into pixels, and every pixel passes through a long pipeline of parsing, "
    "styling, layout and painting, which is why a small change can ripple through the whole page. "
)


def paragraphs(count: int, tag: str = "p") -> str:
    return "".join(f"<{tag}>{LOREM}Paragraph {i}.</{tag}>" for i in range(count))


BLOG = f"""
<html lang="en"><head><title>Why layout is hard - Example Blog</title>
<meta property="og:site_name" content="Example Blog">
<meta name="author" content="Ada Lovelace">
<meta property="article:published_time" content="2026-03-04T09:00:00Z">
<script>window.tracking = "should never appear";</script></head>
<body>
<header class="site-header"><nav><a href="/">Home</a><a href="/about">About</a></nav></header>
<main><article>
<h1>Why layout is hard</h1>
<p class="byline">By Someone Else</p>
{paragraphs(3)}
<h2>Lists and quotes</h2>
<ul><li>First <strong>item</strong></li><li>Second <a href="/rel/link">item</a></li></ul>
<blockquote>Measure twice, paint once.</blockquote>
<pre><code>fn main() {{
    println!("hi");
}}</code></pre>
<figure><img src="/img/diagram.png" alt="A diagram" width="640" height="360">
<figcaption>Figure one</figcaption></figure>
<img src="/img/pixel.gif" width="1" height="1">
{paragraphs(2)}
<div class="share-buttons"><a href="https://twitter.com/share">Share on Twitter</a>
<a href="https://facebook.com/share">Share on Facebook</a></div>
<div id="comments"><h2>Comments</h2><p>First!</p></div>
</article></main>
<aside class="sidebar"><h3>Related</h3><ul><li><a href="/a">One</a></li><li><a href="/b">Two</a></li></ul></aside>
<footer>Copyright Example</footer>
</body></html>
"""

DIV_LAYOUT = f"""
<html><head><title>Notes on parsing</title></head><body>
<div id="top"><ul class="menu-list"><li><a href="/1">A</a></li><li><a href="/2">B</a></li>
<li><a href="/3">C</a></li><li><a href="/4">D</a></li></ul></div>
<div class="page"><div class="post-body">
<h1>Notes on parsing</h1>
{paragraphs(4)}
Stray text directly inside a div still counts as a paragraph of the article, long enough to keep.
</div>
<div class="widget-links"><a href="/x">Popular one</a> <a href="/y">Popular two</a>
<a href="/z">Popular three</a></div></div>
</body></html>
"""

WIKI = f"""
<html lang="en"><head><title>Web browser - Wikipedia</title></head><body>
<div id="mw-navigation"><a href="/wiki/Main_Page">Main page</a></div>
<h1 id="firstHeading">Web browser</h1>
<div id="mw-content-text"><div class="mw-parser-output">
<table class="infobox"><tr><td>Infobox stuff that is not prose at all</td></tr></table>
<p>A <b>web browser</b> is an application for accessing websites.{LOREM}<sup class="reference"><a href="#cn1">[1]</a></sup></p>
<h2><span class="mw-headline" id="History">History</span><span class="mw-editsection"><a href="/edit">edit</a></span></h2>
{paragraphs(3)}
<div class="navbox"><a href="/n1">Nav one</a><a href="/n2">Nav two</a></div>
<h2><span class="mw-headline">See also</span></h2>
<ul class="hlist"><li><a href="/w/a">Alpha</a></li><li><a href="/w/b">Beta</a></li></ul>
</div></div>
<div id="footer">Text is available under a license.</div>
</body></html>
"""

SPLIT = f"""
<html><head><title>Split story</title></head><body>
<div class="story"><div class="part">{paragraphs(2)}</div><div class="part">{paragraphs(2)}</div>
<div class="part">{paragraphs(1)}</div></div></body></html>
"""

HOSTILE = f"""
<html><head><title>Hostile</title></head><body><article>
<h1>Hostile page</h1>{paragraphs(3)}
<p><a href="javascript:alert(1)">bad link</a> <a href="https://ok.example/page" onclick="x()">good link</a></p>
<img src="javascript:alert(2)" onerror="alert(3)">
<img src="data:image/svg+xml;base64,PHN2Zz4=" alt="inline data">
<iframe src="https://evil.example/frame"></iframe>
<script>alert(4)</script><style>p {{ color: red }}</style>
<p style="display:none">hidden trick text that must not show up anywhere</p>
<img data-src="/lazy/photo.jpg" alt="lazy" width="800" height="600">
</article></body></html>
"""

NOT_AN_ARTICLE = """
<html><head><title>Sign in</title></head><body><form><input name="u"><button>Sign in</button></form>
<a href="/help">Help</a> <a href="/terms">Terms</a></body></html>
"""

FIXTURES = {
    "blog": ("https://blog.example/posts/layout", BLOG),
    "div": ("https://notes.example/parsing", DIV_LAYOUT),
    "wiki": ("https://en.wikipedia.org/wiki/Web_browser", WIKI),
    "split": ("https://story.example/s", SPLIT),
    "hostile": ("https://hostile.example/", HOSTILE),
    "form": ("https://login.example/", NOT_AN_ARTICLE),
}


def flatten(node) -> str:
    if isinstance(node, str):
        return node
    return "".join(flatten(child) for child in node[2:])


def walk(nodes):
    for node in nodes:
        if isinstance(node, list):
            yield node
            yield from walk(node[2:])


def tags(result) -> list[str]:
    return [node[0] for node in walk(result["content"])]


def text(result) -> str:
    return " ".join(flatten(node) for node in result["content"])


class ReaderExtractStatic(unittest.TestCase):
    def setUp(self) -> None:
        self.source = EXTRACT.read_text(encoding="utf-8")

    def test_script_is_self_contained_and_offline(self) -> None:
        for fragment in ("fetch(", "XMLHttpRequest", "WebSocket", "sendBeacon", "eval(",
                         "new Function", "import(", "document.write", "innerHTML", "outerHTML",
                         "createElement", "querySelector", "getComputedStyle"):
            with self.subTest(fragment=fragment):
                self.assertNotIn(fragment, self.source)

    def test_entry_point_is_one_function_returning_data(self) -> None:
        self.assertTrue(self.source.lstrip("/ \n").startswith("Island Reader"))
        self.assertEqual(self.source.count("\nfunction islandExtractArticle(doc, win) {"), 1)
        self.assertIn('"use strict"', self.source)

    def test_script_is_ascii_so_it_embeds_in_a_cdp_expression(self) -> None:
        self.source.encode("ascii")


@unittest.skipUnless(NODE, "node is not installed")
class ReaderExtractBehavior(unittest.TestCase):
    results: dict[str, dict]

    @classmethod
    def setUpClass(cls) -> None:
        names = list(FIXTURES)
        payload = [{"url": FIXTURES[name][0], "html": FIXTURES[name][1]} for name in names]
        completed = subprocess.run(
            [NODE, str(HERE / "reader_extract_runner.js")],
            input=json.dumps(payload),
            capture_output=True,
            text=True,
            check=True,
            timeout=60,
        )
        cls.results = dict(zip(names, json.loads(completed.stdout)))

    def test_blog_post_keeps_the_article_and_its_metadata(self) -> None:
        result = self.results["blog"]
        self.assertTrue(result["ok"])
        self.assertEqual(result["title"], "Why layout is hard")
        self.assertEqual(result["byline"], "Ada Lovelace")
        self.assertEqual(result["site"], "Example Blog")
        self.assertEqual(result["published"], "2026-03-04")
        self.assertEqual(result["lang"], "en")
        self.assertGreater(result["words"], 100)

    def test_blog_post_keeps_headings_lists_quotes_code_and_images(self) -> None:
        result = self.results["blog"]
        found = tags(result)
        for tag in ("p", "h2", "ul", "li", "blockquote", "pre", "figure", "figcaption", "img",
                    "strong", "a"):
            with self.subTest(tag=tag):
                self.assertIn(tag, found)
        images = [node for node in walk(result["content"]) if node[0] == "img"]
        self.assertEqual([image[1]["src"] for image in images],
                         ["https://blog.example/img/diagram.png"])
        self.assertEqual(images[0][1]["alt"], "A diagram")
        pre = next(node for node in walk(result["content"]) if node[0] == "pre")
        self.assertIn('println!("hi");', flatten(pre))
        self.assertIn("\n", flatten(pre))  # preformatted text keeps its line breaks

    def test_blog_post_drops_navigation_asides_sharing_and_comments(self) -> None:
        body = text(self.results["blog"])
        for dropped in ("Home", "About", "Related", "Copyright Example", "Share on Twitter",
                        "Comments", "First!", "should never appear"):
            with self.subTest(dropped=dropped):
                self.assertNotIn(dropped, body)
        self.assertNotIn("h1", tags(self.results["blog"]))  # the title is rendered by the reader

    def test_relative_links_become_absolute(self) -> None:
        links = [node for node in walk(self.results["blog"]["content"]) if node[0] == "a"]
        self.assertEqual([link[1]["href"] for link in links], ["https://blog.example/rel/link"])

    def test_div_layout_finds_the_densest_container(self) -> None:
        result = self.results["div"]
        self.assertTrue(result["ok"])
        self.assertEqual(result["title"], "Notes on parsing")
        body = text(result)
        self.assertIn("Paragraph 3.", body)
        self.assertIn("Stray text directly inside a div", body)
        for dropped in ("Popular one", "Popular two"):
            self.assertNotIn(dropped, body)
        self.assertNotIn("A B C D", body)

    def test_wikipedia_shape_drops_chrome_but_keeps_sections(self) -> None:
        result = self.results["wiki"]
        self.assertTrue(result["ok"])
        self.assertEqual(result["title"], "Web browser")
        body = text(result)
        self.assertIn("web browser is an application", body)
        self.assertIn("History", body)
        for dropped in ("Infobox stuff", "Nav one", "edit", "[1]", "Main page", "Text is available",
                        "See also", "Alpha"):
            with self.subTest(dropped=dropped):
                self.assertNotIn(dropped, body)

    def test_article_split_across_sibling_blocks_is_kept_whole(self) -> None:
        body = text(self.results["split"])
        self.assertIn("Paragraph 0.", body)
        self.assertIn("Paragraph 1.", body)

    def test_hostile_markup_never_reaches_the_output(self) -> None:
        result = self.results["hostile"]
        self.assertTrue(result["ok"])
        encoded = json.dumps(result)
        for fragment in ("javascript:", "onclick", "onerror", "alert(", "evil.example",
                         "hidden trick", "color: red", "data:image"):
            with self.subTest(fragment=fragment):
                self.assertNotIn(fragment, encoded)
        links = [node for node in walk(result["content"]) if node[0] == "a"]
        self.assertEqual([link[1] for link in links], [{"href": "https://ok.example/page"}])
        images = [node[1]["src"] for node in walk(result["content"]) if node[0] == "img"]
        self.assertEqual(images, ["https://hostile.example/lazy/photo.jpg"])
        self.assertIn("bad link", text(result))  # an unsafe link keeps its text, not its target

    def test_output_only_uses_whitelisted_tags_and_attributes(self) -> None:
        allowed_tags = {"p", "h2", "h3", "h4", "h5", "h6", "ul", "ol", "li", "blockquote", "pre",
                        "code", "a", "em", "strong", "br", "hr", "img", "figure", "figcaption",
                        "table", "thead", "tbody", "tfoot", "tr", "th", "td", "sub", "sup", "dl",
                        "dt", "dd"}
        for name, result in self.results.items():
            if not result["ok"]:
                continue
            for node in walk(result["content"]):
                with self.subTest(fixture=name, tag=node[0]):
                    self.assertIn(node[0], allowed_tags)
                    if node[1] is not None:
                        self.assertLessEqual(set(node[1]), {"href", "src", "alt"})

    def test_pages_without_an_article_are_reported_not_forced(self) -> None:
        self.assertEqual(self.results["form"], {"ok": False, "reason": "no_article"})


if __name__ == "__main__":
    unittest.main()
