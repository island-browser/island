"""Writes the `.DS_Store` that lays out Island's dmg window, with no Finder session. Stdlib only.

A `.DS_Store` is a "buddy allocator" file holding one B-tree of Finder records. A dmg needs just
three kinds of record: the window itself (`.`: bounds, view options and background picture) and
the position of each item. They all fit in one leaf node, so this writer emits the smallest valid
file: a header, the allocator's root block, the `DSDB` master block and a single 4 KiB leaf.

Format notes: https://wiki.mozilla.org/DS_Store_File_Format and the `ds_store`/`mac_alias` Python
packages, which `tests/package/test_dmg_ds_store.py` mirrors with a small reader.
"""

from __future__ import annotations

import plistlib
import struct
from dataclasses import dataclass
from typing import Final

HFS_EPOCH_OFFSET: Final = 2082844800  # seconds from 1904-01-01 to 1970-01-01
NODE_SIZE: Final = 4096
ROOT_SIZE: Final = 2048
MASTER_OFFSET: Final = 32
ROOT_OFFSET: Final = 2048
NODE_OFFSET: Final = 4096


@dataclass(frozen=True)
class Layout:
    volume_name: str
    background_dir: str  # folder holding the picture, at the volume root
    background_name: str
    window_origin: tuple[int, int]
    window_size: tuple[int, int]  # content size in points; Finder adds the title bar
    icon_size: int
    text_size: int
    positions: dict[str, tuple[int, int]]  # item name -> icon centre in the window
    title_bar: int = 28
    # The real volume's identity, so Finder resolves the background alias exactly (see
    # `installers._build_dmg`, which reads them from the mounted image). Unix seconds and
    # catalog node ids; the defaults are placeholders Finder falls back from by name.
    volume_created: int = 0
    file_created: int = 0
    folder_cnid: int = 20
    file_cnid: int = 21


def _pascal(text: str, size: int) -> bytes:
    raw = text.encode("mac_roman", "replace")[: size - 1]
    return bytes([len(raw)]) + raw.ljust(size - 1, b"\0")


def alias_record(layout: Layout) -> bytes:
    """A version-2 alias to `<volume>/<background_dir>/<background_name>`.

    Finder resolves it by volume name and path when the ids of a real volume do not match, so
    the identifiers here are fixed placeholders.
    """
    volume_created = layout.volume_created + HFS_EPOCH_OFFSET
    file_created = layout.file_created + HFS_EPOCH_OFFSET
    fixed = struct.pack(">4shhh", b"\0\0\0\0", 0, 2, 0)
    fixed += _pascal(layout.volume_name, 28) + struct.pack(">I2sh", volume_created, b"H+", 0)
    fixed += struct.pack(">I", layout.folder_cnid) + _pascal(layout.background_name, 64)
    fixed += struct.pack(">II4s4s", layout.file_cnid, file_created, b"\0\0\0\0", b"\0\0\0\0")
    fixed += struct.pack(">hhI2s10s", -1, -1, 0x0D02, b"\0\0", b"\0" * 10)

    def tag(number: int, payload: bytes) -> bytes:
        return struct.pack(">hh", number, len(payload)) + payload + (b"\0" if len(payload) % 2 else b"")

    def utf16(text: str) -> bytes:
        return struct.pack(">h", len(text)) + text.encode("utf-16-be")

    extra = (tag(0, layout.background_dir.encode()) + tag(1, struct.pack(">I", layout.folder_cnid))
             + tag(14, utf16(layout.background_name)) + tag(15, utf16(layout.volume_name))
             + tag(18, f"/{layout.background_dir}/{layout.background_name}".encode())
             + tag(19, f"/Volumes/{layout.volume_name}".encode()) + struct.pack(">hh", -1, 0))
    record = fixed + extra
    return record[:4] + struct.pack(">h", len(record)) + record[6:]


def _blob(payload: bytes) -> tuple[bytes, bytes]:
    return b"blob", struct.pack(">I", len(payload)) + payload


def records(layout: Layout) -> list[tuple[str, bytes, bytes, bytes]]:
    """(item name, structure id, type, data) for every record, unsorted."""
    width, height = layout.window_size
    x, y = layout.window_origin
    bounds = f"{{{{{x}, {y}}}, {{{width}, {height + layout.title_bar}}}}}"
    window = {
        "WindowBounds": bounds, "ContainerShowSidebar": False, "ShowSidebar": False,
        "ShowToolbar": False, "ShowStatusBar": False, "ShowPathbar": False,
        "ShowTabView": False, "SidebarWidth": 0, "PreviewPaneVisibility": False,
    }
    view = {
        "viewOptionsVersion": 1, "backgroundType": 2,
        "backgroundImageAlias": alias_record(layout), "backgroundColorRed": 1.0,
        "backgroundColorGreen": 1.0, "backgroundColorBlue": 1.0, "iconSize": float(layout.icon_size),
        "textSize": float(layout.text_size), "arrangeBy": "none", "gridSpacing": 100.0,
        "gridOffsetX": 0.0, "gridOffsetY": 0.0, "labelOnBottom": True, "showIconPreview": True,
        "showItemInfo": False, "scrollPositionX": 0.0, "scrollPositionY": 0.0,
    }
    out = [(".", b"bwsp", *_blob(plistlib.dumps(window, fmt=plistlib.FMT_BINARY))),
           (".", b"icvp", *_blob(plistlib.dumps(view, fmt=plistlib.FMT_BINARY))),
           (".", b"vSrn", b"long", struct.pack(">I", 1))]
    for name, (px, py) in layout.positions.items():
        out.append((name, b"Iloc", *_blob(struct.pack(">IIIHH", px, py, 0xFFFFFFFF, 0xFFFF, 0))))
    return out


def _encode_record(name: str, structure: bytes, kind: bytes, data: bytes) -> bytes:
    return (struct.pack(">I", len(name)) + name.encode("utf-16-be") + structure + kind + data)


def build(layout: Layout) -> bytes:
    ordered = sorted(records(layout), key=lambda item: (item[0].lower(), item[1]))
    node = struct.pack(">II", 0, len(ordered)) + b"".join(_encode_record(*item) for item in ordered)
    if len(node) > NODE_SIZE:
        raise ValueError("the layout no longer fits one 4 KiB node")
    node = node.ljust(NODE_SIZE, b"\0")
    master = struct.pack(">IIIII", 2, 0, len(ordered), 1, NODE_SIZE).ljust(32, b"\0")
    # Block addresses are `offset | log2(size)`, offsets counted from byte 4 of the file.
    addresses = [ROOT_OFFSET | 11, MASTER_OFFSET | 5, NODE_OFFSET | 12]
    table = struct.pack(">II", len(addresses), 0) + b"".join(struct.pack(">I", a) for a in addresses)
    table += b"\0\0\0\0" * (256 - len(addresses))
    directory = struct.pack(">I", 1) + bytes([4]) + b"DSDB" + struct.pack(">I", 1)
    free = {6: [64], 7: [128], 8: [256], 9: [512], 10: [1024]}
    free.update({shift: [1 << shift] for shift in range(13, 32)})
    freelist = b"".join(struct.pack(">I", len(free.get(i, []))) + b"".join(
        struct.pack(">I", offset) for offset in free.get(i, [])) for i in range(32))
    root = (table + directory + freelist).ljust(ROOT_SIZE, b"\0")
    if len(table + directory + freelist) > ROOT_SIZE:
        raise ValueError("allocator root block overflow")
    # File offset 4 is allocator offset 0; block 0 (32 bytes) is the file header's remainder.
    allocator = bytearray(NODE_OFFSET + NODE_SIZE)
    allocator[0:32] = b"Bud1" + struct.pack(">III", ROOT_OFFSET, ROOT_SIZE, ROOT_OFFSET) + bytes(16)
    allocator[MASTER_OFFSET:MASTER_OFFSET + 32] = master
    allocator[ROOT_OFFSET:ROOT_OFFSET + ROOT_SIZE] = root
    allocator[NODE_OFFSET:NODE_OFFSET + NODE_SIZE] = node
    return b"\0\0\0\1" + bytes(allocator)
