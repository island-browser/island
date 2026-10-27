from __future__ import annotations

import re
import struct
import sys
import tempfile
import unittest
from pathlib import Path
from typing import final

from .test_installers import installers

REPOSITORY = Path(__file__).resolve().parents[2]
BRAND = REPOSITORY / "resources" / "brand"
MAIN = REPOSITORY / "src" / "main"


def png_size(path: Path) -> tuple[int, int]:
    data = path.read_bytes()
    assert data[:8] == b"\x89PNG\r\n\x1a\n", path
    return struct.unpack(">II", data[16:24])


def icns_types(path: Path) -> dict[str, int]:
    data = path.read_bytes()
    assert data[:4] == b"icns" and struct.unpack(">I", data[4:8])[0] == len(data)
    found, cursor = {}, 8
    while cursor < len(data):
        kind, size = data[cursor: cursor + 4].decode("latin-1"), struct.unpack(">I", data[cursor + 4: cursor + 8])[0]
        found[kind] = size
        cursor += size
    return found


@final
class BrandSourceTests(unittest.TestCase):
    def test_icon_sources_follow_the_macos_grid(self) -> None:
        for name in ("app-icon.svg", "app-icon-dark.svg"):
            svg = (BRAND / name).read_text(encoding="utf-8")
            self.assertRegex(svg, r'viewBox="0 0 1024 1024"', name)
            self.assertNotIn("<image", svg, f"{name} must be vector art")
            # the squircle body spans x 100..924 on the 1024 canvas
            coordinates = [float(x) for x in re.findall(r"[ML](\d+\.?\d*),", svg.split('<path d="', 2)[2])]
            self.assertAlmostEqual(min(coordinates), 100, delta=1, msg=name)
            self.assertAlmostEqual(max(coordinates), 924, delta=1, msg=name)

    def test_dmg_background_svg_is_the_window_size(self) -> None:
        svg = (BRAND / "dmg-background.svg").read_text(encoding="utf-8")
        self.assertIn('viewBox="0 0 660 400"', svg)
        self.assertIn("Drag Island to Applications", svg)

    def test_dmg_backgrounds_are_1x_and_2x_with_matching_dpi(self) -> None:
        self.assertEqual(png_size(BRAND / "dmg-background.png"), (660, 400))
        self.assertEqual(png_size(BRAND / "dmg-background@2x.png"), (1320, 800))
        for name, dpi in (("dmg-background.png", 72), ("dmg-background@2x.png", 144)):
            data = (BRAND / name).read_bytes()
            index = data.index(b"pHYs")
            per_metre, _, _ = struct.unpack(">IIB", data[index + 4: index + 13])
            self.assertAlmostEqual(per_metre / 39.3701, dpi, delta=1, msg=name)

    def test_dmg_label_plates_stay_readable_with_black_and_white_labels(self) -> None:
        # Finder draws labels black (light) or white (dark) over a fixed picture, so the plates
        # behind them need a mid luminance that contrasts with both.
        svg = (BRAND / "dmg-background.svg").read_text(encoding="utf-8")
        plate = re.search(r'rx="15" fill="(#[0-9A-Fa-f]{6})"/>', svg)
        self.assertIsNotNone(plate)
        channels = [int(plate.group(1)[i: i + 2], 16) / 255 for i in (1, 3, 5)]
        lin = [c / 12.92 if c <= 0.04045 else ((c + 0.055) / 1.055) ** 2.4 for c in channels]
        luminance = 0.2126 * lin[0] + 0.7152 * lin[1] + 0.0722 * lin[2]
        self.assertGreaterEqual(1.05 / (luminance + 0.05), 4.0)  # white labels
        self.assertGreaterEqual((luminance + 0.05) / 0.05, 4.0)  # black labels


@final
class GeneratedIconTests(unittest.TestCase):
    def test_icns_has_every_retina_size(self) -> None:
        types = icns_types(MAIN / "macos" / "island.icns")
        for kind in ("ic04", "ic05", "ic07", "ic08", "ic09", "ic10", "ic11", "ic12", "ic13", "ic14"):
            self.assertIn(kind, types)

    def test_ico_holds_png_images_from_16_to_256(self) -> None:
        data = (MAIN / "windows" / "island.ico").read_bytes()
        reserved, kind, count = struct.unpack("<HHH", data[:6])
        self.assertEqual((reserved, kind), (0, 1))
        sizes = []
        for index in range(count):
            width, height, _, _, planes, bits, length, offset = struct.unpack("<BBBBHHII", data[6 + 16 * index: 22 + 16 * index])
            side = width or 256
            self.assertEqual(side, height or 256)
            self.assertEqual(data[offset: offset + 8], b"\x89PNG\r\n\x1a\n")
            self.assertEqual(struct.unpack(">II", data[offset + 16: offset + 24]), (side, side))
            sizes.append(side)
        self.assertEqual(sizes, [16, 24, 32, 48, 64, 128, 256])

    def test_linux_pngs_match_their_names_and_the_deb_ships_them(self) -> None:
        for size in installers.LINUX_ICON_SIZES:
            self.assertEqual(png_size(MAIN / "linux" / "icons" / f"island-browser-{size}.png"), (size, size))
        self.assertEqual((MAIN / "linux" / "island-browser.svg").read_bytes(), (BRAND / "app-icon.svg").read_bytes())


@final
class IconWiringTests(unittest.TestCase):
    def test_macos_bundle_names_the_icon_and_cmake_copies_it(self) -> None:
        self.assertIn("<key>CFBundleIconFile</key>\n    <string>island</string>", (MAIN / "Info.plist.in").read_text())
        cmake = (MAIN / "CMakeLists.txt").read_text()
        self.assertIn("macos/island.icns", cmake)
        self.assertIn("MACOSX_PACKAGE_LOCATION Resources", cmake)

    def test_windows_exe_and_installer_use_the_ico(self) -> None:
        self.assertIn('ICON "island.ico"', (MAIN / "windows" / "island_browser.rc").read_text())
        self.assertIn("island_browser.rc", (REPOSITORY / "cmake" / "platform" / "windows.cmake").read_text())
        iss = (MAIN / "windows" / "island_browser.iss").read_text()
        self.assertIn("SetupIconFile=island.ico", iss)
        self.assertIn('IconFilename: "{app}\\island.ico"', iss)

    def test_linux_desktop_entry_names_the_theme_icon(self) -> None:
        self.assertIn("Icon=island-browser\n", (MAIN / "linux" / "island-browser.desktop").read_text())


@final
@unittest.skipUnless(sys.platform == "darwin", "tiffutil and hdiutil are macOS tools")
class DmgLookTests(unittest.TestCase):
    def test_stage_adds_a_retina_background_and_the_volume_icon(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            stage = Path(temporary)
            installers.stage_dmg_look(stage)
            tiff = (stage / ".background" / "background.tiff").read_bytes()
            self.assertIn(tiff[:4], (b"MM\x00*", b"II*\x00"))
            self.assertEqual((stage / ".VolumeIcon.icns").read_bytes(), (MAIN / "macos" / "island.icns").read_bytes())

    def test_layout_matches_the_background_and_the_two_icons(self) -> None:
        layout = installers.DMG_LAYOUT
        self.assertEqual(layout.window_size, (660, 400))
        self.assertEqual(set(layout.positions), {installers.MAC_APP_NAME, "Applications"})
        self.assertEqual((layout.volume_name, layout.background_dir, layout.background_name),
                         (installers.MAC_VOLUME_NAME, ".background", "background.tiff"))


if __name__ == "__main__":
    unittest.main()
