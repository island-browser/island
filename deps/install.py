"""Download, archive validation, atomic installation, and offline verification."""

from __future__ import annotations

import hashlib
import json
import os
import shutil
import tarfile
import tempfile
from dataclasses import asdict, dataclass
from pathlib import Path, PurePosixPath
from urllib.error import HTTPError, URLError
from urllib.parse import urlparse
from urllib.request import Request, urlopen

from deps.model import Artifact, DependencyError, LockFile


@dataclass(frozen=True, slots=True)
class InstallError(DependencyError):
    detail: str

    def __str__(self) -> str:
        return f"Dependency installation failed: {self.detail}"


@dataclass(frozen=True, slots=True)
class Receipt:
    dependency: str
    target: str
    manifest_sha256: str
    source: str
    archive_sha256: str
    tree_sha256: str


_RECEIPT = ".island-dependency-receipt.json"
_CHUNK = 1024 * 1024


def _safe_member(member: tarfile.TarInfo) -> None:
    name = PurePosixPath(member.name)
    if name.is_absolute() or ".." in name.parts or not name.parts:
        raise InstallError("archive contains an unsafe path")
    if member.issym() or member.islnk() or member.isdev() or member.isfifo():
        raise InstallError("archive contains a link or device entry")


def _allowlisted_url(url: str, allowed_hosts: frozenset[str]) -> bool:
    parsed = urlparse(url)
    return parsed.scheme == "https" and parsed.hostname in allowed_hosts


def _tree_sha256(directory: Path) -> str:
    digest = hashlib.sha256()
    for path in sorted(directory.rglob("*")):
        if path.name == _RECEIPT:
            continue
        if path.is_symlink() or not path.is_file():
            if path.is_dir():
                continue
            raise InstallError("installed tree contains an unsafe entry")
        relative = path.relative_to(directory).as_posix().encode("utf-8")
        file_digest = hashlib.sha256()
        with path.open("rb") as source:
            while chunk := source.read(_CHUNK):
                file_digest.update(chunk)
        digest.update(len(relative).to_bytes(8, "big"))
        digest.update(relative)
        digest.update(file_digest.digest())
    return digest.hexdigest()


def download(artifact: Artifact, destination: Path, allowed_hosts: frozenset[str]) -> None:
    """Stream an allowlisted HTTPS artifact to a private temporary file."""
    parsed = urlparse(artifact.url)
    if not _allowlisted_url(artifact.url, allowed_hosts):
        raise InstallError("artifact URL is not HTTPS on an allowlisted host")
    try:
        with destination.open("wb") as output:
            digest = hashlib.sha256()
            received = 0
            expected: int | None = None
            request: Request = Request(artifact.url, headers={"Range": "bytes=0-"})
            first_request = True
            while first_request or (expected is not None and received < expected):
                first_request = False
                with urlopen(request, timeout=60) as response:
                    if not _allowlisted_url(response.geturl(), allowed_hosts):
                        raise InstallError(f"download redirect is not allowlisted for {artifact.name}")
                    content_length = response.headers.get("Content-Length")
                    if expected is None:
                        if content_length is not None and content_length.isdecimal():
                            expected = int(content_length)
                            if artifact.expected_archive_bytes and expected != artifact.expected_archive_bytes:
                                raise InstallError(f"download size does not match expected artifact for {artifact.name}")
                            if expected > artifact.max_archive_bytes:
                                raise InstallError(f"download exceeds archive size limit for {artifact.name}")
                    elif response.headers.get("Content-Range") != f"bytes {received}-{expected - 1}/{expected}":
                        raise InstallError(f"download resume range is invalid for {artifact.name}")
                    before = received
                    while chunk := response.read(_CHUNK):
                        digest.update(chunk)
                        output.write(chunk)
                        received += len(chunk)
                        if received > artifact.max_archive_bytes:
                            raise InstallError(f"download exceeds archive size limit for {artifact.name}")
                if received == before:
                    raise InstallError(f"download ended before completion for {artifact.name}")
                if expected is None:
                    break
                if received > expected:
                    raise InstallError(f"download exceeded declared size for {artifact.name}")
                if received < expected:
                    request = Request(artifact.url, headers={"Range": f"bytes={received}-"})
    except (HTTPError, URLError, OSError) as error:
        raise InstallError(f"download failed for {artifact.name}: {error}") from error
    if digest.hexdigest() != artifact.sha256:
        raise InstallError(f"SHA-256 mismatch for {artifact.name}")


def extract(archive: Path, destination: Path, artifact: Artifact) -> Path:
    """Reject unsafe tar members before extracting to a fresh private directory."""
    mode = "r:bz2" if artifact.archive == "tar.bz2" else "r:gz"
    try:
        with tarfile.open(archive, mode) as bundle:
            members: list[tarfile.TarInfo] = []
            extracted_size = 0
            for member in bundle:
                _safe_member(member)
                if len(members) >= artifact.max_members:
                    raise InstallError("archive exceeds member count limit")
                if member.size > artifact.max_member_bytes:
                    raise InstallError("archive member exceeds size limit")
                extracted_size += member.size
                if extracted_size > artifact.max_extract_bytes:
                    raise InstallError("archive exceeds extracted size limit")
                members.append(member)
            for member in members:
                bundle.extract(member, destination)
    except (OSError, tarfile.TarError) as error:
        raise InstallError(f"archive extraction failed: {error}") from error
    roots = [path for path in destination.iterdir() if path.is_dir()]
    if len(roots) != 1:
        raise InstallError("archive must contain exactly one top-level directory")
    return roots[0]


def _write_receipt(destination: Path, receipt: Receipt) -> None:
    content = json.dumps(asdict(receipt), sort_keys=True, separators=(",", ":")) + "\n"
    (destination / _RECEIPT).write_text(content, encoding="utf-8")


def _replace(destination: Path, staged: Path) -> None:
    backup = destination.with_name(f".{destination.name}.backup")
    try:
        if backup.exists():
            shutil.rmtree(backup)
        if destination.exists():
            os.replace(destination, backup)
        os.replace(staged, destination)
    except OSError as error:
        if not destination.exists() and backup.exists():
            os.replace(backup, destination)
        raise InstallError(f"atomic replacement failed: {error}") from error
    if backup.exists():
        shutil.rmtree(backup)


def install_cef(root: Path, lock: LockFile, artifact: Artifact, dry_run: bool) -> None:
    """Install CEF only after full archive verification and sentinel validation."""
    if dry_run:
        return
    destination = root / "third_party" / "cef"
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent, prefix=".island-cef-") as temporary:
        work = Path(temporary)
        archive = work / "download.tar.bz2"
        download(artifact, archive, frozenset({"cef-builds.spotifycdn.com"}))
        extracted = extract(archive, work / "extract", artifact)
        expected_root = f"cef_binary_{lock.cef_version}_{artifact.target}"
        if extracted.name != expected_root:
            raise InstallError(f"CEF archive root must be {expected_root}")
        if not (extracted / "cmake" / "cef_macros.cmake").is_file():
            raise InstallError("CEF archive is missing cmake/cef_macros.cmake")
        if not (extracted / "tests" / "cefsimple").is_dir():
            raise InstallError("CEF archive is missing tests/cefsimple")
        staged = work / "staged"
        os.replace(extracted, staged)
        _write_receipt(staged, Receipt("cef", artifact.target, lock.digest, artifact.source, artifact.sha256, _tree_sha256(staged)))
        _replace(destination, staged)


def install_geist(root: Path, lock: LockFile, dry_run: bool) -> None:
    """Install only the allowlisted static font files and OFL license."""
    if dry_run:
        return
    destination = root / "assets" / "fonts"
    destination.parent.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory(dir=destination.parent, prefix=".island-geist-") as temporary:
        work = Path(temporary)
        archive = work / "download.tar.gz"
        download(lock.geist, archive, frozenset({"github.com", "codeload.github.com"}))
        extracted = extract(archive, work / "extract", lock.geist)
        if extracted.name != f"geist-font-{lock.geist_commit}":
            raise InstallError("Geist archive root does not match the pinned commit")
        staged = work / "staged"
        staged.mkdir()
        selected = {
            filename: extracted / "fonts" / ("GeistMono" if filename.startswith("GeistMono-") else "Geist") / "ttf" / filename
            for filename in lock.geist_files
        }
        if not all(path.is_file() for path in selected.values()):
            raise InstallError("Geist archive is missing required static TTF weights")
        licenses = {filename: extracted / filename for filename in lock.geist_licenses}
        if not all(path.is_file() for path in licenses.values()):
            raise InstallError("Geist archive is missing required license files")
        for filename in lock.geist_files:
            shutil.copyfile(selected[filename], staged / filename)
        for filename, license_path in licenses.items():
            shutil.copyfile(license_path, staged / filename)
        _write_receipt(staged, Receipt("geist", "all", lock.digest, lock.geist.source, lock.geist.sha256, _tree_sha256(staged)))
        _replace(destination, staged)


def verify(root: Path, lock: LockFile, target: str) -> None:
    """Verify installed layouts and immutable receipts without network access."""
    checks = (
        (root / "third_party" / "cef", "cef", target, lock.cef_hashes[target], "cmake/cef_macros.cmake"),
        (root / "assets" / "fonts", "geist", "all", lock.geist.sha256, lock.geist_licenses[0]),
    )
    for destination, name, receipt_target, archive_hash, required in checks:
        if not (destination / required).is_file():
            raise InstallError(f"{name} installation is missing {required}")
        try:
            raw = json.loads((destination / _RECEIPT).read_text(encoding="utf-8"))
        except (OSError, json.JSONDecodeError) as error:
            raise InstallError(f"{name} receipt is unreadable: {error}") from error
        if not isinstance(raw, dict) or raw.get("manifest_sha256") != lock.digest or raw.get("archive_sha256") != archive_hash or raw.get("target") != receipt_target:
            raise InstallError(f"{name} receipt does not match the dependency lock")
        tree_sha256 = raw.get("tree_sha256")
        if not isinstance(tree_sha256, str) or tree_sha256 != _tree_sha256(destination):
            raise InstallError(f"{name} installed files do not match the receipt")
    for filename in lock.geist_files:
        if not (root / "assets" / "fonts" / filename).is_file():
            raise InstallError(f"Geist installation is missing {filename}")
    for filename in lock.geist_licenses:
        if not (root / "assets" / "fonts" / filename).is_file():
            raise InstallError(f"Geist installation is missing {filename}")


def clean(root: Path) -> None:
    """Remove only generated dependency destinations inside the repository root."""
    resolved_root = root.resolve()
    if resolved_root == Path(resolved_root.anchor) or not (resolved_root / "deps" / "dependencies.lock.json").is_file():
        raise InstallError("refusing to clean without a repository dependency lock")
    for relative in (Path("third_party/cef"), Path("assets/fonts")):
        destination = (resolved_root / relative).resolve()
        if resolved_root not in destination.parents or destination == resolved_root:
            raise InstallError("refusing to clean a path outside the repository")
        if destination.exists():
            shutil.rmtree(destination)
