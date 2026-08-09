#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///

# ─── How to run ───
# 1. Install uv (if not installed):
#      curl -LsSf https://astral.sh/uv/install.sh | sh
# 2. Run directly (no venv, no pip install needed):
#      uv run scripts/package.py --target macosarm64 --build-dir build --version 0.1.0 --output-dir dist
# 3. Or run with Python 3.11+:
#      python3 scripts/package.py --target macosarm64 --build-dir build --version 0.1.0 --output-dir dist
# ──────────────────
from __future__ import annotations

import argparse
import gzip
import hashlib
import io
import json
import os
import re
import sys
import tarfile
import tempfile
import zipfile
from dataclasses import dataclass
from enum import StrEnum
from pathlib import Path
from typing import Final, TypedDict, assert_never, final, override

from package_resources import ResourceValidationError, validate_runtime_resources


SEMVER: Final = re.compile(r"^(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)\.(0|[1-9][0-9]*)(?:-[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?(?:\+[0-9A-Za-z-]+(?:\.[0-9A-Za-z-]+)*)?$")
EPOCH: Final = (1980, 1, 1, 0, 0, 0)
CORE_RESOURCES: Final = (Path("icudtl.dat"), Path("resources.pak"), Path("chrome_100_percent.pak"), Path("chrome_200_percent.pak"), Path("locales/en-US.pak"))
MAC_RESOURCES: Final = (Path("icudtl.dat"), Path("resources.pak"), Path("chrome_100_percent.pak"), Path("chrome_200_percent.pak"), Path("en.lproj/locale.pak"))
MAC_HELPER_SUFFIXES: Final = ("", " (Alerts)", " (GPU)", " (Plugin)", " (Renderer)")


class PackageError(Exception):
    detail: str

    def __init__(self, detail: str) -> None:
        super().__init__(detail)
        self.detail = detail

    @override
    def __str__(self) -> str:
        return self.detail


class Target(StrEnum):
    MACOS_X64 = "macosx64"
    MACOS_ARM64 = "macosarm64"
    WINDOWS_X64 = "windows64"
    WINDOWS_ARM64 = "windowsarm64"
    LINUX_X64 = "linux64"
    LINUX_ARM64 = "linuxarm64"


class Architecture(StrEnum):
    ARM64 = "arm64"
    FAT = "fat"
    UNKNOWN = "unknown"
    X64 = "x64"


class WindowsMode(StrEnum):
    NORMAL = "normal"
    SANDBOX = "sandbox"


class BuildMetadata(TypedDict):
    format: str
    notarized: bool
    publicReleaseEligible: bool
    signed: bool
    target: str
    version: str


@final
class RawArguments(argparse.Namespace):
    build_dir: str = ""
    output_dir: str = ""
    target: str = ""
    version: str = ""


@dataclass(frozen=True, slots=True)
class Arguments:
    build_dir: Path
    output_dir: Path
    target: Target
    version: str


@dataclass(frozen=True, slots=True)
class Layout:
    archive_suffix: str
    binaries: tuple[Path, ...]
    executable: Path
    runtime_root: Path
    required: tuple[Path, ...]


def parse_arguments() -> Arguments:
    parser = argparse.ArgumentParser(description="Package an unsigned internal Island build; only thin target-architecture binaries are accepted.")
    _ = parser.add_argument("--target", required=True, choices=tuple(target.value for target in Target))
    _ = parser.add_argument("--build-dir", required=True)
    _ = parser.add_argument("--version", required=True)
    _ = parser.add_argument("--output-dir", required=True)
    raw = RawArguments()
    _ = parser.parse_args(namespace=raw)
    version = raw.version
    if SEMVER.fullmatch(version) is None:
        raise PackageError(f"version must be SemVer-like, got {version!r}")
    return Arguments(Path(raw.build_dir).resolve(), Path(raw.output_dir).resolve(), Target(raw.target), version)


def layout(target: Target) -> Layout:
    match target:
        case Target.MACOS_X64 | Target.MACOS_ARM64:
            root = Path("src/main/island_browser.app")
            framework = root / "Contents/Frameworks/Chromium Embedded Framework.framework"
            helpers = tuple(root / f"Contents/Frameworks/island_browser Helper{suffix}.app/Contents/MacOS/island_browser Helper{suffix}" for suffix in MAC_HELPER_SUFFIXES)
            return Layout(".zip", (root / "Contents/MacOS/island_browser", framework / "Chromium Embedded Framework", *helpers), root / "Contents/MacOS/island_browser", root, (framework, framework / "Chromium Embedded Framework", *helpers, *(framework / "Resources" / item for item in MAC_RESOURCES)))
        case Target.WINDOWS_X64 | Target.WINDOWS_ARM64:
            root = Path("Release")
            binaries = (Path("island_browser.exe"), Path("libcef.dll"), Path("chrome_elf.dll"), Path("d3dcompiler_47.dll"), Path("dxcompiler.dll"), Path("dxil.dll"), Path("libEGL.dll"), Path("libGLESv2.dll"), Path("vk_swiftshader.dll"), Path("vulkan-1.dll"))
            return Layout(".zip", tuple(root / item for item in binaries), root / "island_browser.exe", root, tuple(root / item for item in (*binaries, Path("snapshot_blob.bin"), Path("v8_context_snapshot.bin"), Path("vk_swiftshader_icd.json"), *CORE_RESOURCES)))
        case Target.LINUX_X64 | Target.LINUX_ARM64:
            root = Path("Release")
            binaries = (Path("island_browser"), Path("chrome-sandbox"), Path("libcef.so"), Path("libEGL.so"), Path("libGLESv2.so"), Path("libvk_swiftshader.so"), Path("libvulkan.so.1"))
            return Layout(".tar.gz", tuple(root / item for item in binaries), root / "island_browser", root, tuple(root / item for item in (*binaries, Path("snapshot_blob.bin"), Path("v8_context_snapshot.bin"), *CORE_RESOURCES)))
    assert_never(target)


def validate(arguments: Arguments, selected: Layout) -> Path:
    if not arguments.build_dir.is_dir():
        raise PackageError(f"build directory does not exist: {arguments.build_dir}")
    root = arguments.build_dir / selected.runtime_root
    _validate_paths(root)
    required = (arguments.build_dir / selected.executable, *(arguments.build_dir / item for item in selected.required))
    for path in required:
        if not path.exists():
            raise PackageError(f"missing required runtime path: {path.relative_to(arguments.build_dir)}")
    try:
        _ = validate_runtime_resources(root / ("Contents/Resources/island" if arguments.target.value.startswith("macos") else "resources/island"))
    except ResourceValidationError as error:
        raise PackageError(str(error)) from error
    expected = Architecture.ARM64 if arguments.target.value.endswith("arm64") else Architecture.X64
    for binary in (arguments.build_dir / item for item in selected.binaries):
        detected = _architecture(binary)
        if detected is not expected:
            raise PackageError(f"binary architecture {detected.value} conflicts with target {arguments.target.value}: {binary.relative_to(root)}")
    match arguments.target:
        case Target.WINDOWS_X64 | Target.WINDOWS_ARM64:
            if _windows_mode(root) is WindowsMode.SANDBOX:
                client = root / "island_browser.dll"
                detected = _architecture(client)
                if detected is not expected:
                    raise PackageError(f"binary architecture {detected.value} conflicts with target {arguments.target.value}: {client.relative_to(root)}")
            return root
        case Target.MACOS_X64 | Target.MACOS_ARM64 | Target.LINUX_X64 | Target.LINUX_ARM64:
            return root
    assert_never(arguments.target)


def _validate_paths(root: Path) -> None:
    if root.is_symlink() or not root.is_dir():
        raise PackageError(f"missing runtime root: {root}")
    runtime_root = root.resolve()
    for path in (root, *sorted(root.rglob("*"))):
        if path.is_symlink():
            link = Path(os.readlink(path))
            target = (path.parent / link).resolve()
            if link.is_absolute() or not target.is_relative_to(runtime_root) or not target.exists():
                raise PackageError(f"symlink escapes runtime root: {path.relative_to(root)}")
        if not path.is_symlink() and not path.is_dir() and not path.is_file():
            raise PackageError(f"unsupported runtime entry: {path.relative_to(root)}")


def _windows_mode(root: Path) -> WindowsMode:
    executable = root / "island_browser.exe"
    client = root / "island_browser.dll"
    bootstrap = b"island_browser.dll" in executable.read_bytes().lower()
    if bootstrap and not client.is_file():
        raise PackageError("missing sandbox client DLL: island_browser.dll")
    if client.is_file() and not bootstrap:
        raise PackageError("bootstrap/client DLL mismatch: island_browser.dll requires island_browser.exe bootstrap")
    return WindowsMode.SANDBOX if bootstrap else WindowsMode.NORMAL


def _architecture(executable: Path) -> Architecture:
    data = executable.read_bytes()[:128]
    if data[:4] in (b"\xca\xfe\xba\xbe", b"\xbe\xba\xfe\xca", b"\xca\xfe\xba\xbf", b"\xbf\xba\xfe\xca"):
        return Architecture.FAT
    if data[:4] == b"\xcf\xfa\xed\xfe":
        return {b"\x07\x00\x00\x01": Architecture.X64, b"\x0c\x00\x00\x01": Architecture.ARM64}.get(data[4:8], Architecture.UNKNOWN)
    if data[:4] == b"\x7fELF":
        return {62: Architecture.X64, 183: Architecture.ARM64}.get(int.from_bytes(data[18:20], "little"), Architecture.UNKNOWN)
    if data[:2] == b"MZ" and len(data) >= 64:
        offset = int.from_bytes(data[60:64], "little")
        if data[offset:offset + 4] == b"PE\x00\x00" and len(data) >= offset + 6:
            return {0x8664: Architecture.X64, 0xAA64: Architecture.ARM64}.get(int.from_bytes(data[offset + 4:offset + 6], "little"), Architecture.UNKNOWN)
    return Architecture.UNKNOWN


def package(arguments: Arguments) -> Path:
    selected = layout(arguments.target)
    source = validate(arguments, selected)
    name = f"island_browser-{arguments.version}-{arguments.target.value}{selected.archive_suffix}"
    artifact = arguments.output_dir / name
    metadata: BuildMetadata = {"format": selected.archive_suffix.removeprefix("."), "notarized": False, "publicReleaseEligible": False, "signed": False, "target": arguments.target.value, "version": arguments.version}
    _ = arguments.output_dir.mkdir(parents=True, exist_ok=True)
    temporary = _temporary_path(arguments.output_dir, name)
    try:
        _write_archive(temporary, source, selected.archive_suffix, metadata)
        _ = temporary.replace(artifact)
    finally:
        _ = temporary.unlink(missing_ok=True)
    _write_atomically(arguments.output_dir / "SHA256SUMS.txt", f"{_digest(artifact)}  {artifact.name}\n".encode())
    return artifact


def _write_archive(destination: Path, source: Path, suffix: str, metadata: BuildMetadata) -> None:
    notices = _notices(source).encode()
    encoded_metadata = (json.dumps(metadata, sort_keys=True, separators=(",", ":")) + "\n").encode()
    if suffix == ".zip":
        with zipfile.ZipFile(destination, "w", compression=zipfile.ZIP_DEFLATED, compresslevel=9, strict_timestamps=True) as archive:
            for path in sorted(source.rglob("*")):
                if path.is_symlink() or not path.is_dir():
                    _write_zip_bytes(archive, _archive_name(source, path), os.readlink(path).encode() if path.is_symlink() else path.read_bytes(), path)
            _write_zip_bytes(archive, "build-metadata.json", encoded_metadata)
            _write_zip_bytes(archive, "THIRD_PARTY_NOTICES.txt", notices)
        return
    with destination.open("wb") as raw, gzip.GzipFile(fileobj=raw, mode="wb", mtime=0, filename="") as compressed, tarfile.open(fileobj=compressed, mode="w", format=tarfile.PAX_FORMAT) as archive:
        for path in sorted(source.rglob("*")):
            if path.is_symlink() or not path.is_dir():
                _write_tar(archive, path, _archive_name(source, path))
        _write_tar_bytes(archive, "build-metadata.json", encoded_metadata)
        _write_tar_bytes(archive, "THIRD_PARTY_NOTICES.txt", notices)


def _write_zip_bytes(archive: zipfile.ZipFile, name: str, data: bytes, path: Path | None = None) -> None:
    info = zipfile.ZipInfo(name, date_time=EPOCH)
    mode = path.lstat().st_mode if path is not None else 0o100644
    info.external_attr = ((0o120777 if path is not None and path.is_symlink() else mode) & 0xFFFF) << 16
    archive.writestr(info, data, compress_type=zipfile.ZIP_DEFLATED, compresslevel=9)


def _write_tar(archive: tarfile.TarFile, path: Path, name: str) -> None:
    info = archive.gettarinfo(str(path), arcname=name)
    info.mtime = info.uid = info.gid = 0
    info.uname = info.gname = ""
    if path.is_symlink():
        archive.addfile(info)
    else:
        with path.open("rb") as handle:
            archive.addfile(info, handle)


def _write_tar_bytes(archive: tarfile.TarFile, name: str, data: bytes) -> None:
    info = tarfile.TarInfo(name)
    info.size, info.mtime, info.mode = len(data), 0, 0o644
    archive.addfile(info, fileobj=io.BytesIO(data))


def _archive_name(source: Path, path: Path) -> str:
    relative = path.relative_to(source).as_posix()
    return f"{source.name}/{relative}" if source.suffix == ".app" else relative


def _notices(source: Path) -> str:
    return "Island Browser internal package\n\nChromium Embedded Framework (CEF) is included in this package.\n" + "".join(f"\n--- {path.relative_to(source)} ---\n{path.read_text(encoding='utf-8', errors='replace')}\n" for path in sorted(item for item in source.rglob("*") if item.is_file() and item.name.lower().startswith(("license", "notice", "copying"))))


def _temporary_path(directory: Path, name: str) -> Path:
    handle = tempfile.NamedTemporaryFile(prefix=f".{name}.", suffix=".tmp", dir=directory, delete=False)
    handle.close()
    return Path(handle.name)


def _write_atomically(path: Path, data: bytes) -> None:
    temporary = _temporary_path(path.parent, path.name)
    try:
        _ = temporary.write_bytes(data)
        _ = temporary.replace(path)
    finally:
        _ = temporary.unlink(missing_ok=True)


def _digest(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def main() -> int:
    try:
        print(package(parse_arguments()))
    except PackageError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
