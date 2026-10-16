"""Drift guard for the machine-readable Native look color contract."""

from __future__ import annotations

import json
import re
import unittest
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
DESIGN_MD = REPOSITORY / "DESIGN.md"
NATIVE_TOKEN_TEST = REPOSITORY / "tests/design_tokens_test.cpp"

CONTRACT_FENCE = re.compile(
    r"```design-tokens-native[ \t]*\n(?P<json>.*?)\n```", re.DOTALL
)
HEX_COLOR = re.compile(r"^#[0-9A-F]{6}$")
NATIVE_HEX = re.compile(r"0xFF([0-9A-F]{6})U")
COLOR_SLOTS = {
    "background",
    "surface",
    "surface_secondary",
    "fill",
    "text",
    "text_secondary",
    "text_tertiary",
    "border",
    "border_strong",
    "accent",
    "selection",
    "accent_soft",
    "accent_text",
    "warning_bg",
    "warning_border",
    "warning_text",
    "success",
    "danger",
    "branch_a",
    "branch_b",
    "branch_c",
    "branch_d",
}


def load_contract() -> dict:
    text = DESIGN_MD.read_text(encoding="utf-8")
    blocks = CONTRACT_FENCE.findall(text)
    if len(blocks) != 1:
        raise ValueError(
            "DESIGN.md must contain exactly one `design-tokens-native` block, "
            f"found {len(blocks)}"
        )
    return json.loads(blocks[0])


class NativeLookContractTest(unittest.TestCase):
    @classmethod
    def setUpClass(cls) -> None:
        cls.contract = load_contract()

    def test_contract_identity(self) -> None:
        self.assertEqual(self.contract["contract"], "island-native-look")
        self.assertEqual(self.contract["version"], 1)

    def test_both_themes_have_exactly_the_native_color_slots(self) -> None:
        self.assertEqual(set(self.contract["colors"]), {"light", "dark"})
        for theme in ("light", "dark"):
            with self.subTest(theme=theme):
                self.assertEqual(set(self.contract["colors"][theme]), COLOR_SLOTS)

    def test_every_color_is_a_six_digit_hex_value(self) -> None:
        for theme in ("light", "dark"):
            for slot, value in self.contract["colors"][theme].items():
                with self.subTest(theme=theme, slot=slot):
                    self.assertRegex(value, HEX_COLOR)

    def test_every_contract_color_is_pinned_in_the_native_tests(self) -> None:
        native_test = NATIVE_TOKEN_TEST.read_text(encoding="utf-8")
        asserted_colors = {f"#{rgb}" for rgb in NATIVE_HEX.findall(native_test)}
        for theme in ("light", "dark"):
            for slot, value in self.contract["colors"][theme].items():
                with self.subTest(theme=theme, slot=slot):
                    self.assertIn(value, asserted_colors)


if __name__ == "__main__":
    _ = unittest.main()
