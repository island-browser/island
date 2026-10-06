from __future__ import annotations

import hashlib
import importlib.util
import io
import shutil
import subprocess
import sys
import tarfile
import tempfile
import unittest
from pathlib import Path
from typing import final, override


REPOSITORY = Path(__file__).resolve().parents[2]


def _load(name: str, path: Path):  # noqa: ANN202 - a module object
    spec = importlib.util.spec_from_file_location(name, path)
    assert spec is not None and spec.loader is not None
    module = importlib.util.module_from_spec(spec)
    sys.modules[name] = module  # dataclasses resolve their module by name
    spec.loader.exec_module(module)
    return module


# scripts/installers.py does `from package import ...`, which here would find this tests.package
# package, so scripts/package.py stands in for it while installers.py loads.
_saved = sys.modules.get("package")
sys.path.insert(0, str(REPOSITORY / "scripts"))  # package.py imports package_resources
try:
    sys.modules["package"] = _load("island_package_tool", REPOSITORY / "scripts" / "package.py")
    installers = _load("island_installers_tool", REPOSITORY / "scripts" / "installers.py")
finally:
    sys.path.remove(str(REPOSITORY / "scripts"))
    if _saved is None:
        del sys.modules["package"]
    else:
        sys.modules["package"] = _saved
Target = installers.Target


@final
class InstallerTests(unittest.TestCase):
    """scripts/installers.py: the installer beside each release archive."""

    @override
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    @override
    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_names_one_installer_per_target(self) -> None:
        self.assertEqual(installers.installer_name("0.5.0", Target.MACOS_ARM64), "island_browser-0.5.0-macosarm64.dmg")
        self.assertEqual(installers.installer_name("0.5.0", Target.LINUX_X64), "island_browser-0.5.0-linux64.deb")
        self.assertEqual(installers.installer_name("0.5.0", Target.WINDOWS_ARM64), "island_browser-0.5.0-windowsarm64-setup.exe")

    def test_record_checksum_appends_or_replaces_a_line(self) -> None:
        (self.root / "SHA256SUMS.txt").write_text(f"{'a' * 64}  island_browser-0.5.0-linux64.tar.gz\n", encoding="utf-8")
        installer = self.root / "island_browser-0.5.0-linux64.deb"
        installer.write_bytes(b"one")
        installers.record_checksum(self.root, installer)
        installer.write_bytes(b"two")
        installers.record_checksum(self.root, installer)
        self.assertEqual((self.root / "SHA256SUMS.txt").read_text(encoding="utf-8"),
                         f"{'a' * 64}  island_browser-0.5.0-linux64.tar.gz\n"
                         f"{hashlib.sha256(b'two').hexdigest()}  island_browser-0.5.0-linux64.deb\n")

    def test_debian_versions_sort_pre_releases_first(self) -> None:
        self.assertEqual(installers.deb_version("0.5.0"), "0.5.0")
        self.assertEqual(installers.deb_version("0.5.0-nightly.6"), "0.5.0~nightly.6")
        self.assertEqual(installers.deb_version("0.5.0-beta.1-nightly.3"), "0.5.0~beta.1-nightly.3")
        if shutil.which("dpkg") is not None:
            result = subprocess.run(["dpkg", "--compare-versions", "0.5.0~nightly.6", "lt", "0.5.0"], check=False)
            self.assertEqual(result.returncode, 0)

    def test_macos_signs_inside_out(self) -> None:
        app = self.root / "island_browser.app"
        framework = app / "Contents/Frameworks/Chromium Embedded Framework.framework"
        (framework / "Libraries").mkdir(parents=True)
        (framework / "Libraries/libEGL.dylib").write_bytes(b"")
        (app / "Contents/Frameworks/island_browser Helper.app").mkdir()
        (app / "Contents/Frameworks/island_browser Helper (GPU).app").mkdir()
        (app / "Contents/MacOS").mkdir()
        (app / "Contents/MacOS/island_browser").write_bytes(b"")
        (app / "Contents/MacOS/island_mcp_bridge").write_bytes(b"")
        self.assertEqual([path.relative_to(self.root).as_posix() for path in installers.macos_nested_code(app)], [
            "island_browser.app/Contents/Frameworks/Chromium Embedded Framework.framework/Libraries/libEGL.dylib",
            "island_browser.app/Contents/Frameworks/Chromium Embedded Framework.framework",
            "island_browser.app/Contents/Frameworks/island_browser Helper (GPU).app",
            "island_browser.app/Contents/Frameworks/island_browser Helper.app",
            "island_browser.app/Contents/MacOS/island_mcp_bridge",
            "island_browser.app",
        ])

    def test_windows_installer_command(self) -> None:
        command = installers.iscc_command("ISCC.exe", "0.5.0", Target.WINDOWS_ARM64, Path("C:/stage/app"),
                                          Path("C:/dist/island_browser-0.5.0-windowsarm64-setup.exe"))
        self.assertEqual(command[:2], ["ISCC.exe", "/Q"])
        self.assertIn("/DAppVersion=0.5.0", command)
        self.assertIn("/DArch=arm64", command)
        self.assertIn("/DOutputBaseFilename=island_browser-0.5.0-windowsarm64-setup", command)
        self.assertTrue(command[-1].endswith("island_browser.iss"))
        script = Path(command[-1]).read_text(encoding="utf-8")
        for define in ("AppVersion", "SourceDir", "OutputDir", "OutputBaseFilename", "Arch"):
            self.assertIn(f"{{#{define}}}", script)
        self.assertIn("PrivilegesRequired=lowest", script)

    def test_requires_the_package_output_first(self) -> None:
        with self.assertRaises(installers.InstallerError):
            installers.build(Target.LINUX_X64, "0.5.0", self.root)

    @unittest.skipIf(shutil.which("dpkg-deb") is None, "dpkg-deb is not installed")
    def test_builds_a_debian_package_from_the_archive(self) -> None:
        archive = self.root / "island_browser-0.5.0-nightly.6-linuxarm64.tar.gz"
        with tarfile.open(archive, "w:gz") as tar:
            for name, mode in (("island_browser", 0o755), ("chrome-sandbox", 0o755), ("locales/en-US.pak", 0o644)):
                info = tarfile.TarInfo(name)
                info.size, info.mode = 1, mode
                tar.addfile(info, io.BytesIO(b"x"))
        (self.root / "SHA256SUMS.txt").write_text(f"{'b' * 64}  {archive.name}\n", encoding="utf-8")
        deb = installers.build(Target.LINUX_ARM64, "0.5.0-nightly.6", self.root)
        self.assertEqual(deb.name, "island_browser-0.5.0-nightly.6-linuxarm64.deb")
        fields = subprocess.run(["dpkg-deb", "--field", str(deb), "Package", "Version", "Architecture"],
                                capture_output=True, text=True, check=True).stdout
        self.assertEqual(fields, "Package: island-browser\nVersion: 0.5.0~nightly.6\nArchitecture: arm64\n")
        listing = subprocess.run(["dpkg-deb", "--contents", str(deb)], capture_output=True, text=True, check=True).stdout
        self.assertRegex(listing, r"-rwsr-xr-x root/root .* \./opt/island-browser/chrome-sandbox")
        self.assertRegex(listing, r"-rwxr-xr-x root/root .* \./opt/island-browser/island_browser")
        self.assertIn("./opt/island-browser/locales/en-US.pak", listing)
        self.assertIn("./usr/bin/island-browser -> /opt/island-browser/island_browser", listing)
        self.assertIn("./usr/share/applications/island-browser.desktop", listing)
        self.assertIn("./usr/share/icons/hicolor/scalable/apps/island-browser.svg", listing)
        sums = (self.root / "SHA256SUMS.txt").read_text(encoding="utf-8").splitlines()
        self.assertEqual(sums[1], f"{hashlib.sha256(deb.read_bytes()).hexdigest()}  {deb.name}")


if __name__ == "__main__":
    unittest.main()
