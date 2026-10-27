from __future__ import annotations

import dataclasses
import importlib.util
import plistlib
import struct
import sys
import unittest
from pathlib import Path
from typing import final, override

REPOSITORY = Path(__file__).resolve().parents[2]
_spec = importlib.util.spec_from_file_location("island_dmg_ds_store", REPOSITORY / "scripts" / "dmg_ds_store.py")
assert _spec is not None and _spec.loader is not None
dmg_ds_store = importlib.util.module_from_spec(_spec)
sys.modules["island_dmg_ds_store"] = dmg_ds_store  # dataclasses resolve their module by name
_spec.loader.exec_module(dmg_ds_store)

LAYOUT = dmg_ds_store.Layout(
    volume_name="Island", background_dir=".background", background_name="background.tiff",
    window_origin=(200, 120), window_size=(660, 400), icon_size=128, text_size=13,
    positions={"Island.app": (165, 140), "Applications": (495, 140)},
    volume_created=1_700_000_000, file_created=1_700_000_100, folder_cnid=21, file_cnid=22)


def read(data: bytes) -> dict[tuple[str, str], bytes]:
    """A minimal .DS_Store reader: allocator, DSDB master, then the (single) leaf node."""
    assert data[:8] == b"\x00\x00\x00\x01Bud1"
    root_offset, root_size, again = struct.unpack(">III", data[8:20])
    assert root_offset == again

    def block(address: int) -> bytes:
        return data[4 + (address & ~0x1F): 4 + (address & ~0x1F) + (1 << (address & 0x1F))]

    root = data[4 + root_offset: 4 + root_offset + root_size]
    count, _ = struct.unpack(">II", root[:8])
    addresses = struct.unpack(f">{count}I", root[8: 8 + 4 * count])
    cursor = 8 + 4 * 256
    entries, = struct.unpack(">I", root[cursor: cursor + 4])
    cursor += 4
    names = {}
    for _ in range(entries):
        length = root[cursor]
        names[root[cursor + 1: cursor + 1 + length].decode()] = struct.unpack(
            ">I", root[cursor + 1 + length: cursor + 5 + length])[0]
        cursor += 5 + length
    node_block, levels, record_count, nodes, page = struct.unpack(">IIIII", block(addresses[names["DSDB"]])[:20])
    assert (levels, nodes, page) == (0, 1, 4096)
    node = block(addresses[node_block])
    children, records = struct.unpack(">II", node[:8])
    assert children == 0 and records == record_count
    cursor, found = 8, {}
    for _ in range(records):
        length, = struct.unpack(">I", node[cursor: cursor + 4])
        name = node[cursor + 4: cursor + 4 + 2 * length].decode("utf-16-be")
        cursor += 4 + 2 * length
        structure, kind = node[cursor: cursor + 4].decode(), node[cursor + 4: cursor + 8]
        cursor += 8
        if kind == b"blob":
            size, = struct.unpack(">I", node[cursor: cursor + 4])
            found[(name, structure)] = node[cursor + 4: cursor + 4 + size]
            cursor += 4 + size
        elif kind == b"long":
            found[(name, structure)] = node[cursor: cursor + 4]
            cursor += 4
        else:
            raise AssertionError(f"unexpected record type {kind!r}")
    return found


@final
class DmgDsStoreTests(unittest.TestCase):
    @override
    def setUp(self) -> None:
        self.data = dmg_ds_store.build(LAYOUT)
        self.found = read(self.data)

    def test_records_are_sorted_and_complete(self) -> None:
        self.assertEqual(set(self.found), {(".", "bwsp"), (".", "icvp"), (".", "vSrn"),
                                           ("Island.app", "Iloc"), ("Applications", "Iloc")})

    def test_window_bounds_add_the_title_bar_and_hide_the_chrome(self) -> None:
        window = plistlib.loads(self.found[(".", "bwsp")])
        self.assertEqual(window["WindowBounds"], "{{200, 120}, {660, 428}}")
        for key in ("ShowToolbar", "ShowSidebar", "ShowStatusBar", "ShowPathbar", "ShowTabView"):
            self.assertIs(window[key], False, key)

    def test_icon_positions_and_sizes(self) -> None:
        self.assertEqual(struct.unpack(">II", self.found[("Island.app", "Iloc")][:8]), (165, 140))
        self.assertEqual(struct.unpack(">II", self.found[("Applications", "Iloc")][:8]), (495, 140))
        view = plistlib.loads(self.found[(".", "icvp")])
        self.assertEqual((view["iconSize"], view["textSize"], view["arrangeBy"]), (128.0, 13.0, "none"))
        self.assertEqual(view["backgroundType"], 2)

    def test_background_alias_names_the_picture_and_the_volume(self) -> None:
        alias = plistlib.loads(self.found[(".", "icvp")])["backgroundImageAlias"]
        size, version = struct.unpack(">hh", alias[4:8])
        self.assertEqual((size, version), (len(alias), 2))
        self.assertEqual(alias[8:10], b"\x00\x00")  # kind: a file
        self.assertEqual(alias[10], len("Island"))
        self.assertIn("background.tiff".encode("utf-16-be"), alias)
        self.assertIn(b"/.background/background.tiff", alias)
        self.assertIn(b"/Volumes/Island", alias)
        self.assertEqual(alias[-4:], b"\xff\xff\x00\x00")
        self.assertEqual(alias[10 + 28: 10 + 32], struct.pack(">I", 1_700_000_000 + dmg_ds_store.HFS_EPOCH_OFFSET))

    def test_background_colour_fills_past_the_picture(self) -> None:
        layout = dataclasses.replace(LAYOUT, background_rgb=(0.25, 0.5, 0.75))
        view = plistlib.loads(read(dmg_ds_store.build(layout))[(".", "icvp")])
        self.assertEqual((view["backgroundColorRed"], view["backgroundColorGreen"],
                          view["backgroundColorBlue"]), (0.25, 0.5, 0.75))

    def test_file_is_deterministic_and_block_aligned(self) -> None:
        self.assertEqual(self.data, dmg_ds_store.build(LAYOUT))
        self.assertEqual(len(self.data), 4 + 4096 + 4096)

    def test_oversized_layouts_fail_loudly(self) -> None:
        huge = {f"{index:04}-{'x' * 60}": (1, 1) for index in range(60)}
        with self.assertRaises(ValueError):
            dmg_ds_store.build(dmg_ds_store.Layout(**{**LAYOUT.__dict__, "positions": huge}))


if __name__ == "__main__":
    unittest.main()
