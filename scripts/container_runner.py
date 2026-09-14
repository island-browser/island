"""Docker execution planner and runner with enforced security constraints."""

from __future__ import annotations

import os
import shutil
import subprocess
from dataclasses import dataclass
from pathlib import Path
from typing import Final

from image_lock import ImageLock, resolve_digest_for_architecture
from task_model import TaskDeclaration

_SRC_MOUNT: Final = "/src"
_WORK_MOUNT: Final = "/work"
_OUT_MOUNT: Final = "/work/out"
_ISLAND_UID: Final = 1000
_ISLAND_GID: Final = 1000

_ARCH_NORMALIZE: Final = {"x64": "amd64", "arm64": "arm64", "amd64": "amd64", "aarch64": "arm64"}


def _normalize_arch(arch: str) -> str:
    """Normalize router arch labels to Docker/image-lock architecture keys."""
    normalized = _ARCH_NORMALIZE.get(arch)
    if normalized is None:
        raise ContainerRunnerError(f"unsupported architecture: {arch}")
    return normalized


class ContainerRunnerError(Exception):
    """Base error reported by container runner operations."""


@dataclass(frozen=True, slots=True)
class MountAllowlistEntry:
    """A single allowed host-to-container mount."""

    host_path: str
    container_path: str
    read_only: bool


_MOUNT_ALLOWLIST: Final = frozenset({
    # "." mounts the repository root at /src: task entry points reference
    # /src/scripts and /src/tests, and the in-container build copies from /src
    # into the writable /work volume.
    MountAllowlistEntry(".", _SRC_MOUNT, True),
    MountAllowlistEntry("build/container-artifacts", _OUT_MOUNT, False),
})

_FORBIDDEN_MOUNT_PATTERNS: Final = (
    str(Path.home()),
    str(Path.home() / "Desktop"),
    str(Path.home() / "Downloads"),
    str(Path.home() / ".docker"),
    str(Path.home() / ".ssh"),
    "/var/run/docker.sock",
)


@dataclass(frozen=True, slots=True)
class DockerInvocation:
    """A fully planned Docker command line."""

    image: str
    digest: str
    arguments: list[str]
    network: str
    mounts: tuple[MountAllowlistEntry, ...]
    user: str
    workdir: str


def _docker_available() -> bool:
    return shutil.which("docker") is not None


def _refuse_forbidden_mounts() -> None:
    for pattern in _FORBIDDEN_MOUNT_PATTERNS:
        if pattern and pattern in os.environ.get("ISLAND_EXTRA_MOUNTS", ""):
            raise ContainerRunnerError(f"refusing to mount forbidden host path: {pattern}")


def _map_user() -> str:
    uid = os.getuid() if hasattr(os, "getuid") else _ISLAND_UID
    gid = os.getgid() if hasattr(os, "getgid") else _ISLAND_GID
    return f"{uid}:{gid}"


def plan_invocation(
    task: TaskDeclaration,
    lock: ImageLock,
    host_arch: str,
    root: Path,
    network_override: str | None = None,
    extra_args: tuple[str, ...] = (),
    env: tuple[tuple[str, str], ...] = (),
) -> DockerInvocation:
    """Plan a Docker invocation without executing it; validates all constraints."""
    if not _docker_available():
        raise ContainerRunnerError("docker is not available on the host")
    _refuse_forbidden_mounts()
    docker_arch = _normalize_arch(host_arch)
    record = lock.images[task.stage]
    digest = resolve_digest_for_architecture(lock, task.stage, docker_arch)
    image_ref = f"{record.repository}@{digest}"
    network = network_override if network_override is not None else task.network_policy
    if network == "none":
        docker_network = "none"
    elif network == "dependency-fetch":
        docker_network = "default"
    else:
        raise ContainerRunnerError(f"unsupported network policy: {network}")
    mounts = tuple(_MOUNT_ALLOWLIST)
    user = _map_user()
    arguments = [
        "run",
        "--rm",
        f"--network={docker_network}",
        f"--user={user}",
        f"--workdir={_WORK_MOUNT}",
        f"--mount=type=volume,source=island-work,target={_WORK_MOUNT}",
        f"--mount=type=volume,source=island-deps-cache,target=/work/deps-cache",
        f"--mount=type=volume,source=island-compiler-cache,target=/work/compiler-cache",
    ]
    for key, value in env:
        arguments.append(f"--env={key}={value}")
    for mount in mounts:
        source = root.resolve() / mount.host_path
        ro_flag = ",readonly" if mount.read_only else ""
        arguments.append(f"--mount=type=bind,source={source},target={mount.container_path}{ro_flag}")
    arguments.append(image_ref)
    arguments.extend(task.entry_point)
    arguments.extend(extra_args)
    return DockerInvocation(
        image=image_ref,
        digest=digest,
        arguments=arguments,
        network=docker_network,
        mounts=mounts,
        user=user,
        workdir=_WORK_MOUNT,
    )


def dry_run_string(invocation: DockerInvocation) -> str:
    """Return the docker command line as a string for dry-run output."""
    return "docker " + " ".join(invocation.arguments)


def execute(invocation: DockerInvocation, timeout: int = 1800) -> int:
    """Execute the planned Docker invocation and return its exit code."""
    if not _docker_available():
        raise ContainerRunnerError("docker is not available on the host")
    try:
        result = subprocess.run(
            ["docker", *invocation.arguments],
            timeout=timeout,
            check=False,
        )
        return int(result.returncode)
    except subprocess.TimeoutExpired as error:
        raise ContainerRunnerError(f"docker invocation timed out after {timeout}s") from error
    except (OSError, subprocess.SubprocessError) as error:
        raise ContainerRunnerError(f"docker invocation failed: {error}") from error


def is_emulated(host_arch: str, target_arch: str) -> bool:
    """Return True when the target architecture differs from the host."""
    return host_arch != target_arch
