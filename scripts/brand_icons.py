#!/usr/bin/env python3
"""Rasterize Island's brand SVGs and write the per-platform icon files.

Developer tool, run on macOS (needs `qlmanage`, `iconutil` and Pillow: `pip install -r
scripts/requirements-brand.txt`). CI never runs it: the outputs are committed under
`resources/brand/` and `src/main/{macos,windows,linux}/`, so the packaging path stays stdlib-only.

    python3 scripts/brand_icons.py generate        # rewrite every committed icon file
    python3 scripts/brand_icons.py install-concept <svg-light> <svg-dark> [<dmg-bg-svg>]

SVG has no portable rasterizer in the standard library, so `render_svg` asks QuickLook (WebKit)
for the pixels. QuickLook always paints an opaque white page, so each SVG is rendered twice, once
over white and once over black, and the alpha channel is recovered from the difference.
"""

from __future__ import annotations

import argparse
import base64
import re
import shutil
import struct
import subprocess
import sys
import tempfile
from pathlib import Path
from typing import Final

REPOSITORY: Final = Path(__file__).resolve().parents[1]
BRAND: Final = REPOSITORY / "resources" / "brand"
ICON_LIGHT: Final = BRAND / "app-icon.svg"
ICON_DARK: Final = BRAND / "app-icon-dark.svg"
DMG_BACKGROUND_SVG: Final = BRAND / "dmg-background.svg"
DMG_BACKGROUND_1X: Final = BRAND / "dmg-background.png"
DMG_BACKGROUND_2X: Final = BRAND / "dmg-background@2x.png"
DMG_SIZE: Final = (660, 400)
ICNS: Final = REPOSITORY / "src" / "main" / "macos" / "island.icns"
ICO: Final = REPOSITORY / "src" / "main" / "windows" / "island.ico"
LINUX_PNG_DIR: Final = REPOSITORY / "src" / "main" / "linux" / "icons"
LINUX_SVG: Final = REPOSITORY / "src" / "main" / "linux" / "island-browser.svg"
LINUX_SIZES: Final = (16, 32, 48, 64, 128, 256, 512)
ICO_SIZES: Final = (16, 24, 32, 48, 64, 128, 256)
# iconutil's .iconset names: (point size, scale) -> pixel size.
ICONSET: Final = (("16x16", 16), ("16x16@2x", 32), ("32x32", 32), ("32x32@2x", 64),
                  ("128x128", 128), ("128x128@2x", 256), ("256x256", 256),
                  ("256x256@2x", 512), ("512x512", 512), ("512x512@2x", 1024))


class BrandError(RuntimeError):
    pass


def _pillow():
    try:
        from PIL import Image, ImageChops
    except ImportError as error:  # pragma: no cover - environment dependent
        raise BrandError("Pillow is required: pip install -r scripts/requirements-brand.txt") from error
    return Image, ImageChops


def _view_box(svg: str) -> tuple[float, float, float, float]:
    match = re.search(r'<svg\b[^>]*\bviewBox="([^"]+)"', svg)
    if match is None:
        raise BrandError("the SVG root needs a viewBox")
    x, y, width, height = (float(part) for part in match.group(1).replace(",", " ").split())
    return x, y, width, height


def _with_backdrop(svg: str, colour: str) -> str:
    x, y, width, height = _view_box(svg)
    root_end = svg.index(">", svg.index("<svg")) + 1
    rect = f'<rect x="{x}" y="{y}" width="{width}" height="{height}" fill="{colour}"/>'
    return svg[:root_end] + rect + svg[root_end:]


def _quicklook(svg_text: str, size: int, workdir: Path, name: str):
    Image, _ = _pillow()
    source = workdir / f"{name}.svg"
    source.write_text(svg_text, encoding="utf-8")
    if sys.platform != "darwin" or shutil.which("qlmanage") is None:
        raise BrandError("rasterizing needs macOS (qlmanage)")
    subprocess.run(["qlmanage", "-t", "-s", str(size), "-o", str(workdir), str(source)],
                   check=True, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    produced = workdir / f"{name}.svg.png"
    if not produced.is_file():
        raise BrandError(f"qlmanage produced nothing for {name}")
    return Image.open(produced).convert("RGB")


def _squared(svg: str) -> tuple[str, float]:
    """QuickLook thumbnails are square; pad a wide viewBox to a square (content top-left).

    Returns the new SVG and height/width, so the caller can crop the padding off again.
    """
    x, y, width, height = _view_box(svg)
    if width == height:
        return svg, 1.0
    side = max(width, height)
    root_end = svg.index(">", svg.index("<svg"))
    root = re.sub(r'\bviewBox="[^"]+"', f'viewBox="{x} {y} {side} {side}"', svg[:root_end])
    root = re.sub(r'\b(width|height)="[^"]+"', "", root)
    root += ' preserveAspectRatio="xMinYMin meet"'
    return root + svg[root_end:], height / width


def render_svg(svg_path: Path, size: int, *, opaque: bool = False):
    """Render |svg_path| at |size| pixels wide (its aspect ratio kept) as a Pillow RGBA image."""
    image = _render_square(svg_path, size, opaque)
    _, ratio = _squared(svg_path.read_text(encoding="utf-8"))
    if ratio != 1.0:
        image = image.crop((0, 0, size, round(size * ratio)))
    return image


def _render_square(svg_path: Path, size: int, opaque: bool):
    Image, ImageChops = _pillow()
    svg, _ = _squared(svg_path.read_text(encoding="utf-8"))
    with tempfile.TemporaryDirectory() as temporary:
        work = Path(temporary)
        white = _quicklook(_with_backdrop(svg, "#ffffff"), size, work, "w")
        if opaque:
            return white.convert("RGBA")
        black = _quicklook(_with_backdrop(svg, "#000000"), size, work, "b")
    # over white W = c*a + 255*(1-a); over black B = c*a, so a = 1 - (W - B)/255, c = B/a.
    difference = ImageChops.subtract(white, black).convert("L")
    alpha = difference.point(lambda value: 255 - value)
    colour = Image.new("RGB", white.size)
    black_pixels, alpha_pixels = black.load(), alpha.load()
    out = colour.load()
    for y in range(white.size[1]):
        for x in range(white.size[0]):
            a = alpha_pixels[x, y]
            if a == 0:
                out[x, y] = (0, 0, 0)
            else:
                r, g, b = black_pixels[x, y]
                out[x, y] = (min(255, r * 255 // a), min(255, g * 255 // a), min(255, b * 255 // a))
    image = colour.convert("RGBA")
    image.putalpha(alpha)
    return image


def render_sizes(svg_path: Path, sizes) -> dict[int, "Image"]:
    return {size: render_svg(svg_path, size) for size in sorted(set(sizes))}


def write_ico(images: dict[int, "Image"], destination: Path) -> None:
    """A PNG-compressed .ico (Vista and later); no Pillow encoder quirks."""
    import io

    blobs = []
    for size, image in sorted(images.items()):
        buffer = io.BytesIO()
        image.save(buffer, "PNG", optimize=True)
        blobs.append((size, buffer.getvalue()))
    header = struct.pack("<HHH", 0, 1, len(blobs))
    offset = 6 + 16 * len(blobs)
    entries, payload = b"", b""
    for size, blob in blobs:
        entries += struct.pack("<BBBBHHII", size % 256, size % 256, 0, 0, 1, 32, len(blob), offset)
        payload += blob
        offset += len(blob)
    destination.parent.mkdir(parents=True, exist_ok=True)
    destination.write_bytes(header + entries + payload)


def write_icns(light: dict[int, "Image"], destination: Path) -> None:
    if shutil.which("iconutil") is None:
        raise BrandError("iconutil is required (macOS)")
    with tempfile.TemporaryDirectory() as temporary:
        iconset = Path(temporary) / "Island.iconset"
        iconset.mkdir()
        for name, pixels in ICONSET:
            light[pixels].save(iconset / f"icon_{name}.png", "PNG")
        destination.parent.mkdir(parents=True, exist_ok=True)
        subprocess.run(["iconutil", "-c", "icns", str(iconset), "-o", str(destination)], check=True)


def generate_files(output_root: Path = REPOSITORY) -> list[Path]:
    """Write every derived icon file under |output_root|; returns the paths written."""
    sizes = {pixels for _, pixels in ICONSET} | set(ICO_SIZES) | set(LINUX_SIZES)
    images = render_sizes(ICON_LIGHT, sizes)
    written: list[Path] = []

    def under(path: Path) -> Path:
        return output_root / path.relative_to(REPOSITORY)

    write_icns(images, under(ICNS))
    written.append(under(ICNS))
    write_ico({size: images[size] for size in ICO_SIZES}, under(ICO))
    written.append(under(ICO))
    png_dir = under(LINUX_PNG_DIR)
    png_dir.mkdir(parents=True, exist_ok=True)
    for size in LINUX_SIZES:
        target = png_dir / f"island-browser-{size}.png"
        images[size].save(target, "PNG", optimize=True)
        written.append(target)
    shutil.copyfile(ICON_LIGHT, under(LINUX_SVG))
    written.append(under(LINUX_SVG))
    if DMG_BACKGROUND_SVG.is_file():
        for target, scale in ((under(DMG_BACKGROUND_1X), 1), (under(DMG_BACKGROUND_2X), 2)):
            render_svg(DMG_BACKGROUND_SVG, DMG_SIZE[0] * scale, opaque=True).convert("RGB").save(
                target, "PNG", optimize=True, dpi=(72 * scale, 72 * scale))
            written.append(target)
    return written


def install_concept(light: Path, dark: Path, dmg: Path | None) -> None:
    shutil.copyfile(light, ICON_LIGHT)
    shutil.copyfile(dark, ICON_DARK)
    if dmg is not None:
        shutil.copyfile(dmg, DMG_BACKGROUND_SVG)
    generate_files()


def png_size(path: Path) -> tuple[int, int]:
    data = path.read_bytes()[:24]
    if data[:8] != b"\x89PNG\r\n\x1a\n":
        raise BrandError(f"{path} is not a PNG")
    return struct.unpack(">II", data[16:24])


def data_uri(path: Path) -> str:
    return "data:image/png;base64," + base64.b64encode(path.read_bytes()).decode("ascii")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("generate")
    pick = sub.add_parser("install-concept")
    pick.add_argument("light", type=Path)
    pick.add_argument("dark", type=Path)
    pick.add_argument("dmg", type=Path, nargs="?")
    args = parser.parse_args(argv)
    try:
        if args.command == "install-concept":
            install_concept(args.light, args.dark, args.dmg)
        else:
            for path in generate_files():
                print(path.relative_to(REPOSITORY))
    except BrandError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
