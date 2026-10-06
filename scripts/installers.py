#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Builds the per-target installer that sits beside each release archive. Stdlib only.

    python3 scripts/installers.py macos-sign build/src/main/island_browser.app
    python3 scripts/installers.py build --target macosarm64 --version 0.5.0 --output-dir dist \
        --app build/src/main/island_browser.app
    python3 scripts/installers.py build --target linux64 --version 0.5.0 --output-dir dist
    python3 scripts/installers.py build --target windows64 --version 0.5.0 --output-dir dist

`build` runs after `scripts/package.py` and appends the installer's line to the
`SHA256SUMS.txt` that it wrote in `--output-dir`:

- macOS: `island_browser-<version>-<target>.dmg`, holding `Island.app` and an `Applications`
  link to drag it onto (hdiutil, ditto). `macos-sign` ad-hoc signs the bundle inside-out and runs
  before `scripts/package.py`, so the .zip the in-browser updater installs carries the same
  signature. An ad-hoc signature is not a Developer ID: Gatekeeper still asks once (System
  Settings > Privacy & Security > Open Anyway), but no longer calls the download "damaged", which
  is what an unsealed bundle with the quarantine flag gets.
- Linux: `island_browser-<version>-<target>.deb` (dpkg-deb), built from the release .tar.gz:
  the app in `/opt/island-browser`, an `island-browser` command, a desktop entry, and an icon.
- Windows: `island_browser-<version>-<target>-setup.exe` (Inno Setup 6.3+, `ISCC`), built from
  the release .zip with `src/main/windows/island_browser.iss`: a per-user install with a Start
  menu entry and an uninstaller.
"""

from __future__ import annotations

import argparse
import hashlib
import os
import shutil
import subprocess
import sys
import tarfile
import tempfile
import time
import zipfile
from pathlib import Path
from typing import Final, assert_never

from package import SEMVER, Target, archive_name, installer_name

REPOSITORY: Final = Path(__file__).resolve().parents[1]
SUMS_NAME: Final = "SHA256SUMS.txt"
MAC_APP_NAME: Final = "Island.app"
MAC_VOLUME_NAME: Final = "Island"
DEB_PACKAGE: Final = "island-browser"
DEB_PREFIX: Final = Path("opt/island-browser")
DEB_ARCHITECTURES: Final = {Target.LINUX_X64: "amd64", Target.LINUX_ARM64: "arm64"}
# The shared libraries CEF's Linux runtime loads, as package alternatives that cover Debian 12,
# Ubuntu 22.04, and the 64-bit time_t ("t64") renames of Ubuntu 24.04 / Debian 13.
DEB_DEPENDS: Final = (
    "libasound2t64 | libasound2", "libatk-bridge2.0-0t64 | libatk-bridge2.0-0",
    "libatk1.0-0t64 | libatk1.0-0", "libatspi2.0-0t64 | libatspi2.0-0", "libcairo2",
    "libcups2t64 | libcups2", "libdbus-1-3", "libdrm2", "libexpat1", "libfontconfig1",
    "libgbm1", "libglib2.0-0t64 | libglib2.0-0", "libgtk-3-0t64 | libgtk-3-0", "libnspr4",
    "libnss3", "libpango-1.0-0", "libx11-6", "libxcb1", "libxcomposite1", "libxdamage1",
    "libxext6", "libxfixes3", "libxkbcommon0", "libxrandr2",
)
WINDOWS_ARCHITECTURES: Final = {Target.WINDOWS_X64: "x64compatible", Target.WINDOWS_ARM64: "arm64"}


class InstallerError(Exception):
    pass


def _run(*command: str) -> None:
    print("+", " ".join(command), flush=True)
    subprocess.run(command, check=True)


def _digest(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def record_checksum(output_dir: Path, path: Path) -> None:
    """Adds (or replaces) |path|'s line in output_dir/SHA256SUMS.txt."""
    sums = output_dir / SUMS_NAME
    lines = [line for line in sums.read_text(encoding="utf-8").splitlines()
             if line.strip() and line.split(maxsplit=1)[1].lstrip("*") != path.name]
    lines.append(f"{_digest(path)}  {path.name}")
    sums.write_bytes("".join(f"{line}\n" for line in lines).encode("utf-8"))


# --- macOS -------------------------------------------------------------------------------------

def macos_nested_code(app: Path) -> list[Path]:
    """Every separately signed item inside |app|, innermost first, then |app| itself."""
    frameworks = app / "Contents" / "Frameworks"
    items: list[Path] = []
    for framework in sorted(frameworks.glob("*.framework")):
        items.extend(sorted((framework / "Libraries").glob("*.dylib")))
        items.append(framework)
    items.extend(sorted(frameworks.glob("*.app")))
    items.append(app)
    return items


def macos_sign(app: Path) -> None:
    if app.suffix != ".app" or not app.is_dir():
        raise InstallerError(f"not an app bundle: {app}")
    for item in macos_nested_code(app):
        _run("codesign", "--force", "--sign", "-", "--timestamp=none", str(item))
    _run("codesign", "--verify", "--deep", "--strict", "--verbose=2", str(app))


def _build_dmg(app: Path, destination: Path) -> None:
    if app.suffix != ".app" or not app.is_dir():
        raise InstallerError(f"--app must be the built .app bundle, got {app}")
    with tempfile.TemporaryDirectory() as temporary:
        stage = Path(temporary) / MAC_VOLUME_NAME
        stage.mkdir()
        # ditto keeps the framework symlinks and extended attributes the signature covers.
        _run("ditto", str(app), str(stage / MAC_APP_NAME))
        (stage / "Applications").symlink_to("/Applications")
        _run("codesign", "--verify", "--deep", "--strict", str(stage / MAC_APP_NAME))
        create = ("hdiutil", "create", "-volname", MAC_VOLUME_NAME, "-srcfolder", str(stage),
                  "-fs", "HFS+", "-format", "UDZO", "-imagekey", "zlib-level=9", "-ov",
                  str(destination))
        # hdiutil intermittently fails with "Resource busy" on hosted runners; retry that.
        for attempt in range(3):
            try:
                _run(*create)
                break
            except subprocess.CalledProcessError:
                if attempt == 2:
                    raise
                time.sleep(10)
    _run("hdiutil", "verify", str(destination))


# --- Linux -------------------------------------------------------------------------------------

def deb_version(version: str) -> str:
    """SemVer to a Debian version that sorts the same: a pre-release sorts before its release."""
    return version.replace("-", "~", 1)


def deb_control(version: str, target: Target, installed_kib: int) -> str:
    return "".join(f"{line}\n" for line in (
        f"Package: {DEB_PACKAGE}",
        f"Version: {deb_version(version)}",
        f"Architecture: {DEB_ARCHITECTURES[target]}",
        "Maintainer: Island <island-browser@users.noreply.github.com>",
        f"Installed-Size: {installed_kib}",
        f"Depends: {', '.join(DEB_DEPENDS)}",
        "Section: web",
        "Priority: optional",
        "Homepage: https://island-browser.github.io/site/",
        "Description: Island web browser",
        " A desktop browser built on the Chromium Embedded Framework, with spaces,",
        " split view, a command palette, and a built-in agent.",
    ))


def _build_deb(archive: Path, version: str, target: Target, destination: Path) -> None:
    linux = REPOSITORY / "src" / "main" / "linux"
    with tempfile.TemporaryDirectory() as temporary:
        root = Path(temporary) / "root"
        app = root / DEB_PREFIX
        app.mkdir(parents=True)
        with tarfile.open(archive, "r:gz") as tar:
            tar.extractall(app, filter="tar")
        if not (app / "island_browser").is_file():
            raise InstallerError(f"{archive.name} has no island_browser at its root")
        sandbox = app / "chrome-sandbox"
        if sandbox.is_file():
            sandbox.chmod(0o4755)  # Chromium's setuid sandbox helper; owned by root via dpkg-deb
        bin_dir = root / "usr" / "bin"
        bin_dir.mkdir(parents=True)
        (bin_dir / DEB_PACKAGE).symlink_to(Path("/") / DEB_PREFIX / "island_browser")
        applications = root / "usr" / "share" / "applications"
        applications.mkdir(parents=True)
        shutil.copyfile(linux / "island-browser.desktop", applications / "island-browser.desktop")
        icons = root / "usr" / "share" / "icons" / "hicolor" / "scalable" / "apps"
        icons.mkdir(parents=True)
        shutil.copyfile(linux / "island-browser.svg", icons / "island-browser.svg")
        size = sum(path.lstat().st_size for path in root.rglob("*") if not path.is_dir())
        control = root / "DEBIAN"
        control.mkdir()
        (control / "control").write_text(deb_control(version, target, (size + 1023) // 1024),
                                         encoding="utf-8")
        for directory in (root, *(path for path in root.rglob("*") if path.is_dir() and not path.is_symlink())):
            directory.chmod(0o755)
        # xz rather than dpkg's newer zstd default, so Debian 11/12's dpkg can read it.
        _run("dpkg-deb", "--root-owner-group", "-Zxz", "-z6", "--build", str(root), str(destination))


# --- Windows -----------------------------------------------------------------------------------

def find_iscc() -> str:
    found = shutil.which("ISCC") or shutil.which("iscc")
    if found:
        return found
    local = os.environ.get("LOCALAPPDATA")
    for base in (os.environ.get("ProgramFiles(x86)"), os.environ.get("ProgramFiles"),
                 str(Path(local) / "Programs") if local else None):
        if base:
            for candidate in sorted(Path(base).glob("Inno Setup */ISCC.exe"), reverse=True):
                return str(candidate)
    raise InstallerError("Inno Setup (ISCC.exe) is not installed")


def iscc_command(iscc: str, version: str, target: Target, source: Path, destination: Path) -> list[str]:
    return [iscc, "/Q",
            f"/DAppVersion={version}",
            f"/DSourceDir={source}",
            f"/DOutputDir={destination.parent}",
            f"/DOutputBaseFilename={destination.name.removesuffix('.exe')}",
            f"/DArch={WINDOWS_ARCHITECTURES[target]}",
            str(REPOSITORY / "src" / "main" / "windows" / "island_browser.iss")]


def _build_windows(archive: Path, version: str, target: Target, destination: Path) -> None:
    iscc = find_iscc()
    with tempfile.TemporaryDirectory() as temporary:
        source = Path(temporary) / "app"
        with zipfile.ZipFile(archive) as zipped:
            zipped.extractall(source)
        if not (source / "island_browser.exe").is_file():
            raise InstallerError(f"{archive.name} has no island_browser.exe at its root")
        _run(*iscc_command(iscc, version, target, source, destination))
    if not destination.is_file():
        raise InstallerError(f"ISCC did not write {destination}")


# --- entry points ------------------------------------------------------------------------------

def build(target: Target, version: str, output_dir: Path, app: Path | None = None) -> Path:
    if SEMVER.fullmatch(version) is None:
        raise InstallerError(f"version must be SemVer-like, got {version!r}")
    archive = output_dir / archive_name(version, target)
    if not archive.is_file() or not (output_dir / SUMS_NAME).is_file():
        raise InstallerError(f"run scripts/package.py first: {archive} does not exist")
    destination = output_dir / installer_name(version, target)
    destination.unlink(missing_ok=True)
    match target:
        case Target.MACOS_X64 | Target.MACOS_ARM64:
            if app is None:
                raise InstallerError("macOS disk images need --app (the signed .app bundle)")
            _build_dmg(app.resolve(), destination)
        case Target.LINUX_X64 | Target.LINUX_ARM64:
            _build_deb(archive, version, target, destination)
        case Target.WINDOWS_X64 | Target.WINDOWS_ARM64:
            _build_windows(archive, version, target, destination)
        case _:
            assert_never(target)
    record_checksum(output_dir, destination)
    return destination


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    commands = parser.add_subparsers(dest="command", required=True)
    sign_parser = commands.add_parser("macos-sign", help="ad-hoc sign a .app bundle inside-out")
    sign_parser.add_argument("app", type=Path)
    build_parser = commands.add_parser("build", help="build the installer and record its checksum")
    build_parser.add_argument("--target", required=True, choices=tuple(t.value for t in Target))
    build_parser.add_argument("--version", required=True)
    build_parser.add_argument("--output-dir", type=Path, required=True)
    build_parser.add_argument("--app", type=Path, help="the .app bundle (macOS targets)")
    args = parser.parse_args(argv)
    try:
        if args.command == "macos-sign":
            macos_sign(args.app)
        else:
            print(build(Target(args.target), args.version, args.output_dir.resolve(), args.app))
    except (InstallerError, OSError, subprocess.CalledProcessError, tarfile.TarError,
            zipfile.BadZipFile) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
