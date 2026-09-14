from __future__ import annotations

import unittest
from pathlib import Path
from typing import final, override

import island


REPOSITORY = Path(__file__).resolve().parents[2]


@final
class RouterRoutingTests(unittest.TestCase):
    """Tests that the router routes native vs docker correctly."""

    @override
    def setUp(self) -> None:
        pass

    @override
    def tearDown(self) -> None:
        pass

    def test_resolve_host_maps_known_architectures(self) -> None:
        host = island._resolve_host()
        self.assertIn(host.arch_label, ("x64", "arm64", "unknown"))

    def test_host_arch_map_covers_x86_64_and_aarch64(self) -> None:
        self.assertEqual(island._HOST_ARCH_MAP["x86_64"], "x64")
        self.assertEqual(island._HOST_ARCH_MAP["aarch64"], "arm64")
        self.assertEqual(island._HOST_ARCH_MAP["amd64"], "x64")

    def test_native_target_mapping_covers_all_platforms(self) -> None:
        host_macos_x64 = island.HostInfo("Darwin", "x86_64", "x64")
        host_macos_arm = island.HostInfo("Darwin", "arm64", "arm64")
        host_win_x64 = island.HostInfo("Windows", "x86_64", "x64")
        host_win_arm = island.HostInfo("Windows", "arm64", "arm64")
        host_linux_x64 = island.HostInfo("Linux", "x86_64", "x64")
        host_linux_arm = island.HostInfo("Linux", "aarch64", "arm64")
        self.assertEqual(island._native_target(host_macos_x64), "macosx64")
        self.assertEqual(island._native_target(host_macos_arm), "macosarm64")
        self.assertEqual(island._native_target(host_win_x64), "windows64")
        self.assertEqual(island._native_target(host_win_arm), "windowsarm64")
        self.assertEqual(island._native_target(host_linux_x64), "linux64")
        self.assertEqual(island._native_target(host_linux_arm), "linuxarm64")

    def test_exit_codes_are_stable_and_documented(self) -> None:
        self.assertEqual(island.EXIT_SUCCESS, 0)
        self.assertEqual(island.EXIT_UNSUPPORTED, 2)
        self.assertEqual(island.EXIT_TOOL_MISSING, 3)
        self.assertEqual(island.EXIT_TASK_INVALID, 4)
        self.assertEqual(island.EXIT_IMAGE_LOCK_INVALID, 5)
        self.assertEqual(island.EXIT_CONTAINER_FAILED, 6)
        self.assertEqual(island.EXIT_REPORT_FAILED, 7)

    def test_linux_arch_map_covers_both_arches(self) -> None:
        self.assertEqual(island._LINUX_ARCH_MAP["x64"], "linux64")
        self.assertEqual(island._LINUX_ARCH_MAP["arm64"], "linuxarm64")


if __name__ == "__main__":
    unittest.main()
