from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from typing import final, override

from task_model import (
    TaskDeclarationError,
    TaskManifestError,
    TaskSet,
    get_task,
    load_tasks,
)


REPOSITORY = Path(__file__).resolve().parents[2]
VALID_TASKS = REPOSITORY / "docker" / "tasks.json"


def _write_tasks(directory: Path, document: dict) -> Path:
    path = directory / "tasks.json"
    path.write_text(json.dumps(document), encoding="utf-8")
    return path


@final
class TaskDeclarationTests(unittest.TestCase):
    @override
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    @override
    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_loads_valid_task_set_with_all_required_tasks(self) -> None:
        tasks = load_tasks(VALID_TASKS)
        self.assertIsInstance(tasks, TaskSet)
        for name in ("portable", "linux-native", "macos-native", "windows-native"):
            self.assertIn(name, tasks.tasks)

    def test_rejects_missing_schema_field(self) -> None:
        path = _write_tasks(self.root, {"tasks": {}})
        with self.assertRaisesRegex(TaskManifestError, "schema must equal 1"):
            load_tasks(path)

    def test_rejects_undeclared_task_lookup(self) -> None:
        tasks = load_tasks(VALID_TASKS)
        with self.assertRaisesRegex(TaskManifestError, "undeclared task"):
            get_task(tasks, "nonexistent-task")

    def test_rejects_path_traversal_in_outputs(self) -> None:
        document = json.loads(VALID_TASKS.read_text(encoding="utf-8"))
        document["tasks"]["portable"]["outputs"] = ["../escape"]
        path = _write_tasks(self.root, document)
        with self.assertRaisesRegex(TaskManifestError, "path traversal"):
            load_tasks(path)

    def test_rejects_absolute_path_in_outputs(self) -> None:
        document = json.loads(VALID_TASKS.read_text(encoding="utf-8"))
        document["tasks"]["portable"]["outputs"] = ["/etc/passwd"]
        path = _write_tasks(self.root, document)
        with self.assertRaisesRegex(TaskManifestError, "absolute path"):
            load_tasks(path)

    def test_rejects_missing_required_task(self) -> None:
        document = json.loads(VALID_TASKS.read_text(encoding="utf-8"))
        del document["tasks"]["windows-native"]
        path = _write_tasks(self.root, document)
        with self.assertRaisesRegex(TaskManifestError, "required task"):
            load_tasks(path)

    def test_rejects_invalid_network_policy(self) -> None:
        document = json.loads(VALID_TASKS.read_text(encoding="utf-8"))
        document["tasks"]["portable"]["networkPolicy"] = "unrestricted"
        path = _write_tasks(self.root, document)
        with self.assertRaisesRegex(TaskManifestError, "networkPolicy is not allowed"):
            load_tasks(path)

    def test_rejects_invalid_stage_name(self) -> None:
        document = json.loads(VALID_TASKS.read_text(encoding="utf-8"))
        document["tasks"]["portable"]["stage"] = "malicious-stage"
        path = _write_tasks(self.root, document)
        with self.assertRaisesRegex(TaskManifestError, "not an allowed stage"):
            load_tasks(path)

    def test_rejects_unsupported_architecture(self) -> None:
        document = json.loads(VALID_TASKS.read_text(encoding="utf-8"))
        document["tasks"]["portable"]["architectures"].append("riscv64")
        path = _write_tasks(self.root, document)
        with self.assertRaisesRegex(TaskManifestError, "unsupported value"):
            load_tasks(path)

    def test_task_set_digest_is_deterministic(self) -> None:
        first = load_tasks(VALID_TASKS).digest
        second = load_tasks(VALID_TASKS).digest
        self.assertEqual(first, second)
        self.assertEqual(len(first), 64)


if __name__ == "__main__":
    unittest.main()
