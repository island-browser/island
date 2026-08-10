"""Drift guard for the machine-readable Island design-token contract.

`DESIGN.md` holds the single machine-readable token contract (a fenced
``design-tokens`` JSON block). This suite proves that the contract is complete
and well formed, that the real product-site consumer (`site/assets/css/site.css`)
resolves to exactly those values, and that the pinned native assertions in
`tests/design_tokens_test.cpp` still agree with the contract.

No second source of truth is created: the DESIGN.md contract is the only place
the values are authored, and every other check below is an equality assertion.
Robust parsing of Markdown into C++ was rejected as inappropriate for this
repository, so native parity is covered in two weaker-but-sufficient layers:

* value layer (this file): the exact ARGB/DIP constants the native runtime is
  asserted against are re-derived from `tests/design_tokens_test.cpp`, the
  pinned native assertion file, and compared with the contract;
* behavior layer: the native GoogleTest suite exercises
  `ChromeTokens::ForTheme` at runtime (`ctest -R ChromeTokens`).
"""

from __future__ import annotations

import json
import re
import unittest
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
DESIGN_MD = REPOSITORY / "DESIGN.md"
SITE_CSS = REPOSITORY / "site/assets/css/site.css"
NATIVE_TOKEN_TEST = REPOSITORY / "tests/design_tokens_test.cpp"

CONTRACT_FENCE = re.compile(r"```design-tokens[ \t]*\n(?P<json>.*?)\n```", re.DOTALL)
HEX_COLOR = re.compile(r"^#[0-9A-F]{6}$")
CSS_DECLARATION = re.compile(r"(--[A-Za-z0-9-]+)\s*:\s*([^;{}]+);")
NATIVE_ASSERTION = re.compile(r"EXPECT_EQ\(tokens\.([\w.]+), ([^)]+)\);")
NATIVE_HEX = re.compile(r"^0x([0-9A-F]{8})U$")

SEMANTIC_ROLES = (
    "background",
    "surface",
    "surface_secondary",
    "text",
    "text_secondary",
    "border",
    "accent",
)
SITE_ONLY_ROLES = ("space_blue", "space_coral", "space_green")

SEMANTIC_TO_CSS = {
    "background": "--bg",
    "surface": "--surface",
    "surface_secondary": "--surface-2",
    "text": "--text",
    "text_secondary": "--text-2",
    "border": "--border",
    "accent": "--accent",
}
SITE_ONLY_TO_CSS = {
    "space_blue": "--space-blue",
    "space_coral": "--space-coral",
    "space_green": "--space-green",
}
SPACING_TO_CSS = {
    "space_1": "--space-1",
    "space_2": "--space-2",
    "space_3": "--space-3",
    "space_4": "--space-4",
    "space_6": "--space-6",
}
RADII_TO_CSS = {
    "radius_small": "--radius-sm",
    "radius_medium": "--radius-md",
}
MOTION_TO_CSS = {
    "fast_ms": "--motion-fast",
    "enter_ms": "--motion-enter",
    "reveal_ms": "--motion-reveal",
}


def load_contract() -> dict:
    text = DESIGN_MD.read_text(encoding="utf-8")
    blocks = CONTRACT_FENCE.findall(text)
    if len(blocks) != 1:
        raise ValueError(f"DESIGN.md must contain exactly one `design-tokens` block, found {len(blocks)}")
    return json.loads(blocks[0])


def parse_declarations(css_text: str, start: int) -> dict[str, str]:
    root = css_text.find(":root", start)
    if root == -1:
        raise ValueError("no :root block found in site CSS")
    body = css_text[root : css_text.index("}", root)]
    return {name: value.strip() for name, value in CSS_DECLARATION.findall(body)}


def load_site_tokens() -> tuple[dict[str, str], dict[str, str]]:
    """Return (light custom properties, effective dark custom properties)."""
    css_text = SITE_CSS.read_text(encoding="utf-8")
    light = parse_declarations(css_text, 0)
    dark_media = css_text.find("@media (prefers-color-scheme: dark)")
    if dark_media == -1:
        raise ValueError("no dark scheme media block found in site CSS")
    overrides = parse_declarations(css_text, dark_media)
    dark = {**light, **overrides}
    return light, dark


def load_native_assertions() -> dict[str, list[str]]:
    text = NATIVE_TOKEN_TEST.read_text(encoding="utf-8")
    assertions: dict[str, list[str]] = {}
    for field, literal in NATIVE_ASSERTION.findall(text):
        assertions.setdefault(field.removesuffix(".argb"), []).append(literal.strip())
    return assertions


class TokenContractCompleteness(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.contract = load_contract()

    def test_contract_identifies_the_accepted_design(self) -> None:
        self.assertEqual(self.contract["contract"], "island-design-tokens")
        self.assertEqual(self.contract["version"], 1)
        self.assertEqual(self.contract["accepted_design"], "Ledger")

    def test_semantic_roles_cover_both_themes_exactly(self) -> None:
        for theme in ("light", "dark"):
            with self.subTest(theme=theme):
                self.assertEqual(sorted(self.contract["colors"][theme]), sorted(SEMANTIC_ROLES))

    def test_site_only_marker_tokens_are_canonical(self) -> None:
        self.assertEqual(sorted(self.contract["colors"]["site_only"]), sorted(SITE_ONLY_ROLES))

    def test_every_color_is_a_six_digit_hex_value(self) -> None:
        palette = self.contract["colors"]
        for group in ("light", "dark", "site_only"):
            for role, value in palette[group].items():
                with self.subTest(group=group, role=role):
                    self.assertRegex(value, HEX_COLOR)

    def test_spacing_scale_is_4_8_12_16_24(self) -> None:
        self.assertEqual(
            self.contract["spacing"],
            {"space_1": 4, "space_2": 8, "space_3": 12, "space_4": 16, "space_6": 24},
        )

    def test_radii_are_8_and_12(self) -> None:
        self.assertEqual(self.contract["radii"], {"radius_small": 8, "radius_medium": 12})

    def test_rail_width_is_286_dip(self) -> None:
        self.assertEqual(self.contract["rail_width_dip"], 286)

    def test_typography_is_geist_and_geist_mono(self) -> None:
        typography = self.contract["typography"]
        self.assertEqual(typography["ui_family"], "Geist")
        self.assertEqual(typography["mono_family"], "Geist Mono")
        self.assertTrue(typography["ui_stack"].startswith("Geist,"))
        self.assertTrue(typography["mono_stack"].startswith('"Geist Mono",'))

    def test_motion_tokens_are_present(self) -> None:
        motion = self.contract["motion"]
        self.assertEqual(motion["fast_ms"], 160)
        self.assertEqual(motion["enter_ms"], 480)
        self.assertEqual(motion["reveal_ms"], 700)
        self.assertEqual(motion["ease_out"], "cubic-bezier(.16, 1, .3, 1)")
        self.assertIn("transform", motion["composited_properties_only"])
        self.assertNotIn("layout", motion["composited_properties_only"])

    def test_material_and_depth_rules_are_present(self) -> None:
        self.assertIn("surface_secondary", self.contract["material"]["shell_material"])
        self.assertIn("border", self.contract["depth"]["product_window"])

    def test_accessibility_constraints_are_present(self) -> None:
        accessibility = self.contract["accessibility"]
        self.assertEqual(accessibility["target"], "WCAG 2.2 AA")
        self.assertEqual(accessibility["body_contrast_min"], 4.5)
        self.assertEqual(accessibility["large_text_contrast_min"], 3.0)
        self.assertIn("accent", accessibility["focus_ring"])

    def test_phase3_exclusion_rules_are_present(self) -> None:
        phase3_rules = self.contract["rules"]["phase3_exclusions"]
        self.assertTrue(phase3_rules)
        values = phase3_rules if isinstance(phase3_rules, list) else [phase3_rules]
        joined = " ".join(values).lower()
        for forbidden in ("tabs", "spaces", "split", "command palette", "session restore"):
            with self.subTest(feature=forbidden):
                self.assertIn(forbidden, joined)


class SiteConsumerParity(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.contract = load_contract()
        cls.light, cls.dark = load_site_tokens()

    def assert_color(self, css_property: str, expected: str, theme: str) -> None:
        value = (self.light if theme == "light" else self.dark).get(css_property)
        self.assertIsNotNone(value, f"{css_property} is not declared for the {theme} site theme")
        self.assertEqual(value.upper(), expected.upper(), f"{css_property} drifted in {theme}")

    def test_light_semantic_colors_match_the_contract(self) -> None:
        for role, css_property in SEMANTIC_TO_CSS.items():
            with self.subTest(role=role):
                self.assert_color(css_property, self.contract["colors"]["light"][role], "light")

    def test_dark_semantic_colors_match_the_contract(self) -> None:
        # Values not re-declared inside the dark media block inherit from :root,
        # mirroring how the browser resolves them.
        for role, css_property in SEMANTIC_TO_CSS.items():
            with self.subTest(role=role):
                self.assert_color(css_property, self.contract["colors"]["dark"][role], "dark")

    def test_site_only_marker_colors_match_the_contract(self) -> None:
        for role, css_property in SITE_ONLY_TO_CSS.items():
            for theme in ("light", "dark"):
                with self.subTest(role=role, theme=theme):
                    self.assert_color(css_property, self.contract["colors"]["site_only"][role], theme)

    def test_spacing_tokens_match_the_contract_in_px(self) -> None:
        for token, css_property in SPACING_TO_CSS.items():
            with self.subTest(token=token):
                value = self.light[css_property]
                match = re.fullmatch(r"(\d+)px", value)
                self.assertIsNotNone(match, f"{css_property} is not a px token: {value}")
                self.assertEqual(int(match.group(1)), self.contract["spacing"][token])

    def test_radius_tokens_match_the_contract_in_px(self) -> None:
        for token, css_property in RADII_TO_CSS.items():
            with self.subTest(token=token):
                value = self.light[css_property]
                match = re.fullmatch(r"(\d+)px", value)
                self.assertIsNotNone(match, f"{css_property} is not a px token: {value}")
                self.assertEqual(int(match.group(1)), self.contract["radii"][token])

    def test_motion_durations_match_the_contract_in_ms(self) -> None:
        for token, css_property in MOTION_TO_CSS.items():
            with self.subTest(token=token):
                value = self.light[css_property]
                match = re.fullmatch(r"(\d+)ms", value)
                self.assertIsNotNone(match, f"{css_property} is not an ms token: {value}")
                self.assertEqual(int(match.group(1)), self.contract["motion"][token])

    def test_easing_matches_the_contract(self) -> None:
        value = re.sub(r"\s+", "", self.light["--ease-out"])
        expected = re.sub(r"\s+", "", self.contract["motion"]["ease_out"])
        self.assertEqual(value, expected)

    def test_font_stacks_match_the_contract(self) -> None:
        typography = self.contract["typography"]
        self.assertEqual(self.light["--sans"], typography["ui_stack"])
        self.assertEqual(self.light["--mono"], typography["mono_stack"])


class NativeConsumerParity(unittest.TestCase):
    """Bridges the contract to the pinned native token assertions.

    Parsing Markdown into C++ is inappropriate here, so the bridge reads the
    literal constants that `tests/design_tokens_test.cpp` asserts against at
    runtime. If the shape of that native test changes, this bridge fails loudly
    instead of silently claiming parity.
    """

    @classmethod
    def setUpClass(cls) -> None:
        cls.contract = load_contract()
        cls.assertions = load_native_assertions()

    def assert_native_color(self, field: str, role: str) -> None:
        literals = self.assertions.get(field)
        self.assertIsNotNone(literals, f"tests/design_tokens_test.cpp no longer asserts tokens.{field}")
        self.assertEqual(
            len(literals),
            2,
            f"expected exactly one light and one dark assertion for tokens.{field}, found {literals}",
        )
        observed = []
        for literal in literals:
            match = NATIVE_HEX.fullmatch(literal)
            self.assertIsNotNone(match, f"unexpected ARGB literal for tokens.{field}: {literal}")
            argb = match.group(1)
            self.assertEqual(argb[:2], "FF", f"tokens.{field} is not opaque: {literal}")
            observed.append(f"#{argb[2:]}")
        expected = sorted(
            (self.contract["colors"]["light"][role], self.contract["colors"]["dark"][role])
        )
        self.assertEqual(sorted(observed), expected, f"native tokens.{field} drifted from the contract")

    def test_semantic_colors_match_the_contract(self) -> None:
        # In `tests/design_tokens_test.cpp` the ChromeTokens field names match
        # the contract role names one-for-one, so no extra map is needed here.
        for role in SEMANTIC_ROLES:
            with self.subTest(field=role):
                self.assert_native_color(role, role)

    def test_layout_tokens_match_the_contract(self) -> None:
        expected = {
            "rail_width_dip": self.contract["rail_width_dip"],
            "radius_small_dip": self.contract["radii"]["radius_small"],
            "radius_medium_dip": self.contract["radii"]["radius_medium"],
            "spacing_1_dip": self.contract["spacing"]["space_1"],
            "spacing_2_dip": self.contract["spacing"]["space_2"],
            "spacing_3_dip": self.contract["spacing"]["space_3"],
            "spacing_4_dip": self.contract["spacing"]["space_4"],
            "spacing_6_dip": self.contract["spacing"]["space_6"],
        }
        for field, value in expected.items():
            with self.subTest(field=field):
                literals = self.assertions.get(field)
                self.assertIsNotNone(literals, f"tests/design_tokens_test.cpp no longer asserts tokens.{field}")
                self.assertEqual({int(literal) for literal in literals}, {value})

    def test_font_tokens_match_the_contract(self) -> None:
        typography = self.contract["typography"]
        self.assertEqual(self.assertions["ui_font"], ["ChromeFont::kGeist"])
        self.assertEqual(self.assertions["mono_font"], ["ChromeFont::kGeistMono"])
        self.assertEqual(typography["ui_family"], "Geist")
        self.assertEqual(typography["mono_family"], "Geist Mono")


if __name__ == "__main__":
    _ = unittest.main()
