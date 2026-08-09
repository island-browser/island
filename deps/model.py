"""Typed dependency lock-file parsing and target resolution."""

from __future__ import annotations

import hashlib
import json
import platform
from dataclasses import dataclass
from pathlib import Path
from typing import Final
from urllib.parse import quote


class DependencyError(Exception):
    """Base error reported by dependency operations."""


@dataclass(frozen=True, slots=True)
class ManifestError(DependencyError):
    detail: str

    def __str__(self) -> str:
        return f"Invalid dependency lock file: {self.detail}"


@dataclass(frozen=True, slots=True)
class TargetError(DependencyError):
    target: str

    def __str__(self) -> str:
        return f"Unsupported CEF target: {self.target}"


@dataclass(frozen=True, slots=True)
class Artifact:
    """An immutable downloadable archive."""

    name: str
    target: str
    url: str
    sha256: str
    archive: str
    source: str
    max_archive_bytes: int = 400000000
    max_members: int = 50000
    max_member_bytes: int = 400000000
    max_extract_bytes: int = 900000000
    expected_archive_bytes: int = 0


@dataclass(frozen=True, slots=True)
class LockFile:
    """Validated dependency lock records."""

    digest: str
    cef_version: str
    cef_hashes: dict[str, str]
    cef_limits: tuple[int, int, int]
    cef_archives: dict[str, tuple[str, int, int]]
    geist_commit: str
    geist: Artifact
    geist_files: tuple[str, ...]
    geist_licenses: tuple[str, ...]


_CEF_HOST: Final = "cef-builds.spotifycdn.com"
_HEX_LENGTH: Final = 64
_CEF_TARGETS: Final = frozenset({"macosx64", "macosarm64", "windows64", "windowsarm64", "linux64", "linuxarm64"})
_GEIST_FILES: Final = frozenset({"Geist-Regular.ttf", "Geist-Medium.ttf", "Geist-SemiBold.ttf", "Geist-Bold.ttf", "GeistMono-Regular.ttf", "GeistMono-Medium.ttf", "GeistMono-SemiBold.ttf", "GeistMono-Bold.ttf"})


def _string(value: object, field: str) -> str:
    if not isinstance(value, str) or not value:
        raise ManifestError(f"{field} must be a non-empty string")
    return value


def _mapping(value: object, field: str) -> dict[str, object]:
    if not isinstance(value, dict) or not all(isinstance(key, str) for key in value):
        raise ManifestError(f"{field} must be an object")
    return value


def _sha256(value: object, field: str) -> str:
    digest = _string(value, field)
    if len(digest) != _HEX_LENGTH or any(char not in "0123456789abcdef" for char in digest):
        raise ManifestError(f"{field} must be a lowercase SHA-256 digest")
    return digest


def _url(value: object, field: str, host: str) -> str:
    url = _string(value, field)
    prefix = f"https://{host}/"
    if not url.startswith(prefix):
        raise ManifestError(f"{field} must use HTTPS host {host}")
    return url


def _limits(value: object, field: str) -> tuple[int, int, int, int]:
    limits = _mapping(value, field)
    names = ("max_archive_bytes", "max_members", "max_member_bytes", "max_extract_bytes")
    parsed: list[int] = []
    for name in names:
        number = limits.get(name)
        if not isinstance(number, int) or isinstance(number, bool) or number <= 0:
            raise ManifestError(f"{field}.{name} must be a positive integer")
        parsed.append(number)
    return parsed[0], parsed[1], parsed[2], parsed[3]


def load_lock(path: Path) -> LockFile:
    """Load and semantically validate the immutable lock file."""
    try:
        raw = path.read_bytes()
        document = json.loads(raw)
    except (OSError, json.JSONDecodeError) as error:
        raise ManifestError(str(error)) from error
    root = _mapping(document, "root")
    if root.get("schema") != 1:
        raise ManifestError("schema must equal 1")
    dependencies = _mapping(root.get("dependencies"), "dependencies")
    cef = _mapping(dependencies.get("cef"), "dependencies.cef")
    geist = _mapping(dependencies.get("geist"), "dependencies.geist")
    version = _string(cef.get("version"), "dependencies.cef.version")
    cef_limits = _limits(cef.get("limits"), "dependencies.cef.limits")[1:]
    if _string(cef.get("host"), "dependencies.cef.host") != _CEF_HOST:
        raise ManifestError("dependencies.cef.host is not allowlisted")
    if _string(cef.get("archive"), "dependencies.cef.archive") != "tar.bz2":
        raise ManifestError("dependencies.cef.archive must be tar.bz2")
    if _string(cef.get("sentinel"), "dependencies.cef.sentinel") != "cmake/cef_macros.cmake":
        raise ManifestError("dependencies.cef.sentinel is incorrect")
    targets = _mapping(cef.get("targets"), "dependencies.cef.targets")
    archives: dict[str, tuple[str, int, int]] = {}
    for target, record in targets.items():
        entry = _mapping(record, f"dependencies.cef.targets.{target}")
        archive_bytes = entry.get("archive_bytes")
        maximum = entry.get("max_archive_bytes")
        if not isinstance(archive_bytes, int) or not isinstance(maximum, int) or archive_bytes <= 0 or maximum < archive_bytes:
            raise ManifestError(f"dependencies.cef.targets.{target} has invalid archive bounds")
        archives[target] = (_sha256(entry.get("sha256"), f"dependencies.cef.targets.{target}.sha256"), archive_bytes, maximum)
    if set(archives) != _CEF_TARGETS:
        raise ManifestError("dependencies.cef.targets must list every supported CEF target")
    if _string(geist.get("host"), "dependencies.geist.host") != "github.com":
        raise ManifestError("dependencies.geist.host is not allowlisted")
    if _string(geist.get("archive"), "dependencies.geist.archive") != "tar.gz":
        raise ManifestError("dependencies.geist.archive must be tar.gz")
    geist_url = _url(geist.get("url"), "dependencies.geist.url", "github.com")
    geist_limits = _limits(geist.get("limits"), "dependencies.geist.limits")
    geist_commit = _string(geist.get("commit"), "dependencies.geist.commit")
    if len(geist_commit) != 40 or any(char not in "0123456789abcdef" for char in geist_commit):
        raise ManifestError("dependencies.geist.commit must be a lowercase Git commit")
    files_raw = geist.get("files")
    if not isinstance(files_raw, list) or not all(isinstance(item, str) for item in files_raw):
        raise ManifestError("dependencies.geist.files must be an array of strings")
    files = tuple(files_raw)
    if set(files) != _GEIST_FILES or len(files) != len(_GEIST_FILES):
        raise ManifestError("dependencies.geist.files must list approved static font weights")
    return LockFile(
        digest=hashlib.sha256(raw).hexdigest(),
        cef_version=version,
        cef_hashes={target: record[0] for target, record in archives.items()},
        cef_limits=cef_limits,
        cef_archives=archives,
        geist_commit=geist_commit,
        geist=Artifact(
            name="geist",
            target="all",
            url=geist_url,
            sha256=_sha256(geist.get("sha256"), "dependencies.geist.sha256"),
            archive="tar.gz",
            source=_string(geist.get("source"), "dependencies.geist.source"),
            max_archive_bytes=geist_limits[0], max_members=geist_limits[1], max_member_bytes=geist_limits[2], max_extract_bytes=geist_limits[3],
        ),
        geist_files=files,
        geist_licenses=_licenses(geist.get("license_files")),
    )


def _licenses(value: object) -> tuple[str, ...]:
    if not isinstance(value, list) or not all(isinstance(item, str) and item in {"OFL.txt", "LICENSE.txt"} for item in value):
        raise ManifestError("dependencies.geist.license_files must list approved license files")
    if set(value) != {"OFL.txt", "LICENSE.txt"}:
        raise ManifestError("dependencies.geist.license_files must include OFL.txt and LICENSE.txt")
    return tuple(value)


def detect_target(system: str = platform.system(), machine: str = platform.machine()) -> str:
    """Map the host platform to an explicit supported CEF target."""
    mappings = {
        ("Darwin", "x86_64"): "macosx64", ("Darwin", "arm64"): "macosarm64",
        ("Windows", "AMD64"): "windows64", ("Windows", "ARM64"): "windowsarm64",
        ("Linux", "x86_64"): "linux64", ("Linux", "aarch64"): "linuxarm64",
    }
    try:
        return mappings[(system, machine)]
    except KeyError as error:
        raise TargetError(f"{system}/{machine}") from error


def resolve_cef(lock: LockFile, target: str) -> Artifact:
    """Resolve the CEF artifact for a validated target."""
    try:
        digest, archive_bytes, maximum = lock.cef_archives[target]
    except KeyError as error:
        raise TargetError(target) from error
    encoded_version = quote(lock.cef_version, safe="")
    return Artifact(
        name="cef", target=target,
        url=f"https://{_CEF_HOST}/cef_binary_{encoded_version}_{target}.tar.bz2",
        sha256=digest, archive="tar.bz2", source="official CEF binary distribution",
        max_archive_bytes=maximum, max_members=lock.cef_limits[0], max_member_bytes=lock.cef_limits[1], max_extract_bytes=lock.cef_limits[2], expected_archive_bytes=archive_bytes,
    )
