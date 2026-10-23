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


class IconCountAgreementTests(unittest.TestCase):
    def test_packager_icon_count_matches_the_cmake_gate(self) -> None:
        import re
        import sys

        sys.path.insert(0, str(REPOSITORY / "scripts"))
        try:
            from package_resources import EXPECTED_ICON_OUTPUTS
        finally:
            sys.path.pop(0)
        cmake = (REPOSITORY / "CMakeLists.txt").read_text(encoding="utf-8")
        match = re.search(r"_island_icon_output_count EQUAL (\d+)", cmake)
        self.assertIsNotNone(match)
        assert match is not None
        self.assertEqual(int(match.group(1)), EXPECTED_ICON_OUTPUTS)
        manifest = json.loads(
            (REPOSITORY / "resources/island/icons/manifest.json").read_text(encoding="utf-8")
        )
        self.assertEqual(len(manifest["outputs"]), EXPECTED_ICON_OUTPUTS)

    def test_cmake_stages_icons_without_expanding_every_path_on_the_command_line(self) -> None:
        cmake = (REPOSITORY / "CMakeLists.txt").read_text(encoding="utf-8")
        staging = cmake.split("function(island_stage_chrome_resources", 1)[1].split(
            "endfunction()", 1
        )[0]
        self.assertNotIn("copy_if_different ${ISLAND_ICON_PNG_RESOURCES}", staging)
        self.assertIn(
            'copy_directory\n            "${ISLAND_ICON_RESOURCE_DIRECTORY}/png"', staging
        )
