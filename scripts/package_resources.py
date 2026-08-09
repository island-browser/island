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
    pngs: list[Path] = []
    for output in outputs:
        if not isinstance(output, dict):
            raise ResourceValidationError("icon manifest output is not an object")
        source_path = output.get("path")
        expected_digest = output.get("sha256")
        if not isinstance(source_path, str) or not source_path.startswith(ICON_OUTPUT_PREFIX):
            raise ResourceValidationError("icon manifest contains an invalid PNG path")
        if not isinstance(expected_digest, str) or len(expected_digest) != 64:
            raise ResourceValidationError(f"icon manifest has an invalid SHA-256: {source_path}")
        runtime_path = icon_root / source_path.removeprefix(ICON_OUTPUT_PREFIX)
        if runtime_path.is_symlink() or not runtime_path.is_file():
            raise ResourceValidationError(f"missing declared icon PNG: {runtime_path}")
        if hashlib.sha256(runtime_path.read_bytes()).hexdigest() != expected_digest:
            raise ResourceValidationError(f"icon PNG hash mismatch: {runtime_path}")
        pngs.append(runtime_path)
    return required + tuple(pngs)
