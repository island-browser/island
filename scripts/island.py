#!/usr/bin/env python3
"""Island hybrid build router.

Routes build, test, package, smoke, and dependency-fetch commands either to the
authoritative native host or to a locked, digest-pinned Linux container. Native
commands never start Docker. Unsupported combinations are refused with stable
exit codes.
"""

from __future__ import annotations

import argparse
import platform
import shutil
import subprocess
import sys
from dataclasses import dataclass
from pathlib import Path
from typing import Final

SCRIPT_DIRECTORY = Path(__file__).resolve().parent
REPOSITORY_ROOT = SCRIPT_DIRECTORY.parent
if str(SCRIPT_DIRECTORY) in sys.path:
    sys.path.remove(str(SCRIPT_DIRECTORY))
# Append, never prepend: test suites import this module alongside the repo's
# `deps` package, and prepending would let the flat scripts/deps.py entry
# point shadow that package on sys.path.
sys.path.append(str(SCRIPT_DIRECTORY))

from container_runner import ContainerRunnerError, dry_run_string, execute, is_emulated, plan_invocation
from image_lock import ImageLock, ImageLockError, load_image_lock
from report_writer import ReportError, build_report, write_report
from task_model import TaskDeclaration, TaskDeclarationError, TaskSet, get_task, load_tasks

EXIT_SUCCESS: Final = 0
EXIT_UNSUPPORTED: Final = 2
EXIT_TOOL_MISSING: Final = 3
EXIT_TASK_INVALID: Final = 4
EXIT_IMAGE_LOCK_INVALID: Final = 5
EXIT_CONTAINER_FAILED: Final = 6
EXIT_REPORT_FAILED: Final = 7

_LINUX_ARCH_MAP: Final = {"x64": "linux64", "arm64": "linuxarm64"}
_HOST_ARCH_MAP: Final = {"x86_64": "x64", "amd64": "x64", "aarch64": "arm64", "arm64": "arm64"}


@dataclass(frozen=True, slots=True)
class HostInfo:
    """Resolved host platform and architecture."""

    system: str
    machine: str
    arch_label: str

    @property
    def is_linux(self) -> bool:
        return self.system == "Linux"

    @property
    def is_macos(self) -> bool:
        return self.system == "Darwin"

    @property
    def is_windows(self) -> bool:
        return self.system == "Windows"


def _resolve_host() -> HostInfo:
    system = platform.system()
    machine = platform.machine()
    arch_label = _HOST_ARCH_MAP.get(machine, "unknown")
    return HostInfo(system=system, machine=machine, arch_label=arch_label)


class _RouterExit(Exception):
    """Internal control-flow exception carrying a stable exit code."""

    def __init__(self, code: int) -> None:
        super().__init__(f"router exit {code}")
        self.code = code


def _load_locks(root: Path) -> tuple[ImageLock, TaskSet]:
    try:
        image_lock = load_image_lock(root / "docker" / "images.lock.json")
    except ImageLockError as error:
        print(f"error: {error}", file=sys.stderr)
        raise _RouterExit(EXIT_IMAGE_LOCK_INVALID) from error
    try:
        task_set = load_tasks(root / "docker" / "tasks.json")
    except TaskDeclarationError as error:
        print(f"error: {error}", file=sys.stderr)
        raise _RouterExit(EXIT_TASK_INVALID) from error
    return image_lock, task_set


def _native_tool_available(tool: str) -> bool:
    return shutil.which(tool) is not None


def _cmd_doctor(args: argparse.Namespace, root: Path, host: HostInfo) -> int:
    image_lock, task_set = _load_locks(root)
    docker_available = shutil.which("docker") is not None
    cmake_available = _native_tool_available("cmake")
    print(f"host: {host.system}/{host.machine} (arch={host.arch_label})")
    print(f"docker: {'available' if docker_available else 'not available'}")
    print(f"cmake: {'available' if cmake_available else 'not available'}")
    print(f"image lock digest: {image_lock.digest}")
    print(f"task set digest: {task_set.digest}")
    for name, record in image_lock.images.items():
        print(f"image[{name}]: {record.repository}@{record.tag} digest={record.manifest_digest}")
    for name, task in task_set.tasks.items():
        print(f"task[{name}]: stage={task.stage} network={task.network_policy} archs={list(task.architectures)}")
    return EXIT_SUCCESS


def _cmd_test(args: argparse.Namespace, root: Path, host: HostInfo) -> int:
    if args.sub != "portable":
        print(f"error: unsupported test target: {args.sub}", file=sys.stderr)
        return EXIT_UNSUPPORTED
    image_lock, task_set = _load_locks(root)
    task = get_task(task_set, "portable")
    if host.arch_label not in task.architectures:
        print(f"error: portable tests do not support arch {host.arch_label}", file=sys.stderr)
        return EXIT_UNSUPPORTED
    return _run_container_task(args, task, image_lock, host, root, "portable", host.arch_label)


def _cmd_build(args: argparse.Namespace, root: Path, host: HostInfo) -> int:
    if args.sub == "linux":
        if args.arch not in ("x64", "arm64"):
            print(f"error: unsupported linux arch: {args.arch}", file=sys.stderr)
            return EXIT_UNSUPPORTED
        image_lock, task_set = _load_locks(root)
        task = get_task(task_set, "linux-native")
        if args.arch not in task.architectures:
            print(f"error: linux-native does not support arch {args.arch}", file=sys.stderr)
            return EXIT_UNSUPPORTED
        target = _LINUX_ARCH_MAP[args.arch]
        return _run_container_task(args, task, image_lock, host, root, "linux-native", args.arch, target)
    if args.sub == "native":
        return _run_native_build(root, host)
    print(f"error: unsupported build target: {args.sub}", file=sys.stderr)
    return EXIT_UNSUPPORTED


def _cmd_package(args: argparse.Namespace, root: Path, host: HostInfo) -> int:
    if args.sub != "native":
        print(f"error: unsupported package target: {args.sub}", file=sys.stderr)
        return EXIT_UNSUPPORTED
    if not _native_tool_available("cmake"):
        print("error: cmake is required for native packaging", file=sys.stderr)
        return EXIT_TOOL_MISSING
    try:
        result = subprocess.run(
            ["python3", str(root / "scripts" / "package.py"), "--target", _native_target(host)],
            cwd=root,
            check=False,
        )
        return int(result.returncode)
    except (OSError, subprocess.SubprocessError) as error:
        print(f"error: native packaging failed: {error}", file=sys.stderr)
        return EXIT_CONTAINER_FAILED


def _cmd_smoke(args: argparse.Namespace, root: Path, host: HostInfo) -> int:
    if args.sub != "native":
        print(f"error: unsupported smoke target: {args.sub}", file=sys.stderr)
        return EXIT_UNSUPPORTED
    if not host.is_macos:
        print(f"error: native smoke is only supported on macOS, not {host.system}", file=sys.stderr)
        return EXIT_UNSUPPORTED
    app_path = root / "build" / "src" / "main" / "island_browser.app"
    if not app_path.is_dir():
        print(f"error: built app not found at {app_path}", file=sys.stderr)
        return EXIT_TOOL_MISSING
    try:
        result = subprocess.run(["open", str(app_path), "--args", "--island-smoke-test"], check=False)
        return int(result.returncode)
    except (OSError, subprocess.SubprocessError) as error:
        print(f"error: native smoke failed: {error}", file=sys.stderr)
        return EXIT_CONTAINER_FAILED


def _cmd_deps(args: argparse.Namespace, root: Path, host: HostInfo) -> int:
    if args.sub != "fetch":
        print(f"error: unsupported deps command: {args.sub}", file=sys.stderr)
        return EXIT_UNSUPPORTED
    if not args.target:
        print("error: --target is required for deps fetch", file=sys.stderr)
        return EXIT_UNSUPPORTED
    command = ["python3", str(root / "scripts" / "deps.py"), "install", "--target", args.target]
    native_target = _native_target(host)
    if args.target != native_target:
        # Non-blocking dry-run for a target this host cannot install natively.
        print(f"notice: host target is {native_target}; requested {args.target} cannot be "
              f"installed natively here. Printing the fetch plan without downloading:")
        command.append("--dry-run")
    try:
        result = subprocess.run(command, cwd=root, check=False)
        return int(result.returncode)
    except (OSError, subprocess.SubprocessError) as error:
        print(f"error: deps fetch failed: {error}", file=sys.stderr)
        return EXIT_CONTAINER_FAILED


def _native_target(host: HostInfo) -> str:
    mapping = {("Darwin", "x64"): "macosx64", ("Darwin", "arm64"): "macosarm64",
               ("Windows", "x64"): "windows64", ("Windows", "arm64"): "windowsarm64",
               ("Linux", "x64"): "linux64", ("Linux", "arm64"): "linuxarm64"}
    return mapping.get((host.system, host.arch_label), "unknown")


def _run_native_build(root: Path, host: HostInfo) -> int:
    if not _native_tool_available("cmake"):
        print("error: cmake is required for native build", file=sys.stderr)
        return EXIT_TOOL_MISSING
    try:
        configure = subprocess.run(["cmake", "-B", str(root / "build"), "-S", str(root)], cwd=root, check=False)
        if configure.returncode != 0:
            return int(configure.returncode)
        build = subprocess.run(["cmake", "--build", str(root / "build")], cwd=root, check=False)
        return int(build.returncode)
    except (OSError, subprocess.SubprocessError) as error:
        print(f"error: native build failed: {error}", file=sys.stderr)
        return EXIT_CONTAINER_FAILED


def _run_container_task(
    args: argparse.Namespace,
    task: TaskDeclaration,
    image_lock: ImageLock,
    host: HostInfo,
    root: Path,
    task_name: str,
    arch: str,
    target: str | None = None,
) -> int:
    extra_args: tuple[str, ...] = ()
    env: tuple[tuple[str, str], ...] = ()
    if target and task_name == "linux-native":
        extra_args = ("--target", target)
        env = (("ISLAND_TARGET", target),)
    # Pre-create the export directory as the host user so a Linux host does not
    # get a root-owned bind-mount target created by the Docker daemon.
    (root / "build" / "container-artifacts").mkdir(parents=True, exist_ok=True)
    try:
        invocation = plan_invocation(task, image_lock, arch, root, extra_args=extra_args, env=env)
    except ContainerRunnerError as error:
        print(f"error: {error}", file=sys.stderr)
        return EXIT_TOOL_MISSING
    resolved_target = target or arch
    emulated = is_emulated(host.arch_label, arch)
    if args.dry_run:
        print(dry_run_string(invocation))
        return _write_task_report(task, root, resolved_target, "docker", emulated, invocation.digest, EXIT_SUCCESS)
    try:
        exit_code = execute(invocation)
    except ContainerRunnerError as error:
        print(f"error: {error}", file=sys.stderr)
        exit_code = EXIT_CONTAINER_FAILED
    report_code = _write_task_report(task, root, resolved_target, "docker", emulated, invocation.digest, exit_code)
    if report_code != EXIT_SUCCESS:
        return report_code
    return exit_code if exit_code == 0 else EXIT_CONTAINER_FAILED


def _write_task_report(
    task: TaskDeclaration,
    root: Path,
    target: str,
    mode: str,
    emulated: bool,
    digest: str | None,
    exit_code: int,
) -> int:
    artifact_dir = root / "build" / "container-artifacts"
    report = build_report(task, target, mode, emulated, digest, root, exit_code, artifact_dir)
    try:
        write_report(report, root / "build" / "reports")
    except (ReportError, OSError) as error:
        print(f"error: report writing failed: {error}", file=sys.stderr)
        return EXIT_REPORT_FAILED
    return EXIT_SUCCESS


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--dry-run", action="store_true", help="Plan and print, do not execute.")
    parser.add_argument("--root", default=str(REPOSITORY_ROOT))
    subparsers = parser.add_subparsers(dest="command", required=True)
    subparsers.add_parser("doctor")
    test_parser = subparsers.add_parser("test")
    test_parser.add_argument("sub", choices=["portable"])
    build_parser = subparsers.add_parser("build")
    build_parser.add_argument("sub", choices=["linux", "native"])
    build_parser.add_argument("--arch", choices=["x64", "arm64"], default="x64")
    package_parser = subparsers.add_parser("package")
    package_parser.add_argument("sub", choices=["native"])
    smoke_parser = subparsers.add_parser("smoke")
    smoke_parser.add_argument("sub", choices=["native"])
    deps_parser = subparsers.add_parser("deps")
    deps_parser.add_argument("sub", choices=["fetch"])
    deps_parser.add_argument("--target")
    parsed = parser.parse_args()
    root = Path(parsed.root).resolve()
    host = _resolve_host()
    handlers = {
        "doctor": _cmd_doctor, "test": _cmd_test, "build": _cmd_build,
        "package": _cmd_package, "smoke": _cmd_smoke, "deps": _cmd_deps,
    }
    try:
        return handlers[parsed.command](parsed, root, host)
    except _RouterExit as exit_signal:
        return exit_signal.code


if __name__ == "__main__":
    raise SystemExit(main())
