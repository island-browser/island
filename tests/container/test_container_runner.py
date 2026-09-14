from __future__ import annotations

import os
import tempfile
import unittest
from pathlib import Path
from typing import final, override
from unittest.mock import patch

from container_runner import (
    ContainerRunnerError,
    DockerInvocation,
    _FORBIDDEN_MOUNT_PATTERNS,
    _MOUNT_ALLOWLIST,
    dry_run_string,
    is_emulated,
    plan_invocation,
)
from image_lock import load_image_lock
from task_model import get_task, load_tasks


REPOSITORY = Path(__file__).resolve().parents[2]
IMAGE_LOCK = REPOSITORY / "docker" / "images.lock.json"
TASKS = REPOSITORY / "docker" / "tasks.json"


@final
class ContainerRunnerTests(unittest.TestCase):
    @override
    def setUp(self) -> None:
        self.temporary = tempfile.TemporaryDirectory()
        self.root = Path(self.temporary.name)
        (self.root / "src").mkdir()
        (self.root / "build" / "container-artifacts").mkdir(parents=True)
        self.lock = load_image_lock(IMAGE_LOCK)
        self.task_set = load_tasks(TASKS)
        self.portable_task = get_task(self.task_set, "portable")
        self.linux_task = get_task(self.task_set, "linux-native")

    @override
    def tearDown(self) -> None:
        self.temporary.cleanup()

    def test_plan_invocation_uses_network_none_for_portable_task(self) -> None:
        with patch("container_runner._docker_available", return_value=True):
            invocation = plan_invocation(self.portable_task, self.lock, "x64", self.root)
        self.assertIn("--network=none", invocation.arguments)
        self.assertEqual(invocation.network, "none")

    def test_plan_invocation_uses_default_network_for_dependency_fetch(self) -> None:
        with patch("container_runner._docker_available", return_value=True):
            invocation = plan_invocation(self.linux_task, self.lock, "x64", self.root)
        self.assertIn("--network=default", invocation.arguments)
        self.assertEqual(invocation.network, "default")

    def test_plan_invocation_mounts_source_read_only(self) -> None:
        with patch("container_runner._docker_available", return_value=True):
            invocation = plan_invocation(self.portable_task, self.lock, "x64", self.root)
        mount_args = [arg for arg in invocation.arguments if arg.startswith("--mount=")]
        self.assertTrue(any("readonly" in arg and "/src" in arg for arg in mount_args))

    def test_plan_invocation_uses_named_volumes_not_host_bind_for_caches(self) -> None:
        with patch("container_runner._docker_available", return_value=True):
            invocation = plan_invocation(self.portable_task, self.lock, "x64", self.root)
        mount_args = [arg for arg in invocation.arguments if arg.startswith("--mount=")]
        self.assertTrue(any("type=volume" in arg and "island-deps-cache" in arg for arg in mount_args))
        self.assertTrue(any("type=volume" in arg and "island-compiler-cache" in arg for arg in mount_args))

    def test_plan_invocation_maps_caller_uid_gid(self) -> None:
        with patch("container_runner._docker_available", return_value=True):
            invocation = plan_invocation(self.portable_task, self.lock, "x64", self.root)
        user_arg = [arg for arg in invocation.arguments if arg.startswith("--user=")]
        self.assertTrue(user_arg)
        self.assertIn(":", user_arg[0])

    def test_plan_invocation_refuses_when_docker_unavailable(self) -> None:
        with patch("container_runner._docker_available", return_value=False):
            with self.assertRaisesRegex(ContainerRunnerError, "docker is not available"):
                plan_invocation(self.portable_task, self.lock, "x64", self.root)

    def test_plan_invocation_refuses_forbidden_mount_in_extra_env(self) -> None:
        with patch("container_runner._docker_available", return_value=True):
            with patch.dict(os.environ, {"ISLAND_EXTRA_MOUNTS": str(Path.home() / ".ssh")}):
                with self.assertRaisesRegex(ContainerRunnerError, "refusing to mount forbidden"):
                    plan_invocation(self.portable_task, self.lock, "x64", self.root)

    def test_plan_invocation_refuses_unsupported_arch(self) -> None:
        with patch("container_runner._docker_available", return_value=True):
            with self.assertRaisesRegex(ContainerRunnerError, "unsupported architecture"):
                plan_invocation(self.portable_task, self.lock, "riscv64", self.root)

    def test_mount_allowlist_does_not_include_home_or_docker_socket(self) -> None:
        for entry in _MOUNT_ALLOWLIST:
            for forbidden in _FORBIDDEN_MOUNT_PATTERNS:
                self.assertNotIn(forbidden, entry.host_path)

    def test_mount_allowlist_src_is_read_only(self) -> None:
        src_entry = [e for e in _MOUNT_ALLOWLIST if e.container_path == "/src"]
        self.assertTrue(src_entry)
        self.assertTrue(src_entry[0].read_only)

    def test_dry_run_string_produces_a_docker_command(self) -> None:
        invocation = DockerInvocation(
            image="ubuntu@sha256:abc",
            digest="sha256:abc",
            arguments=["run", "--rm", "ubuntu@sha256:abc"],
            network="none",
            mounts=(),
            user="1000:1000",
            workdir="/work",
        )
        rendered = dry_run_string(invocation)
        self.assertTrue(rendered.startswith("docker run"))
        self.assertIn("ubuntu@sha256:abc", rendered)

    def test_is_emulated_detects_arch_mismatch(self) -> None:
        self.assertTrue(is_emulated("x64", "arm64"))
        self.assertFalse(is_emulated("arm64", "arm64"))

    def test_plan_invocation_uses_architecture_specific_digest(self) -> None:
        with patch("container_runner._docker_available", return_value=True):
            x64_invocation = plan_invocation(self.linux_task, self.lock, "x64", self.root)
            arm64_invocation = plan_invocation(self.linux_task, self.lock, "arm64", self.root)
        self.assertNotEqual(x64_invocation.digest, arm64_invocation.digest)
        self.assertIn(x64_invocation.digest, x64_invocation.image)
        self.assertIn(arm64_invocation.digest, arm64_invocation.image)

    def test_plan_invocation_normalizes_x64_to_amd64_digest(self) -> None:
        with patch("container_runner._docker_available", return_value=True):
            x64_invocation = plan_invocation(self.linux_task, self.lock, "x64", self.root)
            amd64_invocation = plan_invocation(self.linux_task, self.lock, "amd64", self.root)
        self.assertEqual(x64_invocation.digest, amd64_invocation.digest)

    def test_plan_invocation_passes_extra_args_after_entry_point(self) -> None:
        with patch("container_runner._docker_available", return_value=True):
            invocation = plan_invocation(
                self.linux_task, self.lock, "x64", self.root,
                extra_args=("--target", "linux64"),
            )
        image_index = invocation.arguments.index(invocation.image)
        after_image = invocation.arguments[image_index + 1:]
        self.assertEqual(tuple(after_image[:-2]), tuple(self.linux_task.entry_point))
        self.assertEqual(tuple(after_image[-2:]), ("--target", "linux64"))

    def test_plan_invocation_sets_env_vars(self) -> None:
        with patch("container_runner._docker_available", return_value=True):
            invocation = plan_invocation(
                self.linux_task, self.lock, "x64", self.root,
                env=(("ISLAND_TARGET", "linux64"),),
            )
        env_args = [arg for arg in invocation.arguments if arg.startswith("--env=")]
        self.assertTrue(any("ISLAND_TARGET=linux64" in arg for arg in env_args))


if __name__ == "__main__":
    unittest.main()
