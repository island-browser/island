from __future__ import annotations

import hashlib
import json
from pathlib import Path
from typing import Final


FONT_FILES: Final = (
    "Geist-Regular.ttf",
    "Geist-Medium.ttf",
    "Geist-SemiBold.ttf",
    "Geist-Bold.ttf",
    "GeistMono-Regular.ttf",
    "GeistMono-Medium.ttf",
    "GeistMono-SemiBold.ttf",
    "GeistMono-Bold.ttf",
    "OFL.txt",
    "LICENSE.txt",
)
ICON_LICENSE: Final = "LICENSE-LUCIDE-ISC-AND-FEATHER-MIT.txt"
ICON_MANIFEST: Final = "manifest.json"
ICON_OUTPUT_PREFIX: Final = "resources/island/icons/"


class ResourceValidationError(Exception):
    pass


def runtime_resource_paths(resources: Path) -> tuple[Path, ...]:
    return tuple(resources / "fonts" / filename for filename in FONT_FILES) + (
        resources / "icons" / ICON_MANIFEST,
        resources / "icons" / ICON_LICENSE,
    )


def validate_runtime_resources(resources: Path) -> tuple[Path, ...]:
    required = runtime_resource_paths(resources)
    for path in required:
        if not path.is_file():
            raise ResourceValidationError(f"missing required chrome resource: {path}")

    manifest_path = resources / "icons" / ICON_MANIFEST
    try:
        manifest = json.loads(manifest_path.read_text(encoding="utf-8"))
    except json.JSONDecodeError as error:
        raise ResourceValidationError(f"invalid icon manifest: {manifest_path}") from error
    outputs = manifest.get("outputs")
    if not isinstance(outputs, list) or len(outputs) != 96:
        raise ResourceValidationError("icon manifest must declare exactly 96 PNG resources")

    icon_root = manifest_path.parent
    resolved_icon_root = icon_root.resolve()
    png_root = icon_root / "png"
    pngs: list[Path] = []
    declared_pngs: set[Path] = set()
    for output in outputs:
        if not isinstance(output, dict):
            raise ResourceValidationError("icon manifest output is not an object")
        source_path = output.get("path")
        expected_digest = output.get("sha256")
        if not isinstance(source_path, str) or not source_path.startswith(ICON_OUTPUT_PREFIX):
            raise ResourceValidationError("icon manifest contains an invalid PNG path")
        if not isinstance(expected_digest, str) or len(expected_digest) != 64:
            raise ResourceValidationError(f"icon manifest has an invalid SHA-256: {source_path}")
        relative_path = source_path.removeprefix(ICON_OUTPUT_PREFIX)
        components = relative_path.split("/")
        if (
            "\\" in source_path
            or not components
            or any(component in {"", ".", ".."} for component in components)
            or components[0] != "png"
            or not components[-1].endswith(".png")
        ):
            raise ResourceValidationError(f"icon manifest contains an unsafe PNG path: {source_path}")
        runtime_path = icon_root.joinpath(*components)
        if Path(relative_path).is_absolute() or any(
            icon_root.joinpath(*components[:index]).is_symlink()
            for index in range(1, len(components) + 1)
        ):
            raise ResourceValidationError(f"icon manifest contains a symlinked PNG path: {source_path}")
        resolved_runtime_path = runtime_path.resolve()
        if not resolved_runtime_path.is_relative_to(resolved_icon_root):
            raise ResourceValidationError(f"icon manifest PNG escapes its resource root: {source_path}")
        if resolved_runtime_path in declared_pngs:
            raise ResourceValidationError(f"icon manifest contains a duplicate PNG path: {source_path}")
        if not runtime_path.is_file():
            raise ResourceValidationError(f"missing declared icon PNG: {runtime_path}")
        if hashlib.sha256(runtime_path.read_bytes()).hexdigest() != expected_digest:
            raise ResourceValidationError(f"icon PNG hash mismatch: {runtime_path}")
        declared_pngs.add(resolved_runtime_path)
        pngs.append(runtime_path)
    expected_pngs: set[Path] = set()
    for path in png_root.rglob("*"):
        if path.is_symlink():
            raise ResourceValidationError(f"icon PNG directory contains a symlink: {path}")
        if path.is_file():
            if path.suffix != ".png":
                raise ResourceValidationError(f"icon PNG directory contains a non-PNG resource: {path}")
            expected_pngs.add(path.resolve())
    if declared_pngs != expected_pngs:
        raise ResourceValidationError("icon manifest PNG paths do not match staged resources")
    return required + tuple(pngs)
