from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from typing import final, override

from report_writer import Report, build_report, write_report
from task_model import get_task, load_tasks


REPOSITORY = Path(__file__).resolve().parents[2]
TASKS = REPOSITORY / "docker" / "tasks.json"


@final
class ReportWriterTests(unittest.TestCase):
    @override
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        self.task_set = load_tasks(TASKS)
        self.portable_task = get_task(self.task_set, "portable")

    @override
    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_build_report_has_schema_version_one(self) -> None:
        report = build_report(
            self.portable_task, "portable", "docker", False,
            "sha256:abc", REPOSITORY, 0, None,
        )
        self.assertEqual(report.schema, 1)

    def test_build_report_records_task_name_and_target(self) -> None:
        report = build_report(
            self.portable_task, "portable", "docker", False,
            "sha256:abc", REPOSITORY, 0, None,
        )
        self.assertEqual(report.task, "portable")
        self.assertEqual(report.target, "portable")
        self.assertEqual(report.mode, "docker")
        self.assertFalse(report.emulated)

    def test_build_report_native_mode_has_null_digest(self) -> None:
        report = build_report(
            self.portable_task, "portable", "native", False,
            None, REPOSITORY, 0, None,
        )
        self.assertIsNone(report.image_digest)

    def test_build_report_exit_code_is_preserved(self) -> None:
        report = build_report(
            self.portable_task, "portable", "docker", False,
            "sha256:abc", REPOSITORY, 6, None,
        )
        self.assertEqual(report.exit, 6)

    def test_write_report_produces_valid_json(self) -> None:
        report = build_report(
            self.portable_task, "portable", "docker", False,
            "sha256:abc", REPOSITORY, 0, None,
        )
        path = write_report(report, self.root / "reports")
        self.assertTrue(path.is_file())
        data = json.loads(path.read_text(encoding="utf-8"))
        self.assertEqual(data["schema"], 1)
        self.assertEqual(data["task"], "portable")
        self.assertEqual(data["exit"], 0)

    def test_write_report_redacts_home_paths(self) -> None:
        report = build_report(
            self.portable_task, "portable", "docker", False,
            "sha256:abc", REPOSITORY, 0, None,
        )
        path = write_report(report, self.root / "reports")
        content = path.read_text(encoding="utf-8")
        self.assertNotIn(str(Path.home()), content)

    def test_artifact_hashes_are_empty_when_no_artifact_dir(self) -> None:
        report = build_report(
            self.portable_task, "portable", "docker", False,
            "sha256:abc", REPOSITORY, 0, None,
        )
        self.assertEqual(report.artifact_hashes, {})

    def test_artifact_hashes_populated_when_dir_has_files(self) -> None:
        import hashlib
        artifact_dir = self.root / "artifacts"
        artifact_dir.mkdir()
        (artifact_dir / "test.txt").write_text("hello", encoding="utf-8")
        report = build_report(
            self.portable_task, "portable", "docker", False,
            "sha256:abc", REPOSITORY, 0, artifact_dir,
        )
        self.assertIn("test.txt", report.artifact_hashes)
        expected = hashlib.sha256(b"hello").hexdigest()
        self.assertEqual(report.artifact_hashes["test.txt"], expected)


if __name__ == "__main__":
    unittest.main()
