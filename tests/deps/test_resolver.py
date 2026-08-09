from __future__ import annotations

import hashlib
import argparse
import importlib.util
import io
import json
import shutil
import subprocess
import tarfile
import tempfile
import unittest
from dataclasses import replace
from pathlib import Path
from unittest.mock import patch

from deps.install import InstallError, download, extract, install_cef, install_geist, verify
from deps.model import Artifact, ManifestError, load_lock, resolve_cef
from deps.updates import UpdateError, compare_digest, parse_sha256_sidecar


REPOSITORY = Path(__file__).resolve().parents[2]


class FixtureResponse:
    def __init__(self, data: bytes, headers: dict[str, str]) -> None:
        self.data = io.BytesIO(data)
        self.headers = headers

    def read(self, size: int) -> bytes:
        return self.data.read(size)

    def __enter__(self) -> FixtureResponse:
        return self

    def __exit__(self, exception_type: type[BaseException] | None, exception: BaseException | None, traceback: object | None) -> None:
        return None

    def geturl(self) -> str:
        return "https://example.test/file"


def make_archive(path: Path, files: dict[str, bytes], link: bool = False) -> None:
    mode = "w:gz" if path.suffix == ".gz" else "w:bz2"
    with tarfile.open(path, mode) as archive:
        for name, content in files.items():
            member = tarfile.TarInfo(name)
            member.size = len(content)
            archive.addfile(member, io.BytesIO(content))
        if link:
            member = tarfile.TarInfo("fixture/link")
            member.type = tarfile.SYMTYPE
            member.linkname = "outside"
            archive.addfile(member)


def load_cli():
    spec = importlib.util.spec_from_file_location("island_deps_cli", REPOSITORY / "scripts" / "deps.py")
    if spec is None or spec.loader is None:
        raise RuntimeError("unable to load dependency CLI")
    module = importlib.util.module_from_spec(spec)
    spec.loader.exec_module(module)
    return module


def copy_download(source: Path):
    def replacement(_: Artifact, destination: Path, __: frozenset[str]) -> None:
        shutil.copyfile(source, destination)

    return replacement


class ResolverTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "deps").mkdir()
        shutil.copyfile(REPOSITORY / "deps" / "dependencies.lock.json", self.root / "deps" / "dependencies.lock.json")
        self.lock = load_lock(self.root / "deps" / "dependencies.lock.json")
        self.artifact = resolve_cef(self.lock, "macosx64")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_load_lock_rejects_malformed_manifest(self) -> None:
        given = self.root / "broken.json"
        given.write_text("{", encoding="utf-8")
        with self.assertRaises(ManifestError):
            load_lock(given)

    def test_resolve_rejects_unsupported_target(self) -> None:
        with self.assertRaisesRegex(Exception, "Unsupported CEF target"):
            resolve_cef(self.lock, "plan9")

    def test_resolve_uses_target_specific_archive_bounds(self) -> None:
        expected = {"macosx64": 321634893, "macosarm64": 285042171, "windows64": 346862808, "windowsarm64": 323575662, "linux64": 667825046, "linuxarm64": 839173804}
        for target, size in expected.items():
            artifact = resolve_cef(self.lock, target)
            self.assertEqual(artifact.expected_archive_bytes, size)
            self.assertGreaterEqual(artifact.max_archive_bytes, size)

    def test_download_accepts_exact_target_bound(self) -> None:
        artifact = replace(Artifact("fixture", "all", "https://example.test/file", hashlib.sha256(b"complete").hexdigest(), "tar.bz2", "test"), expected_archive_bytes=8, max_archive_bytes=8)
        with patch("deps.install.urlopen", return_value=FixtureResponse(b"complete", {"Content-Length": "8"})):
            download(artifact, self.root / "exact", frozenset({"example.test"}))

    def test_download_rejects_hash_mismatch_before_extraction(self) -> None:
        given = Artifact("fixture", "all", "https://example.test/file", "0" * 64, "tar.bz2", "test")
        destination = self.root / "download"
        with patch("deps.install.urlopen", return_value=FixtureResponse(b"incorrect", {"Content-Length": "9"})):
            with self.assertRaisesRegex(InstallError, "SHA-256 mismatch"):
                download(given, destination, frozenset({"example.test"}))
        self.assertEqual(destination.read_bytes(), b"incorrect")

    def test_download_resumes_a_truncated_response(self) -> None:
        artifact = Artifact("fixture", "all", "https://example.test/file", hashlib.sha256(b"abcde").hexdigest(), "tar.bz2", "test")
        destination = self.root / "resumed-download"
        responses = [FixtureResponse(b"abc", {"Content-Length": "5"}), FixtureResponse(b"de", {"Content-Range": "bytes 3-4/5", "Content-Length": "2"})]
        with patch("deps.install.urlopen", side_effect=responses):
            download(artifact, destination, frozenset({"example.test"}))
        self.assertEqual(destination.read_bytes(), b"abcde")

    def test_download_hashes_a_complete_chunked_response(self) -> None:
        artifact = Artifact("fixture", "all", "https://example.test/file", hashlib.sha256(b"complete").hexdigest(), "tar.bz2", "test")
        destination = self.root / "chunked-download"
        with patch("deps.install.urlopen", return_value=FixtureResponse(b"complete", {})):
            download(artifact, destination, frozenset({"example.test"}))
        self.assertEqual(destination.read_bytes(), b"complete")

    def test_download_rejects_disallowed_redirect(self) -> None:
        class RedirectResponse(FixtureResponse):
            def geturl(self) -> str:
                return "https://untrusted.test/file"

        artifact = Artifact("fixture", "all", "https://example.test/file", hashlib.sha256(b"complete").hexdigest(), "tar.bz2", "test")
        with patch("deps.install.urlopen", return_value=RedirectResponse(b"complete", {"Content-Length": "8"})):
            with self.assertRaisesRegex(InstallError, "redirect is not allowlisted"):
                download(artifact, self.root / "redirect", frozenset({"example.test"}))

    def test_extract_rejects_member_count_limit(self) -> None:
        archive = self.root / "many.tar.bz2"
        make_archive(archive, {"fixture/one": b"1", "fixture/two": b"2"})
        with self.assertRaisesRegex(InstallError, "member count"):
            extract(archive, self.root / "many", replace(self.artifact, max_members=1))

    def test_extract_rejects_member_size_limit(self) -> None:
        archive = self.root / "large-member.tar.bz2"
        make_archive(archive, {"fixture/file": b"12345"})
        with self.assertRaisesRegex(InstallError, "member exceeds size"):
            extract(archive, self.root / "large-member", replace(self.artifact, max_member_bytes=4))

    def test_download_rejects_archive_size_limit(self) -> None:
        artifact = replace(Artifact("fixture", "all", "https://example.test/file", hashlib.sha256(b"complete").hexdigest(), "tar.bz2", "test"), max_archive_bytes=4)
        with patch("deps.install.urlopen", return_value=FixtureResponse(b"complete", {"Content-Length": "8"})):
            with self.assertRaisesRegex(InstallError, "archive size limit"):
                download(artifact, self.root / "large-download", frozenset({"example.test"}))

    def test_update_sidecar_parsing_compares_candidates(self) -> None:
        digest = "a" * 64
        self.assertEqual(parse_sha256_sidecar(f"{digest}  archive.tar.bz2\n".encode()), digest)
        self.assertEqual(compare_digest(digest, digest), "current")
        self.assertEqual(compare_digest(digest, "b" * 64), "changed")
        with self.assertRaises(UpdateError):
            parse_sha256_sidecar(b"not-a-digest")

    def test_extract_rejects_traversal_and_links(self) -> None:
        traversal = self.root / "traversal.tar.bz2"
        make_archive(traversal, {"../outside": b"bad"})
        with self.assertRaisesRegex(InstallError, "unsafe path"):
            extract(traversal, self.root / "traversal", self.artifact)
        link = self.root / "link.tar.bz2"
        make_archive(link, {"fixture/file": b"ok"}, link=True)
        with self.assertRaisesRegex(InstallError, "link or device"):
            extract(link, self.root / "link", self.artifact)

    def test_invalid_cef_install_preserves_existing_destination(self) -> None:
        old = self.root / "third_party" / "cef"
        old.mkdir(parents=True)
        (old / "old").write_text("keep", encoding="utf-8")
        bad = self.root / "bad.tar.bz2"
        make_archive(bad, {f"cef_binary_{self.lock.cef_version}_{self.artifact.target}/other": b"bad"})
        with patch("deps.install.download", copy_download(bad)):
            with self.assertRaisesRegex(InstallError, "cef_macros"):
                install_cef(self.root, self.lock, self.artifact, False)
        self.assertEqual((old / "old").read_text(encoding="utf-8"), "keep")

    def test_successful_install_and_offline_verify(self) -> None:
        cef = self.root / "cef.tar.bz2"
        cef_root = f"cef_binary_{self.lock.cef_version}_{self.artifact.target}"
        make_archive(cef, {f"{cef_root}/cmake/cef_macros.cmake": b"ok", f"{cef_root}/tests/cefsimple/main.cc": b"ok"})
        fonts = self.root / "fonts.tar.gz"
        geist_root = f"geist-font-{self.lock.geist_commit}"
        make_archive(fonts, {**{f"{geist_root}/fonts/{'GeistMono' if name.startswith('GeistMono-') else 'Geist'}/ttf/{name}": b"font" for name in self.lock.geist_files}, **{f"{geist_root}/{name}": b"license" for name in self.lock.geist_licenses}})
        with patch("deps.install.download", copy_download(cef)):
            install_cef(self.root, self.lock, self.artifact, False)
        with patch("deps.install.download", copy_download(fonts)):
            install_geist(self.root, self.lock, False)
        verify(self.root, self.lock, "macosx64")

    def test_verify_rejects_receipt_mismatch(self) -> None:
        cef = self.root / "cef.tar.bz2"
        cef_root = f"cef_binary_{self.lock.cef_version}_{self.artifact.target}"
        make_archive(cef, {f"{cef_root}/cmake/cef_macros.cmake": b"ok", f"{cef_root}/tests/cefsimple/main.cc": b"ok"})
        fonts = self.root / "fonts.tar.gz"
        geist_root = f"geist-font-{self.lock.geist_commit}"
        make_archive(fonts, {**{f"{geist_root}/fonts/{'GeistMono' if name.startswith('GeistMono-') else 'Geist'}/ttf/{name}": b"font" for name in self.lock.geist_files}, **{f"{geist_root}/{name}": b"license" for name in self.lock.geist_licenses}})
        with patch("deps.install.download", copy_download(cef)):
            install_cef(self.root, self.lock, self.artifact, False)
        with patch("deps.install.download", copy_download(fonts)):
            install_geist(self.root, self.lock, False)
        receipt = self.root / "third_party" / "cef" / ".island-dependency-receipt.json"
        receipt.write_text(json.dumps({"manifest_sha256": "wrong"}), encoding="utf-8")
        with self.assertRaisesRegex(InstallError, "receipt does not match"):
            verify(self.root, self.lock, "macosx64")

    def test_verify_rejects_tampered_installed_file(self) -> None:
        cef = self.root / "cef.tar.bz2"
        cef_root = f"cef_binary_{self.lock.cef_version}_{self.artifact.target}"
        make_archive(cef, {f"{cef_root}/cmake/cef_macros.cmake": b"ok", f"{cef_root}/tests/cefsimple/main.cc": b"ok"})
        fonts = self.root / "fonts.tar.gz"
        geist_root = f"geist-font-{self.lock.geist_commit}"
        make_archive(fonts, {**{f"{geist_root}/fonts/{'GeistMono' if name.startswith('GeistMono-') else 'Geist'}/ttf/{name}": b"font" for name in self.lock.geist_files}, **{f"{geist_root}/{name}": b"license" for name in self.lock.geist_licenses}})
        with patch("deps.install.download", copy_download(cef)):
            install_cef(self.root, self.lock, self.artifact, False)
        with patch("deps.install.download", copy_download(fonts)):
            install_geist(self.root, self.lock, False)
        (self.root / "third_party" / "cef" / "tests" / "cefsimple" / "main.cc").write_bytes(b"changed")
        with self.assertRaisesRegex(InstallError, "installed files"):
            verify(self.root, self.lock, "macosx64")

    def test_cli_skips_valid_existing_dependencies_without_network(self) -> None:
        cef = self.root / "cef.tar.bz2"
        cef_root = f"cef_binary_{self.lock.cef_version}_{self.artifact.target}"
        make_archive(cef, {f"{cef_root}/cmake/cef_macros.cmake": b"ok", f"{cef_root}/tests/cefsimple/main.cc": b"ok"})
        fonts = self.root / "fonts.tar.gz"
        geist_root = f"geist-font-{self.lock.geist_commit}"
        make_archive(fonts, {**{f"{geist_root}/fonts/{'GeistMono' if name.startswith('GeistMono-') else 'Geist'}/ttf/{name}": b"font" for name in self.lock.geist_files}, **{f"{geist_root}/{name}": b"license" for name in self.lock.geist_licenses}})
        with patch("deps.install.download", copy_download(cef)):
            install_cef(self.root, self.lock, self.artifact, False)
        with patch("deps.install.download", copy_download(fonts)):
            install_geist(self.root, self.lock, False)
        cli = load_cli()
        with patch.object(cli, "install_cef", side_effect=AssertionError("network")), patch.object(cli, "install_geist", side_effect=AssertionError("network")):
            self.assertEqual(cli._install(argparse.Namespace(root=str(self.root), target="macosx64", dry_run=False, force=False)), 0)

    def test_cli_refuses_invalid_existing_dependencies_without_force(self) -> None:
        (self.root / "third_party" / "cef").mkdir(parents=True)
        with self.assertRaisesRegex(InstallError, "run install --force"):
            load_cli()._install(argparse.Namespace(root=str(self.root), target="macosx64", dry_run=False, force=False))

    def test_cli_installs_absent_dependencies_and_forces_reinstall(self) -> None:
        cli = load_cli()
        args = argparse.Namespace(root=str(self.root), target="macosx64", dry_run=False, force=False)
        with patch.object(cli, "install_cef") as cef, patch.object(cli, "install_geist") as fonts, patch.object(cli, "verify"):
            self.assertEqual(cli._install(args), 0)
            cef.assert_called_once()
            fonts.assert_called_once()
        args.force = True
        with patch.object(cli, "install_cef") as cef, patch.object(cli, "install_geist") as fonts, patch.object(cli, "verify"):
            self.assertEqual(cli._install(args), 0)
            cef.assert_called_once()
            fonts.assert_called_once()

    def test_dry_run_has_no_writes_and_cache_key_is_deterministic(self) -> None:
        command = ["python3", str(REPOSITORY / "scripts" / "deps.py"), "--root", str(self.root), "--target", "macosx64"]
        dry_run = subprocess.run(command + ["install", "--dry-run"], check=True, text=True, capture_output=True)
        self.assertIn("CEF URL:", dry_run.stdout)
        self.assertFalse((self.root / "third_party").exists())
        first = subprocess.run(command + ["print-cache-key"], check=True, text=True, capture_output=True).stdout
        second = subprocess.run(command + ["print-cache-key"], check=True, text=True, capture_output=True).stdout
        self.assertEqual(first, second)


if __name__ == "__main__":
    unittest.main()
