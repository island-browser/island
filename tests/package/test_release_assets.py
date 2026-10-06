from __future__ import annotations

import hashlib
import subprocess
import sys
import tempfile
import unittest
from pathlib import Path
from typing import final, override


REPOSITORY = Path(__file__).resolve().parents[2]
ASSEMBLER = REPOSITORY / "scripts" / "release_assets.py"
TARGETS = {"macosx64": ".zip", "macosarm64": ".zip", "windows64": ".zip", "windowsarm64": ".zip",
           "linux64": ".tar.gz", "linuxarm64": ".tar.gz"}
INSTALLERS = {"macosx64": ".dmg", "macosarm64": ".dmg", "windows64": "-setup.exe",
              "windowsarm64": "-setup.exe", "linux64": ".deb", "linuxarm64": ".deb"}


@final
class ReleaseAssetsTests(unittest.TestCase):
    """scripts/release_assets.py: the release asset set and the combined SHA256SUMS.txt that the
    in-browser updater verifies downloads against."""

    @override
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self._use("case")

    def _use(self, name: str) -> None:
        """Points staging and output at a fresh pair of directories under the temporary root."""
        self.staging = self.root / name / "staging"
        self.output = self.root / name / "release-dist"

    @override
    def tearDown(self) -> None:
        self.temporary.cleanup()

    def _stage(self, version: str, targets: dict[str, str] = TARGETS, installers: bool = True) -> dict[str, str]:
        """Lays out artifacts like `gh run download`: one directory per artifact, each holding an
        archive and an installer and the SHA256SUMS.txt written beside them."""
        digests: dict[str, str] = {}
        for target, suffix in targets.items():
            directory = self.staging / f"unsigned-candidate-{version}-{target}"
            directory.mkdir(parents=True)
            names = [f"island_browser-{version}-{target}{suffix}"]
            if installers:
                names.append(f"island_browser-{version}-{target}{INSTALLERS[target]}")
            lines = ""
            for name in names:
                data = f"{name} contents\n".encode()
                (directory / name).write_bytes(data)
                digests[name] = hashlib.sha256(data).hexdigest()
                lines += f"{digests[name]}  {name}\n"
            (directory / "SHA256SUMS.txt").write_text(lines, encoding="utf-8")
        return digests

    def _run(self, version: str) -> subprocess.CompletedProcess[str]:
        return subprocess.run([sys.executable, str(ASSEMBLER), "--staging", str(self.staging),
                               "--version", version, "--output", str(self.output)],
                              capture_output=True, text=True, check=False)

    def test_assembles_archives_installers_and_a_combined_checksum_file(self) -> None:
        for version in ("0.4.0", "0.4.0-nightly.17", "0.5.0-beta.1-nightly.3"):
            with self.subTest(version=version):
                self._use(version)
                digests = self._stage(version)
                result = self._run(version)
                self.assertEqual(result.returncode, 0, result.stderr)
                sums = (self.output / "SHA256SUMS.txt").read_bytes().decode("utf-8")
                expected = "".join(f"{digests[name]}  {name}\n" for name in sorted(digests))
                self.assertEqual(sums, expected)
                self.assertNotIn("\r", sums)
                self.assertEqual(result.stdout, expected)
                self.assertEqual(sorted(path.name for path in self.output.iterdir()),
                                 sorted([*digests, "SHA256SUMS.txt"]))
                for name, digest in digests.items():
                    self.assertEqual(hashlib.sha256((self.output / name).read_bytes()).hexdigest(), digest)

    def test_requires_all_six_targets(self) -> None:
        self._stage("0.4.0", {target: suffix for target, suffix in TARGETS.items() if target != "windowsarm64"})
        result = self._run("0.4.0")
        self.assertEqual(result.returncode, 1)
        self.assertIn("island_browser-0.4.0-windowsarm64.zip", result.stderr)
        self.assertFalse(self.output.exists())

    def test_requires_an_installer_per_target(self) -> None:
        self._stage("0.5.0", installers=False)
        result = self._run("0.5.0")
        self.assertEqual(result.returncode, 1)
        for name in ("macosarm64.dmg", "macosx64.dmg", "linux64.deb", "linuxarm64.deb",
                     "windows64-setup.exe", "windowsarm64-setup.exe"):
            self.assertIn(f"island_browser-0.5.0-{name}", result.stderr)
        self.assertFalse(self.output.exists())

    def test_rejects_a_checksum_mismatch(self) -> None:
        self._stage("0.4.0")
        (self.staging / "unsigned-candidate-0.4.0-linux64" / "island_browser-0.4.0-linux64.tar.gz").write_bytes(b"tampered")
        result = self._run("0.4.0")
        self.assertEqual(result.returncode, 1)
        self.assertIn("checksum mismatch", result.stderr)

    def test_rejects_archives_of_another_version_or_unlisted_files(self) -> None:
        self._stage("0.4.0-nightly.1")
        result = self._run("0.4.0-nightly.2")
        self.assertEqual(result.returncode, 1)
        self.assertIn("unexpected", result.stderr)
        self._use("unlisted")
        self._stage("0.4.0")
        (self.staging / "unsigned-candidate-0.4.0-linux64" / "island_browser-0.4.0-extra.zip").write_bytes(b"x")
        result = self._run("0.4.0")
        self.assertEqual(result.returncode, 1)
        self.assertIn("not listed", result.stderr)

    def test_rejects_invalid_versions(self) -> None:
        for version in ("v0.4.0", "0.4", "0.4.0+build.1", "0.4.0-"):
            with self.subTest(version=version):
                result = self._run(version)
                self.assertEqual(result.returncode, 1)
                self.assertIn("SemVer", result.stderr)


if __name__ == "__main__":
    unittest.main()
