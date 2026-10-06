#!/usr/bin/env python3
# /// script
# requires-python = ">=3.11"
# dependencies = []
# ///
"""Assembles the GitHub release assets from per-target package outputs. Stdlib only.

    python3 scripts/release_assets.py --staging staging --version 0.4.0 --output release-dist

`--staging` holds the downloaded `unsigned-candidate-<version>-<target>` artifacts, each with
one archive and the `SHA256SUMS.txt` that `scripts/package.py` wrote next to it (any directory
depth). The command verifies every archive against its own checksum line, requires exactly one
archive per target (all six), copies them into `--output`, and writes a combined
`SHA256SUMS.txt` there: one `<sha256>  <file name>` line per archive, sorted by file name, LF
line endings. The in-browser updater verifies downloads against that exact file.
"""

from __future__ import annotations

import argparse
import hashlib
import shutil
import sys
from pathlib import Path
from typing import Final

from package import SEMVER, Target, archive_name

SUMS_NAME: Final = "SHA256SUMS.txt"


class AssetError(Exception):
    pass


def _digest(path: Path) -> str:
    digest = hashlib.sha256()
    with path.open("rb") as handle:
        for block in iter(lambda: handle.read(1024 * 1024), b""):
            digest.update(block)
    return digest.hexdigest()


def _parse_sums(path: Path) -> dict[str, str]:
    entries: dict[str, str] = {}
    for number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        if not line.strip():
            continue
        parts = line.split(maxsplit=1)
        if len(parts) != 2 or len(parts[0]) != 64 or any(c not in "0123456789abcdef" for c in parts[0]):
            raise AssetError(f"{path}:{number}: not a '<sha256>  <file>' line: {line!r}")
        name = parts[1].lstrip("*")
        if name in entries:
            raise AssetError(f"{path}:{number}: duplicate entry for {name}")
        entries[name] = parts[0]
    return entries


def expected_archives(version: str) -> list[str]:
    if SEMVER.fullmatch(version) is None or "+" in version:
        raise AssetError(f"version must be SemVer without build metadata, got {version!r}")
    return sorted(archive_name(version, target) for target in Target)


def assemble(staging: Path, version: str, output: Path) -> list[tuple[str, str]]:
    """Copies the verified archives into `output` and writes the combined SHA256SUMS.txt.

    Returns the (digest, file name) pairs written, sorted by file name."""
    expected = expected_archives(version)
    if not staging.is_dir():
        raise AssetError(f"staging directory does not exist: {staging}")
    found: dict[str, tuple[Path, str]] = {}
    for sums in sorted(staging.rglob(SUMS_NAME)):
        for name, digest in _parse_sums(sums).items():
            archive = sums.parent / name
            if Path(name).name != name or not archive.is_file():
                raise AssetError(f"{sums} lists {name!r}, which is not a file beside it")
            if name in found:
                raise AssetError(f"{name} appears in more than one artifact")
            actual = _digest(archive)
            if actual != digest:
                raise AssetError(f"checksum mismatch for {archive}: expected {digest}, got {actual}")
            found[name] = (archive, digest)
    listed = {path.resolve() for path, _ in found.values()}
    for path in sorted(staging.rglob("island_browser-*")):
        if path.is_file() and path.resolve() not in listed:
            raise AssetError(f"{path} is not listed in any {SUMS_NAME}")
    missing = [name for name in expected if name not in found]
    unexpected = sorted(name for name in found if name not in expected)
    if missing or unexpected:
        raise AssetError("archive set does not match the six targets for "
                         f"{version}: missing {missing}, unexpected {unexpected}")
    output.mkdir(parents=True, exist_ok=True)
    if any(output.iterdir()):
        raise AssetError(f"output directory is not empty: {output}")
    rows: list[tuple[str, str]] = []
    for name in expected:
        archive, digest = found[name]
        shutil.copyfile(archive, output / name)
        rows.append((digest, name))
    (output / SUMS_NAME).write_bytes("".join(f"{d}  {n}\n" for d, n in rows).encode("utf-8"))
    return rows


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--staging", type=Path, required=True)
    parser.add_argument("--version", required=True)
    parser.add_argument("--output", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        for digest, name in assemble(args.staging, args.version, args.output):
            print(f"{digest}  {name}")
    except (AssetError, OSError) as error:
        print(f"error: {error}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
