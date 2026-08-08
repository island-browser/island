#!/usr/bin/env python3
"""Resolve and install Island's immutable binary dependencies."""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path

SCRIPT_DIRECTORY = Path(__file__).resolve().parent
REPOSITORY_ROOT = SCRIPT_DIRECTORY.parent
if str(SCRIPT_DIRECTORY) in sys.path:
    sys.path.remove(str(SCRIPT_DIRECTORY))
sys.path.insert(0, str(REPOSITORY_ROOT))

from deps.install import InstallError, clean, install_cef, install_geist, verify
from deps.model import DependencyError, detect_target, load_lock, resolve_cef
from deps.updates import check_artifact


def _root(raw: str) -> Path:
    return Path(raw).resolve()


def _lock(root: Path):
    return load_lock(root / "deps" / "dependencies.lock.json")


def _target(value: str | None) -> str:
    return value if value is not None else detect_target()


def _resolve(args: argparse.Namespace) -> int:
    root = _root(args.root)
    lock = _lock(root)
    target = _target(args.target)
    artifact = resolve_cef(lock, target)
    print(json.dumps({"target": target, "cef_url": artifact.url, "geist_url": lock.geist.url}, sort_keys=True))
    return 0


def _install(args: argparse.Namespace) -> int:
    root = _root(args.root)
    lock = _lock(root)
    target = _target(args.target)
    artifact = resolve_cef(lock, target)
    if args.dry_run:
        print(f"CEF URL: {artifact.url}")
        print(f"Geist font source: {lock.geist.url}")
        return 0
    cef_destination = root / "third_party" / "cef"
    font_destination = root / "assets" / "fonts"
    if not args.force:
        if cef_destination.exists() or font_destination.exists():
            try:
                verify(root, lock, target)
            except InstallError as error:
                raise InstallError(f"existing dependency installation is invalid ({error}); run install --force to replace it") from error
            print("Dependencies already verified; skipping download.")
            return 0
    install_cef(root, lock, artifact, False)
    install_geist(root, lock, False)
    verify(root, lock, target)
    print("Dependencies ready.")
    return 0


def _verify(args: argparse.Namespace) -> int:
    root = _root(args.root)
    verify(root, _lock(root), _target(args.target))
    print("Dependencies verified.")
    return 0


def _cache_key(args: argparse.Namespace) -> int:
    root = _root(args.root)
    lock = _lock(root)
    target = _target(args.target)
    print(f"island-deps-{lock.digest}-{target}")
    return 0


def _updates(args: argparse.Namespace) -> int:
    lock = _lock(_root(args.root))
    cef = resolve_cef(lock, _target(args.target))
    print(f"CEF {lock.cef_version}: {check_artifact(cef, frozenset({'cef-builds.spotifycdn.com'}))}")
    print(f"Geist {lock.geist.source}: {check_artifact(lock.geist, frozenset({'github.com', 'codeload.github.com'}))}")
    return 0


def _clean(args: argparse.Namespace) -> int:
    clean(_root(args.root))
    print("Generated dependencies removed.")
    return 0


def main() -> int:
    """Parse a thin command-line interface around resolver operations."""
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--root", default=str(REPOSITORY_ROOT))
    parser.add_argument("--target")
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("resolve", "verify", "check-updates", "print-cache-key", "clean"):
        command = commands.add_parser(name)
        command.add_argument("--target", default=argparse.SUPPRESS)
    install = commands.add_parser("install")
    install.add_argument("--dry-run", action="store_true")
    install.add_argument("--force", action="store_true")
    install.add_argument("--target", default=argparse.SUPPRESS)
    args = parser.parse_args()
    handlers = {"resolve": _resolve, "install": _install, "verify": _verify, "check-updates": _updates, "print-cache-key": _cache_key, "clean": _clean}
    try:
        return handlers[args.command](args)
    except DependencyError as error:
        print(f"error: {error}", file=sys.stderr)
        return 1


if __name__ == "__main__":
    raise SystemExit(main())
