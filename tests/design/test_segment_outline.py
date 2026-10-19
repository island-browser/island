"""Dark selected segments on the web pages keep an outline, as the native chrome does."""

from __future__ import annotations

import re
import unittest
from pathlib import Path

PAGES = Path(__file__).resolve().parents[2] / "src/main/pages"
PAGE_NAMES = ("settings.html", "start.html", "plan.html")
SELECTED_RULE = re.compile(r"\.(segmented|modes) button\[aria-(pressed|checked)=\"true\"\] \{[^}]*\}")


class SegmentOutlineTest(unittest.TestCase):
    def test_every_theme_pairs_the_selected_fill_with_an_outline(self) -> None:
        for name in PAGE_NAMES:
            with self.subTest(page=name):
                html = (PAGES / name).read_text(encoding="utf-8")
                selected = re.findall(r"--segment-selected:\s*(#[0-9A-Fa-f]{6})", html)
                outlines = re.findall(r"--segment-outline:\s*([^;}]+)", html)
                self.assertGreaterEqual(len(selected), 2)
                self.assertEqual(len(outlines), len(selected))
                # Light: no outline, like the native chrome. Dark: a border_strong ring.
                self.assertIn("--segment-outline: transparent", html)
                self.assertIn("--segment-outline: #48484A", html)

    def test_selected_segments_draw_the_outline_inset(self) -> None:
        for name in PAGE_NAMES:
            with self.subTest(page=name):
                html = (PAGES / name).read_text(encoding="utf-8")
                rules = SELECTED_RULE.findall(html)
                self.assertTrue(rules, "no selected segment rule")
                for match in SELECTED_RULE.finditer(html):
                    self.assertIn("inset 0 0 0 1px var(--segment-outline)", match.group(0))


if __name__ == "__main__":
    unittest.main()
