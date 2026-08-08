from __future__ import annotations

import hashlib
import subprocess
import tarfile
import tempfile
import unittest
import zipfile
from pathlib import Path
from typing import final, override


REPOSITORY = Path(__file__).resolve().parents[2]
PACKAGER = REPOSITORY / "scripts" / "package.py"


@final
class PackageTests(unittest.TestCase):
    @override
    def __init__(self, method_name: str = "runTest") -> None:
        super().__init__(method_name)
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.build = self.root / "build"
        self.output = self.root / "output"

    @override
    def tearDown(self) -> None:
        _ = self.temporary.cleanup()

    def test_packages_each_supported_layout_with_metadata_and_checksum(self) -> None:
        for target in ("macosarm64", "windows64", "linux64", "linuxarm64"):
            with self.subTest(target=target):
                self._stage(target)
                result = self._run(target)
                artifact = self.output / f"island_browser-1.2.3-{target}{'.tar.gz' if target.startswith('linux') else '.zip'}"
                self.assertEqual(result.stdout.strip(), str(artifact.resolve()))
                self.assertTrue(artifact.is_file())
                first_bytes = artifact.read_bytes()
                _ = self._run(target)
                self.assertEqual(artifact.read_bytes(), first_bytes)
                members = self._members(artifact)
                self.assertIn("build-metadata.json", members)
                self.assertIn("THIRD_PARTY_NOTICES.txt", members)
                if target.startswith("macos"):
                    self.assertIn("island_browser.app/Contents/MacOS/island_browser", members)
                    framework = "island_browser.app/Contents/Frameworks/Chromium Embedded Framework.framework/"
                    self.assertIn(f"{framework}Resources", members)
                    self.assertIn(f"{framework}Versions/A/Resources/en.lproj/locale.pak", members)
                    with zipfile.ZipFile(artifact) as archive:
                        self.assertEqual(archive.read(f"{framework}Resources"), b"Versions/A/Resources")
                metadata = self._read(artifact, "build-metadata.json")
                self.assertIn(f'"target":"{target}"', metadata)
                expected_format = "tar.gz" if target.startswith("linux") else "zip"
                self.assertIn(f'"format":"{expected_format}"', metadata)
                self.assertIn('"signed":false', metadata)
                self.assertIn('"notarized":false', metadata)
                self.assertIn('"publicReleaseEligible":false', metadata)
                checksum = (self.output / "SHA256SUMS.txt").read_text(encoding="utf-8")
                self.assertEqual(checksum, f"{hashlib.sha256(artifact.read_bytes()).hexdigest()}  {artifact.name}\n")

    def test_rejects_missing_runtime_before_writing_artifact(self) -> None:
        self._stage("windows64")
        _ = (self.build / "Release/resources.pak").unlink()
        result = self._run("windows64", check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("resources.pak", result.stderr)
        self.assertFalse(self.output.exists())

    def test_packages_complete_windows_sandbox_bootstrap_with_client_dll(self) -> None:
        self._stage("windows64", sandbox=True)
        _ = self._run("windows64")
        artifact = self.output / "island_browser-1.2.3-windows64.zip"
        self.assertIn("island_browser.dll", self._members(artifact))

    def test_rejects_windows_bootstrap_without_client_dll(self) -> None:
        self._stage("windows64", sandbox=True)
        _ = (self.build / "Release/island_browser.dll").unlink()
        result = self._run("windows64", check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("missing sandbox client DLL", result.stderr)

    def test_rejects_windows_client_dll_without_bootstrap(self) -> None:
        self._stage("windows64")
        self._write_binary(self.build / "Release/island_browser.dll", "windows64")
        result = self._run("windows64", check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("bootstrap/client DLL mismatch", result.stderr)

    def test_rejects_windows_sandbox_client_dll_architecture_mismatch(self) -> None:
        self._stage("windows64", sandbox=True)
        self._write_binary(self.build / "Release/island_browser.dll", "windowsarm64")
        result = self._run("windows64", check=False)
        self.assertNotEqual(result.returncode, 0)
        self.assertIn("architecture", result.stderr)

    def test_packages_complete_windows_non_sandbox_layout(self) -> None:
        self._stage("windows64")
        _ = self._run("windows64")
        artifact = self.output / "island_browser-1.2.3-windows64.zip"
        self.assertNotIn("island_browser.dll", self._members(artifact))

    def test_rejects_missing_mac_framework_or_helper_bundle(self) -> None:
        root = self.build / "src/main/island_browser.app/Contents/Frameworks"
        for path in (root / "Chromium Embedded Framework.framework/Chromium Embedded Framework", root / "island_browser Helper (Renderer).app/Contents/MacOS/island_browser Helper (Renderer)"):
            with self.subTest(path=path.name):
                self._stage("macosarm64")
                _ = path.unlink()
                result = self._run("macosarm64", check=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(path.name, result.stderr)

    def test_rejects_zip_and_tar_symlinks_that_escape_runtime_root(self) -> None:
        for target, library in (("windows64", "libcef.dll"), ("linux64", "libcef.so")):
            with self.subTest(target=target):
                self._stage(target)
                runtime = self.build / "Release"
                _ = (self.root / "outside").write_text("outside", encoding="utf-8")
                _ = (runtime / library).unlink()
                _ = (runtime / library).symlink_to("../../../outside")
                result = self._run(target, check=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn("symlink", result.stderr)
                self.assertFalse(self.output.exists())

    def test_rejects_invalid_versions_and_target_architecture_conflicts(self) -> None:
        self._stage("macosarm64")
        invalid = self._run("macosarm64", version="release", check=False)
        self.assertNotEqual(invalid.returncode, 0)
        self.assertIn("version", invalid.stderr)
        executable = self.build / "src/main/island_browser.app/Contents/MacOS/island_browser"
        _ = executable.write_bytes(b"\xcf\xfa\xed\xfe\x07\x00\x00\x01")
        mismatch = self._run("macosarm64", check=False)
        self.assertNotEqual(mismatch.returncode, 0)
        self.assertIn("architecture", mismatch.stderr)

    def test_rejects_unknown_and_fat_architecture_binaries(self) -> None:
        executable = self.build / "src/main/island_browser.app/Contents/MacOS/island_browser"
        for data, label in ((b"unknown", "unknown"), (b"\xca\xfe\xba\xbe", "fat")):
            with self.subTest(label=label):
                self._stage("macosarm64")
                _ = executable.write_bytes(data)
                result = self._run("macosarm64", check=False)
                self.assertNotEqual(result.returncode, 0)
                self.assertIn(label, result.stderr)

    def _run(self, target: str, version: str = "1.2.3", check: bool = True) -> subprocess.CompletedProcess[str]:
        return subprocess.run(
            ["python3", str(PACKAGER), "--target", target, "--build-dir", str(self.build), "--version", version,
             "--output-dir", str(self.output)],
            check=check,
            text=True,
            capture_output=True,
        )

    def _stage(self, target: str, sandbox: bool = False) -> None:
        if target.startswith("macos"):
            root = self.build / "src/main/island_browser.app/Contents"
            self._write_binary(root / "MacOS/island_browser", target)
            self._write_binary(root / "Frameworks/Chromium Embedded Framework.framework/Chromium Embedded Framework", target)
            for suffix in ("", " (Alerts)", " (GPU)", " (Plugin)", " (Renderer)"):
                self._write_binary(root / f"Frameworks/island_browser Helper{suffix}.app/Contents/MacOS/island_browser Helper{suffix}", target)
            framework = root / "Frameworks/Chromium Embedded Framework.framework"
            for name in ("icudtl.dat", "resources.pak", "chrome_100_percent.pak", "chrome_200_percent.pak", "en.lproj/locale.pak"):
                self._write(framework / "Versions/A/Resources" / name)
            if not (framework / "Versions/Current").exists():
                _ = (framework / "Versions/Current").symlink_to("A")
            if not (framework / "Resources").exists():
                _ = (framework / "Resources").symlink_to("Versions/A/Resources")
            return
        root = self.build / "Release"
        self._write_binary(root / ("island_browser.exe" if target.startswith("windows") else "island_browser"), target, sandbox)
        for name in ("icudtl.dat", "resources.pak", "chrome_100_percent.pak", "chrome_200_percent.pak", "locales/en-US.pak"):
            self._write(root / name)
        if target.startswith("windows"):
            for name in ("libcef.dll", "chrome_elf.dll", "d3dcompiler_47.dll", "dxcompiler.dll", "dxil.dll", "libEGL.dll", "libGLESv2.dll", "snapshot_blob.bin", "v8_context_snapshot.bin", "vk_swiftshader.dll", "vulkan-1.dll"):
                self._write_binary(root / name, target)
            self._write(root / "vk_swiftshader_icd.json")
            if sandbox:
                self._write_binary(root / "island_browser.dll", target)
        else:
            for name in ("chrome-sandbox", "libcef.so", "libEGL.so", "libGLESv2.so", "libvk_swiftshader.so", "libvulkan.so.1", "snapshot_blob.bin", "v8_context_snapshot.bin"):
                self._write_binary(root / name, target)

    @staticmethod
    def _write(path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        _ = path.write_text("fixture", encoding="utf-8")

    @staticmethod
    def _write_binary(path: Path, target: str, bootstrap: bool = False) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        data = b""
        match target:
            case "macosx64":
                data = b"\xcf\xfa\xed\xfe\x07\x00\x00\x01"
            case "macosarm64":
                data = b"\xcf\xfa\xed\xfe\x0c\x00\x00\x01"
            case "windows64" | "windowsarm64":
                data = bytearray(70)
                data[:2], data[60:64], data[64:68] = b"MZ", (64).to_bytes(4, "little"), b"PE\x00\x00"
                data[68:70] = (0xAA64 if target.endswith("arm64") else 0x8664).to_bytes(2, "little")
                data = bytes(data)
            case "linux64" | "linuxarm64":
                data = b"\x7fELF" + b"\x00" * 14 + (183 if target.endswith("arm64") else 62).to_bytes(2, "little")
            case unexpected:
                raise AssertionError(f"unexpected fixture target: {unexpected}")
        if bootstrap:
            data += b"island_browser.dll\x00"
        _ = path.write_bytes(data)

    @staticmethod
    def _members(artifact: Path) -> set[str]:
        if artifact.suffix == ".zip":
            with zipfile.ZipFile(artifact) as archive:
                return set(archive.namelist())
        with tarfile.open(artifact) as archive:
            return {member.name for member in archive.getmembers()}

    @staticmethod
    def _read(artifact: Path, name: str) -> str:
        if artifact.suffix == ".zip":
            with zipfile.ZipFile(artifact) as archive:
                return archive.read(name).decode("utf-8")
        with tarfile.open(artifact) as archive:
            member = archive.extractfile(name)
            assert member is not None
            return member.read().decode("utf-8")


if __name__ == "__main__":
    _ = unittest.main()
