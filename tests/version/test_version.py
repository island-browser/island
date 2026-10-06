"""Tests for scripts/version.py: SemVer parsing and bumping, CHANGELOG rotation,
the Markdown subset rendered into site/changelog.html, and the repository's
own consistency (VERSION, CHANGELOG.md, and the site agree)."""

from __future__ import annotations

import importlib.util
import io
import sys
import tempfile
import unittest
from contextlib import redirect_stderr, redirect_stdout
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[2]
_SPEC = importlib.util.spec_from_file_location("island_version_tool",
                                               REPOSITORY / "scripts" / "version.py")
assert _SPEC is not None and _SPEC.loader is not None
version = importlib.util.module_from_spec(_SPEC)
sys.modules[_SPEC.name] = version  # dataclasses resolve their module by name
_SPEC.loader.exec_module(version)

CHANGELOG = """# Changelog

Intro.

## [Unreleased]

### Added

- A new thing with `code`.

## [0.1.0] - 2026-01-01

### Added

- First.

[Unreleased]: https://github.com/island-browser/island/compare/v0.1.0...HEAD
[0.1.0]: https://github.com/island-browser/island/releases/tag/v0.1.0
"""

SITE_PAGE = """<html><head><script type="application/ld+json">{"softwareVersion": "0.0.1"}</script></head>
<body><span data-island-version>0.0.1</span>
<!-- changelog:start -->
stale
<!-- changelog:end -->
</body></html>
"""


def run(root: Path, *argv: str) -> tuple[int, str, str]:
    out, err = io.StringIO(), io.StringIO()
    with redirect_stdout(out), redirect_stderr(err):
        code = version.main(["--root", str(root), *argv])
    return code, out.getvalue(), err.getvalue()


class VersionParsingTests(unittest.TestCase):
    def test_parses_release_and_prerelease(self) -> None:
        self.assertEqual(str(version.Version.parse("1.2.3")), "1.2.3")
        parsed = version.Version.parse("0.5.0-beta.2\n")
        self.assertEqual((parsed.major, parsed.minor, parsed.patch, parsed.pre), (0, 5, 0, "beta.2"))
        self.assertEqual(parsed.numeric, "0.5.0")

    def test_rejects_non_semver(self) -> None:
        for text in ("1.2", "01.2.3", "1.2.3-", "v1.2.3", "1.2.3+build", ""):
            with self.subTest(text=text), self.assertRaises(version.VersionError):
                version.Version.parse(text)

    def test_bumps(self) -> None:
        current = version.Version.parse("0.4.2")
        self.assertEqual(str(version.bumped(current, "patch")), "0.4.3")
        self.assertEqual(str(version.bumped(current, "minor")), "0.5.0")
        self.assertEqual(str(version.bumped(current, "major")), "1.0.0")
        beta = version.bumped(current, "minor", "beta")
        self.assertEqual(str(beta), "0.5.0-beta.1")
        self.assertEqual(str(version.bumped(beta, "pre")), "0.5.0-beta.2")
        self.assertEqual(str(version.bumped(beta, "release")), "0.5.0")
        with self.assertRaises(version.VersionError):
            version.bumped(current, "release")
        with self.assertRaises(version.VersionError):
            version.bumped(current, "pre")
        with self.assertRaises(version.VersionError):
            version.bumped(current, "minor", "bad label")


class ChangelogTests(unittest.TestCase):
    def test_rotation_moves_unreleased_notes_and_links(self) -> None:
        rotated = version.rotate_changelog(CHANGELOG, version.Version.parse("0.2.0"),
                                           "2026-02-02", allow_empty=False)
        releases, links = version.parse_changelog(rotated)
        self.assertEqual([r.name for r in releases], ["Unreleased", "0.2.0", "0.1.0"])
        self.assertEqual(releases[0].body, [])
        self.assertEqual(releases[1].date, "2026-02-02")
        self.assertIn("- A new thing with `code`.", releases[1].body)
        self.assertEqual(links["Unreleased"],
                         "https://github.com/island-browser/island/compare/v0.2.0...HEAD")
        self.assertEqual(links["0.2.0"], "https://github.com/island-browser/island/releases/tag/v0.2.0")
        self.assertEqual(links["0.1.0"], "https://github.com/island-browser/island/releases/tag/v0.1.0")

    def test_rotation_refuses_empty_or_duplicate_releases(self) -> None:
        empty = version.rotate_changelog(CHANGELOG, version.Version.parse("0.2.0"), "d", False)
        with self.assertRaises(version.VersionError):
            version.rotate_changelog(empty, version.Version.parse("0.3.0"), "d", False)
        self.assertIn("## [0.3.0] - d",
                      version.rotate_changelog(empty, version.Version.parse("0.3.0"), "d", True))
        with self.assertRaises(version.VersionError):
            version.rotate_changelog(CHANGELOG, version.Version.parse("0.1.0"), "d", False)

    def test_render_escapes_html_and_supports_the_subset(self) -> None:
        text = ("## [1.0.0] - 2026-03-03\n\n### Fixed\n\n- A <script> **bold** `a<b>` and\n"
                "  [docs](https://example.test/x) [bad](javascript:alert(1))\n\nA paragraph.\n\n"
                "[1.0.0]: https://github.com/island-browser/island/releases/tag/v1.0.0\n")
        rendered = version.render_changelog_html(text)
        self.assertIn('id="v1.0.0"', rendered)
        self.assertIn('<a href="https://github.com/island-browser/island/releases/tag/v1.0.0">1.0.0</a>',
                      rendered)
        self.assertIn('<time datetime="2026-03-03">', rendered)
        self.assertIn("<h3>Fixed</h3>", rendered)
        self.assertIn("A &lt;script&gt; <strong>bold</strong> <code>a&lt;b&gt;</code> and "
                      '<a href="https://example.test/x">docs</a> <a href="#">bad</a>', rendered)
        self.assertIn("<p>A paragraph.</p>", rendered)
        self.assertNotIn("<script>", rendered)

    def test_empty_unreleased_is_not_rendered(self) -> None:
        rendered = version.render_changelog_html("## [Unreleased]\n\n## [0.1.0] - d\n\n- x\n")
        self.assertNotIn("unreleased", rendered)
        self.assertIn('id="v0.1.0"', rendered)


class CommandTests(unittest.TestCase):
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "VERSION").write_text("0.1.0\n", encoding="utf-8")
        (self.root / "CHANGELOG.md").write_text(CHANGELOG, encoding="utf-8")
        (self.root / "site").mkdir()
        (self.root / "site" / "changelog.html").write_text(SITE_PAGE, encoding="utf-8")
        manifest = self.root / "src/main/windows/island_browser.exe.manifest"
        manifest.parent.mkdir(parents=True)
        manifest.write_text('<assemblyIdentity name="I" version="0.0.1.0" type="win32" />\n',
                            encoding="utf-8")

    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_check_reports_stale_files_until_sync(self) -> None:
        code, _, err = run(self.root, "check")
        self.assertEqual(code, 1)
        self.assertIn("site/changelog.html is out of date", err)
        self.assertIn("island_browser.exe.manifest is out of date", err)
        self.assertEqual(run(self.root, "sync")[0], 0)
        self.assertEqual(run(self.root, "check")[0], 0)
        page = (self.root / "site" / "changelog.html").read_text(encoding="utf-8")
        self.assertIn('<span data-island-version>0.1.0</span>', page)
        self.assertIn('"softwareVersion": "0.1.0"', page)
        self.assertNotIn("stale", page)
        self.assertIn('version="0.1.0.0"',
                      (self.root / "src/main/windows/island_browser.exe.manifest").read_text())

    def test_check_matches_tags(self) -> None:
        run(self.root, "sync")
        self.assertEqual(run(self.root, "check", "--tag", "v0.1.0")[0], 0)
        self.assertEqual(run(self.root, "check", "--tag", "refs/tags/v0.1.0")[0], 0)
        code, _, err = run(self.root, "check", "--tag", "v0.2.0")
        self.assertEqual(code, 1)
        self.assertIn("does not match VERSION", err)

    def test_bump_rotates_writes_and_syncs(self) -> None:
        code, out, _ = run(self.root, "bump", "minor", "--date", "2026-04-04")
        self.assertEqual(code, 0)
        self.assertIn("0.1.0 -> 0.2.0", out)
        self.assertEqual((self.root / "VERSION").read_text(encoding="utf-8"), "0.2.0\n")
        self.assertIn("## [0.2.0] - 2026-04-04",
                      (self.root / "CHANGELOG.md").read_text(encoding="utf-8"))
        self.assertIn('id="v0.2.0"',
                      (self.root / "site" / "changelog.html").read_text(encoding="utf-8"))
        self.assertEqual(run(self.root, "check")[0], 0)
        # Nothing new to release: refused without touching VERSION.
        self.assertEqual(run(self.root, "bump", "patch")[0], 2)
        self.assertEqual((self.root / "VERSION").read_text(encoding="utf-8"), "0.2.0\n")

    def test_prerelease_bump_keeps_unreleased_notes(self) -> None:
        self.assertEqual(run(self.root, "bump", "minor", "--pre", "beta")[0], 0)
        self.assertEqual((self.root / "VERSION").read_text(encoding="utf-8"), "0.2.0-beta.1\n")
        self.assertIn("- A new thing", version.parse_changelog(
            (self.root / "CHANGELOG.md").read_text(encoding="utf-8"))[0][0].body[-1])
        self.assertEqual(run(self.root, "check")[0], 0)

    def test_notes_prints_one_section(self) -> None:
        code, out, _ = run(self.root, "notes")
        self.assertEqual(code, 0)
        self.assertEqual(out, "### Added\n\n- First.\n")
        self.assertEqual(run(self.root, "notes", "v0.1.0")[1], out)
        self.assertEqual(run(self.root, "notes", "0.1.0")[1], out)
        code, out, _ = run(self.root, "notes", "Unreleased")
        self.assertEqual((code, out), (0, "### Added\n\n- A new thing with `code`.\n"))
        self.assertEqual(run(self.root, "notes", "unreleased")[1], out)
        self.assertNotIn("[Unreleased]:", out)

    def test_notes_rejects_missing_or_invalid_versions(self) -> None:
        code, out, err = run(self.root, "notes", "0.9.0")
        self.assertEqual((code, out), (2, ""))
        self.assertIn("no '## [0.9.0]' section", err)
        self.assertEqual(run(self.root, "notes", "latest")[0], 2)

    def test_notes_of_an_empty_section_is_empty(self) -> None:
        (self.root / "CHANGELOG.md").write_text(CHANGELOG.replace(
            "### Added\n\n- A new thing with `code`.\n\n", ""), encoding="utf-8")
        self.assertEqual(run(self.root, "notes", "Unreleased"), (0, "", ""))

    def test_invalid_version_file(self) -> None:
        (self.root / "VERSION").write_text("one\n", encoding="utf-8")
        code, _, err = run(self.root, "check")
        self.assertEqual(code, 1)
        self.assertIn("not a SemVer version", err)


class RepositoryTests(unittest.TestCase):
    def test_repository_is_consistent(self) -> None:
        self.assertEqual(version.check(REPOSITORY, None), [])

    def test_cmake_reads_the_same_version_file(self) -> None:
        module = (REPOSITORY / "cmake" / "island_version.cmake").read_text(encoding="utf-8")
        self.assertIn('"${repo_root}/VERSION"', module)
        root = (REPOSITORY / "CMakeLists.txt").read_text(encoding="utf-8")
        self.assertIn("project(island_browser VERSION ${ISLAND_VERSION_NUMERIC}", root)
        for plist in ("Info.plist.in", "Helper-Info.plist.in"):
            text = (REPOSITORY / "src" / "main" / plist).read_text(encoding="utf-8")
            self.assertEqual(text.count("@ISLAND_VERSION_NUMERIC@"), 2, plist)


if __name__ == "__main__":
    unittest.main()
