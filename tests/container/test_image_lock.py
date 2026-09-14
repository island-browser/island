from __future__ import annotations

import json
import tempfile
import unittest
from pathlib import Path
from typing import final, override

from image_lock import (
    ImageLock,
    ImageLockError,
    ManifestError,
    load_image_lock,
    resolve_digest_for_architecture,
)


REPOSITORY = Path(__file__).resolve().parents[2]
VALID_LOCK = REPOSITORY / "docker" / "images.lock.json"


def _write_lock(directory: Path, document: dict) -> Path:
    path = directory / "images.lock.json"
    path.write_text(json.dumps(document), encoding="utf-8")
    return path


@final
class ImageLockTests(unittest.TestCase):
    @override
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)

    @override
    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_loads_valid_image_lock_with_all_required_stages(self) -> None:
        lock = load_image_lock(VALID_LOCK)
        self.assertIsInstance(lock, ImageLock)
        self.assertIn("ubuntu-base", lock.images)
        self.assertIn("portable-tests", lock.images)
        self.assertIn("linux-build", lock.images)
        self.assertIn("dev", lock.images)

    def test_rejects_missing_schema_field(self) -> None:
        path = _write_lock(self.root, {"images": {}})
        with self.assertRaisesRegex(ManifestError, "schema must equal 1"):
            load_image_lock(path)

    def test_rejects_missing_required_stage(self) -> None:
        document = json.loads(VALID_LOCK.read_text(encoding="utf-8"))
        del document["images"]["dev"]
        path = _write_lock(self.root, document)
        with self.assertRaisesRegex(ManifestError, "required stage"):
            load_image_lock(path)

    def test_rejects_floating_digest_without_sha256_prefix(self) -> None:
        document = json.loads(VALID_LOCK.read_text(encoding="utf-8"))
        document["images"]["ubuntu-base"]["manifestDigest"] = "floating-tag"
        path = _write_lock(self.root, document)
        with self.assertRaisesRegex(ManifestError, "must start with sha256:"):
            load_image_lock(path)

    def test_rejects_digest_with_invalid_hex(self) -> None:
        document = json.loads(VALID_LOCK.read_text(encoding="utf-8"))
        document["images"]["ubuntu-base"]["manifestDigest"] = "sha256:" + "z" * 64
        path = _write_lock(self.root, document)
        with self.assertRaisesRegex(ManifestError, "sha256 digest"):
            load_image_lock(path)

    def test_rejects_unsupported_architecture(self) -> None:
        document = json.loads(VALID_LOCK.read_text(encoding="utf-8"))
        document["images"]["ubuntu-base"]["architectures"].append("linux/riscv64")
        path = _write_lock(self.root, document)
        with self.assertRaisesRegex(ManifestError, "unsupported architecture"):
            load_image_lock(path)

    def test_resolve_digest_for_architecture_returns_arch_specific_digest(self) -> None:
        lock = load_image_lock(VALID_LOCK)
        amd64 = resolve_digest_for_architecture(lock, "linux-build", "amd64")
        arm64 = resolve_digest_for_architecture(lock, "linux-build", "arm64")
        self.assertNotEqual(amd64, arm64)
        self.assertTrue(amd64.startswith("sha256:"))
        self.assertTrue(arm64.startswith("sha256:"))

    def test_resolve_digest_falls_back_to_manifest_digest_for_unknown_arch(self) -> None:
        lock = load_image_lock(VALID_LOCK)
        digest = resolve_digest_for_architecture(lock, "portable-tests", "riscv64")
        self.assertEqual(digest, lock.images["portable-tests"].manifest_digest)

    def test_lock_digest_is_deterministic(self) -> None:
        first = load_image_lock(VALID_LOCK).digest
        second = load_image_lock(VALID_LOCK).digest
        self.assertEqual(first, second)
        self.assertEqual(len(first), 64)

    def test_rejects_malformed_json(self) -> None:
        path = self.root / "broken.json"
        path.write_text("{", encoding="utf-8")
        with self.assertRaises(ManifestError):
            load_image_lock(path)


if __name__ == "__main__":
    unittest.main()
