#!/usr/bin/env python3
"""Island's version tooling. Stdlib only.

The `VERSION` file at the repository root is the single source of truth
(SemVer: MAJOR.MINOR.PATCH with an optional -pre-release suffix). CMake reads
it for the binaries; this script keeps everything else in step:

    python3 scripts/version.py show
    python3 scripts/version.py bump minor          # or major / patch
    python3 scripts/version.py bump minor --pre beta   # 0.5.0-beta.1
    python3 scripts/version.py bump pre            # 0.5.0-beta.2
    python3 scripts/version.py bump release        # 0.5.0
    python3 scripts/version.py sync                # rewrite derived files
    python3 scripts/version.py check [--tag v0.5.0]

`bump` moves the CHANGELOG's Unreleased notes under the new version (it refuses
an empty Unreleased section unless --allow-empty), then runs `sync`. `sync`
rewrites the derived files: the site's version markers and JSON-LD, the
rendered site/changelog.html, and the Windows manifest. `check` fails when any
of them is stale, when the changelog has no entry for a release version, or
when --tag disagrees with VERSION; CI and the site deploy run it.
"""

from __future__ import annotations

import argparse
import datetime
import html
import re
import sys
from dataclasses import dataclass
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[1]
REPO_URL = "https://github.com/impelixx/island"

SEMVER = re.compile(
    r"^(0|[1-9]\d*)\.(0|[1-9]\d*)\.(0|[1-9]\d*)(?:-([0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*))?$"
)
SECTION = re.compile(r"^## \[([^\]]+)\](?:\s*-\s*(\S+))?\s*$")
LINK_REF = re.compile(r"^\[([^\]]+)\]:\s*(\S+)\s*$")
CHANGELOG_START = "<!-- changelog:start -->"
CHANGELOG_END = "<!-- changelog:end -->"


class VersionError(Exception):
    pass


@dataclass(frozen=True)
class Version:
    major: int
    minor: int
    patch: int
    pre: str = ""

    @classmethod
    def parse(cls, text: str) -> Version:
        match = SEMVER.fullmatch(text.strip())
        if match is None:
            raise VersionError(f"not a SemVer version: {text.strip()!r}")
        return cls(int(match[1]), int(match[2]), int(match[3]), match[4] or "")

    @property
    def numeric(self) -> str:
        return f"{self.major}.{self.minor}.{self.patch}"

    def __str__(self) -> str:
        return self.numeric + (f"-{self.pre}" if self.pre else "")


def bumped(current: Version, part: str, pre_label: str | None = None) -> Version:
    """Returns the next version. `pre_label` starts a pre-release (label.1)."""
    if part == "major":
        base = Version(current.major + 1, 0, 0)
    elif part == "minor":
        base = Version(current.major, current.minor + 1, 0)
    elif part == "patch":
        base = Version(current.major, current.minor, current.patch + 1)
    elif part == "release":
        if not current.pre:
            raise VersionError(f"{current} is not a pre-release")
        return Version(current.major, current.minor, current.patch)
    elif part == "pre":
        match = re.fullmatch(r"(.*?)(\d+)", current.pre)
        if not current.pre or match is None:
            raise VersionError(f"{current} has no numbered pre-release to advance")
        return Version(current.major, current.minor, current.patch,
                       f"{match[1]}{int(match[2]) + 1}")
    else:
        raise VersionError(f"unknown bump part {part!r}")
    if pre_label:
        if not re.fullmatch(r"[0-9A-Za-z-]+", pre_label):
            raise VersionError(f"invalid pre-release label {pre_label!r}")
        return Version(base.major, base.minor, base.patch, f"{pre_label}.1")
    return base


# --- CHANGELOG ---------------------------------------------------------------


@dataclass
class Release:
    name: str  # "Unreleased" or a version string
    date: str
    body: list[str]


def parse_changelog(text: str) -> tuple[list[Release], dict[str, str]]:
    releases: list[Release] = []
    links: dict[str, str] = {}
    current: Release | None = None
    for line in text.splitlines():
        section = SECTION.match(line)
        link = LINK_REF.match(line)
        if section:
            current = Release(section[1], section[2] or "", [])
            releases.append(current)
        elif link:
            links[link[1]] = link[2]
        elif current is not None:
            current.body.append(line)
    for release in releases:
        while release.body and not release.body[-1].strip():
            release.body.pop()
        while release.body and not release.body[0].strip():
            release.body.pop(0)
    return releases, links


def rotate_changelog(text: str, version: Version, date: str, allow_empty: bool) -> str:
    """Moves the Unreleased notes under `version` and updates the link refs."""
    lines = text.splitlines()
    try:
        start = next(i for i, line in enumerate(lines) if line.strip() == "## [Unreleased]")
    except StopIteration:
        raise VersionError("CHANGELOG.md has no '## [Unreleased]' section") from None
    end = next((i for i in range(start + 1, len(lines)) if lines[i].startswith("## [")
                or LINK_REF.match(lines[i])), len(lines))
    notes = lines[start + 1:end]
    while notes and not notes[0].strip():
        notes.pop(0)
    while notes and not notes[-1].strip():
        notes.pop()
    if not notes and not allow_empty:
        raise VersionError("the Unreleased section is empty; add notes or pass --allow-empty")
    if any(SECTION.match(line) and SECTION.match(line)[1] == str(version) for line in lines):
        raise VersionError(f"CHANGELOG.md already has a section for {version}")
    block = ["## [Unreleased]", "", f"## [{version}] - {date}", ""]
    block += notes + ([""] if notes else [])
    rest = lines[end:]
    out = lines[:start] + block + rest
    tag = f"v{version}"
    refs = []
    replaced_unreleased = False
    for i, line in enumerate(out):
        link = LINK_REF.match(line)
        if link and link[1] == "Unreleased":
            out[i] = f"[Unreleased]: {REPO_URL}/compare/{tag}...HEAD"
            refs.append(i)
            replaced_unreleased = True
    new_ref = f"[{version}]: {REPO_URL}/releases/tag/{tag}"
    if replaced_unreleased:
        out.insert(refs[0] + 1, new_ref)
    else:
        out += ["", f"[Unreleased]: {REPO_URL}/compare/{tag}...HEAD", new_ref]
    return "\n".join(out).rstrip("\n") + "\n"


# --- Markdown subset -> HTML ------------------------------------------------


def _inline(text: str) -> str:
    out = []
    pos = 0
    pattern = re.compile(r"`([^`]+)`|\*\*([^*]+)\*\*|\[([^\]]+)\]\(([^)\s]+)\)")
    for match in pattern.finditer(text):
        out.append(html.escape(text[pos:match.start()], quote=False))
        if match[1] is not None:
            out.append(f"<code>{html.escape(match[1], quote=False)}</code>")
        elif match[2] is not None:
            out.append(f"<strong>{html.escape(match[2], quote=False)}</strong>")
        else:
            url = match[4]
            if not re.match(r"^(https?://|#|[A-Za-z0-9_./-]+$)", url):
                url = "#"
            out.append(f'<a href="{html.escape(url)}">{html.escape(match[3], quote=False)}</a>')
        pos = match.end()
    out.append(html.escape(text[pos:], quote=False))
    return "".join(out)


def _render_body(body: list[str], indent: str) -> list[str]:
    out: list[str] = []
    items: list[str] = []

    def flush() -> None:
        if items:
            out.append(f"{indent}<ul>")
            out.extend(f"{indent}  <li>{_inline(item)}</li>" for item in items)
            out.append(f"{indent}</ul>")
            items.clear()

    paragraph: list[str] = []
    for line in body + [""]:
        stripped = line.strip()
        if line.startswith("### "):
            flush()
            out.append(f"{indent}<h3>{_inline(line[4:].strip())}</h3>")
        elif line.startswith("- "):
            items.append(line[2:].strip())
        elif stripped and items and line.startswith("  "):
            items[-1] += " " + stripped
        elif stripped:
            flush()
            paragraph.append(stripped)
        else:
            flush()
            if paragraph:
                out.append(f"{indent}<p>{_inline(' '.join(paragraph))}</p>")
                paragraph = []
    return out


def render_changelog_html(text: str) -> str:
    releases, links = parse_changelog(text)
    indent = "          "
    out: list[str] = []
    for release in releases:
        if release.name == "Unreleased" and not release.body:
            continue
        anchor = "unreleased" if release.name == "Unreleased" else "v" + release.name
        title = html.escape(release.name)
        link = links.get(release.name, "")
        if release.name != "Unreleased" and "/releases/tag/" in link:
            title = f'<a href="{html.escape(link)}">{title}</a>'
        date = (f' <time datetime="{html.escape(release.date)}">{html.escape(release.date)}</time>'
                if release.date else "")
        out.append(f'{indent}<section class="release" id="{html.escape(anchor)}">')
        out.append(f'{indent}  <h2>{title}{date}</h2>')
        out.extend(_render_body(release.body, indent + "  "))
        out.append(f"{indent}</section>")
    return "\n".join(out)


# --- Derived files ------------------------------------------------------------


def _replace_between(text: str, start: str, end: str, content: str, path: Path) -> str:
    a = text.find(start)
    b = text.find(end)
    if a < 0 or b < a:
        raise VersionError(f"{path} lacks the {start} ... {end} markers")
    return text[:a + len(start)] + "\n" + content + "\n" + text[b:]


def derived_files(root: Path, version: Version) -> dict[Path, str]:
    """Returns {path: expected content} for every file `sync` maintains."""
    expected: dict[Path, str] = {}
    site = root / "site"
    marker = re.compile(r"(<[^>]*\bdata-island-version\b[^>]*>)[^<]*(<)")
    json_ld = re.compile(r'("softwareVersion":\s*")[^"]*(")')
    for page in sorted(site.glob("*.html")):
        text = page.read_text(encoding="utf-8")
        new = marker.sub(lambda m: f"{m[1]}{version}{m[2]}", text)
        new = json_ld.sub(lambda m: f"{m[1]}{version}{m[2]}", new)
        if page.name == "changelog.html":
            changelog = (root / "CHANGELOG.md").read_text(encoding="utf-8")
            new = _replace_between(new, CHANGELOG_START, CHANGELOG_END,
                                   render_changelog_html(changelog), page)
        if new != text:
            expected[page] = new
    manifest = root / "src/main/windows/island_browser.exe.manifest"
    if manifest.exists():
        text = manifest.read_text(encoding="utf-8")
        new = re.sub(r'(<assemblyIdentity\b[^>]*\bversion=")[^"]*(")',
                     lambda m: f"{m[1]}{version.numeric}.0{m[2]}", text, count=1)
        if new != text:
            expected[manifest] = new
    return expected


def read_version(root: Path) -> Version:
    path = root / "VERSION"
    if not path.exists():
        raise VersionError(f"{path} is missing")
    return Version.parse(path.read_text(encoding="utf-8"))


def check(root: Path, tag: str | None) -> list[str]:
    problems: list[str] = []
    try:
        version = read_version(root)
    except VersionError as error:
        return [str(error)]
    if tag is not None:
        name = tag.removeprefix("refs/tags/")
        if name != f"v{version}":
            problems.append(f"tag {name} does not match VERSION {version} (expected v{version})")
    releases, _ = parse_changelog((root / "CHANGELOG.md").read_text(encoding="utf-8"))
    names = [release.name for release in releases]
    if "Unreleased" not in names:
        problems.append("CHANGELOG.md has no '## [Unreleased]' section")
    if not version.pre and str(version) not in names:
        problems.append(f"CHANGELOG.md has no '## [{version}]' section")
    for path in derived_files(root, version):
        problems.append(f"{path.relative_to(root)} is out of date; run "
                        "python3 scripts/version.py sync")
    return problems


def sync(root: Path) -> list[Path]:
    version = read_version(root)
    changed = derived_files(root, version)
    for path, content in changed.items():
        path.write_text(content, encoding="utf-8")
    return list(changed)


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--root", type=Path, default=REPOSITORY, help=argparse.SUPPRESS)
    commands = parser.add_subparsers(dest="command", required=True)
    commands.add_parser("show", help="print the current version")
    bump = commands.add_parser("bump", help="bump VERSION, rotate CHANGELOG, sync")
    bump.add_argument("part", choices=["major", "minor", "patch", "pre", "release"])
    bump.add_argument("--pre", metavar="LABEL", help="start a pre-release, e.g. beta")
    bump.add_argument("--date", help="release date (default: today, UTC)")
    bump.add_argument("--allow-empty", action="store_true",
                      help="allow a release with no Unreleased notes")
    commands.add_parser("sync", help="rewrite files derived from VERSION and CHANGELOG")
    check_parser = commands.add_parser("check", help="fail if anything is out of step")
    check_parser.add_argument("--tag", help="a git tag (vX.Y.Z) that must match VERSION")
    args = parser.parse_args(argv)
    root: Path = args.root
    try:
        if args.command == "show":
            print(read_version(root))
        elif args.command == "bump":
            current = read_version(root)
            new = bumped(current, args.part, args.pre)
            date = args.date or datetime.datetime.now(datetime.timezone.utc).date().isoformat()
            changelog = root / "CHANGELOG.md"
            text = changelog.read_text(encoding="utf-8")
            if not new.pre:
                text = rotate_changelog(text, new, date, args.allow_empty)
            (root / "VERSION").write_text(f"{new}\n", encoding="utf-8")
            changelog.write_text(text, encoding="utf-8")
            sync(root)
            print(f"{current} -> {new}")
        elif args.command == "sync":
            for path in sync(root):
                print(f"updated {path.relative_to(root)}")
        elif args.command == "check":
            problems = check(root, args.tag)
            for problem in problems:
                print(f"version check: {problem}", file=sys.stderr)
            if problems:
                return 1
            print(f"version {read_version(root)} is consistent")
    except VersionError as error:
        print(f"error: {error}", file=sys.stderr)
        return 2
    return 0


if __name__ == "__main__":
    sys.exit(main())
