from __future__ import annotations

import json
import subprocess
import tempfile
import unittest
from pathlib import Path
from typing import final, override

from .package_fixture import PackageFixture


REPOSITORY = Path(__file__).resolve().parents[2]
PACKAGER = REPOSITORY / "scripts" / "package.py"
ICON_PREFIX = "resources/island/icons/"


@final
class PackageResourceTests(unittest.TestCase):
    @override
    def __init__(self, method_name: str = "runTest") -> None:
        super().__init__(method_name)
        self.temporary = tempfile.TemporaryDirectory()
        root = Path(self.temporary.name)
        self.build = root / "build"
        self.output = root / "output"
        self.fixture = PackageFixture(self.build, REPOSITORY)

    @override
    def tearDown(self) -> None:
        _ = self.temporary.cleanup()

    def test_rejects_traversal_absolute_backslash_and_duplicate_manifest_paths(self) -> None:
        for label, replacement in (
            ("traversal", "png/../outside.png"),
            ("absolute", "/outside.png"),
            ("backslash", "png\\outside.png"),
            ("duplicate", None),
        ):
            with self.subTest(label=label):
                manifest_path, manifest = self._stage_manifest()
                outputs = manifest["outputs"]
                if replacement is None:
                    outputs[1]["path"] = outputs[0]["path"]
                else:
                    source = manifest_path.parent / "png" / Path(outputs[0]["path"]).name
                    destination = manifest_path.parent / "outside.png"
                    _ = destination.write_bytes(source.read_bytes())
                    outputs[0]["path"] = ICON_PREFIX + replacement
                _ = manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

                result = self._run(check=False)

                self.assertNotEqual(result.returncode, 0)
                self.assertIn("icon manifest", result.stderr)

    def test_rejects_a_safe_looking_symlinked_icon(self) -> None:
        manifest_path, manifest = self._stage_manifest()
        output = manifest["outputs"][0]
        icon = manifest_path.parent / "png" / Path(output["path"]).name
        backing = icon.with_name("backing.png")
        _ = icon.replace(backing)
        _ = icon.symlink_to(backing.name)

        result = self._run(check=False)

        self.assertNotEqual(result.returncode, 0)
        self.assertIn("symlink", result.stderr)

    def test_accepts_a_valid_nested_icon_path(self) -> None:
        manifest_path, manifest = self._stage_manifest()
        output = manifest["outputs"][0]
        icon = manifest_path.parent / "png" / Path(output["path"]).name
        nested = icon.parent / "nested" / icon.name
        nested.parent.mkdir()
        _ = icon.replace(nested)
        output["path"] = ICON_PREFIX + f"png/nested/{icon.name}"
        _ = manifest_path.write_text(json.dumps(manifest), encoding="utf-8")

        result = self._run(check=False)

        self.assertEqual(result.returncode, 0, result.stderr)

    def _stage_manifest(self) -> tuple[Path, dict[str, list[dict[str, str]]]]:
        self.fixture.stage("linux64")
        manifest_path = self.build / "src/main/Release/resources/island/icons/manifest.json"
        manifest: dict[str, list[dict[str, str]]] = json.loads(
            manifest_path.read_text(encoding="utf-8")
        )
        return manifest_path, manifest

    def _run(self, check: bool) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["python3", str(PACKAGER), "--target", "linux64", "--build-dir", str(self.build), "--version", "1.2.3", "--output-dir", str(self.output)],
            check=check,
            text=True,
            capture_output=True,
        )
