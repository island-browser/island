"""Typed model for docker/tasks.json parsing, traversal rejection, and validation."""

from __future__ import annotations

import hashlib
import json
from dataclasses import dataclass
from pathlib import Path, PurePosixPath
from typing import Final

_REQUIRED_TASKS: Final = frozenset({"portable", "linux-native", "macos-native", "windows-native"})
_REQUIRED_STAGES: Final = frozenset({"portable-tests", "linux-build", "native"})
_VALID_NETWORK_POLICIES: Final = frozenset({"none", "dependency-fetch"})
_VALID_SOURCE_ACCESS: Final = frozenset({"read-only"})
_VALID_ARCHITECTURES: Final = frozenset({"x64", "arm64"})


class TaskDeclarationError(Exception):
    """Base error reported by task declaration operations."""


@dataclass(frozen=True, slots=True)
class TaskManifestError(TaskDeclarationError):
    detail: str

    def __str__(self) -> str:
        return f"Invalid task declaration: {self.detail}"


@dataclass(frozen=True, slots=True)
class TaskDeclaration:
    """A validated task entry from docker/tasks.json."""

    name: str
    stage: str
    network_policy: str
    source_access: str
    outputs: tuple[str, ...]
    architectures: tuple[str, ...]
    entry_point: tuple[str, ...]
    description: str


@dataclass(frozen=True, slots=True)
class TaskSet:
    """Validated task declarations and their lock digest."""

    digest: str
    tasks: dict[str, TaskDeclaration]


def _string(value: object, field: str) -> str:
    if not isinstance(value, str) or not value:
        raise TaskManifestError(f"{field} must be a non-empty string")
    return value


def _mapping(value: object, field: str) -> dict[str, object]:
    if not isinstance(value, dict) or not all(isinstance(key, str) for key in value):
        raise TaskManifestError(f"{field} must be an object")
    return value


def _string_array(value: object, field: str) -> tuple[str, ...]:
    if not isinstance(value, list) or not all(isinstance(item, str) and item for item in value):
        raise TaskManifestError(f"{field} must be a non-empty array of strings")
    return tuple(value)


def _reject_path_traversal(path: str, field: str) -> str:
    name = PurePosixPath(path)
    if name.is_absolute() or ".." in name.parts or not name.parts:
        raise TaskManifestError(f"{field} contains a path traversal or absolute path: {path}")
    return path


def _outputs(value: object, field: str) -> tuple[str, ...]:
    raw = _string_array(value, field)
    return tuple(_reject_path_traversal(item, f"{field}[{item}]") for item in raw)


def load_tasks(path: Path) -> TaskSet:
    """Load and validate the declarative task allowlist."""
    try:
        raw = path.read_bytes()
        document = json.loads(raw)
    except (OSError, json.JSONDecodeError) as error:
        raise TaskManifestError(str(error)) from error
    root = _mapping(document, "root")
    if root.get("schema") != 1:
        raise TaskManifestError("schema must equal 1")
    tasks_raw = _mapping(root.get("tasks"), "tasks")
    declarations: dict[str, TaskDeclaration] = {}
    for name, record in tasks_raw.items():
        entry = _mapping(record, f"tasks.{name}")
        stage = _string(entry.get("stage"), f"tasks.{name}.stage")
        if stage not in _REQUIRED_STAGES:
            raise TaskManifestError(f"tasks.{name}.stage is not an allowed stage: {stage}")
        network_policy = _string(entry.get("networkPolicy"), f"tasks.{name}.networkPolicy")
        if network_policy not in _VALID_NETWORK_POLICIES:
            raise TaskManifestError(f"tasks.{name}.networkPolicy is not allowed: {network_policy}")
        source_access = _string(entry.get("sourceAccess"), f"tasks.{name}.sourceAccess")
        if source_access not in _VALID_SOURCE_ACCESS:
            raise TaskManifestError(f"tasks.{name}.sourceAccess is not allowed: {source_access}")
        architectures = _string_array(entry.get("architectures"), f"tasks.{name}.architectures")
        if not set(architectures) <= _VALID_ARCHITECTURES:
            raise TaskManifestError(f"tasks.{name}.architectures contains an unsupported value")
        declarations[name] = TaskDeclaration(
            name=name,
            stage=stage,
            network_policy=network_policy,
            source_access=source_access,
            outputs=_outputs(entry.get("outputs"), f"tasks.{name}.outputs"),
            architectures=architectures,
            entry_point=_string_array(entry.get("entryPoint"), f"tasks.{name}.entryPoint"),
            description=_string(entry.get("description"), f"tasks.{name}.description"),
        )
    missing = _REQUIRED_TASKS - set(declarations)
    if missing:
        raise TaskManifestError(f"tasks must declare every required task: {sorted(missing)}")
    return TaskSet(digest=hashlib.sha256(raw).hexdigest(), tasks=declarations)


def get_task(tasks: TaskSet, name: str) -> TaskDeclaration:
    """Return a task declaration or raise for an undeclared task."""
    try:
        return tasks.tasks[name]
    except KeyError as error:
        raise TaskManifestError(f"undeclared task: {name}") from error
