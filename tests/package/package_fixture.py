from __future__ import annotations

import shutil
from dataclasses import dataclass
from pathlib import Path


@dataclass(frozen=True, slots=True)
class PackageFixture:
    build: Path
    repository: Path

    def stage(self, target: str, sandbox: bool = False) -> None:
        if target.startswith("macos"):
            root = self.build / "src/main/island_browser.app/Contents"
            self.write_binary(root / "MacOS/island_browser", target)
            self.write_binary(
                root / "Frameworks/Chromium Embedded Framework.framework/Chromium Embedded Framework",
                target,
            )
            for suffix in ("", " (Alerts)", " (GPU)", " (Plugin)", " (Renderer)"):
                self.write_binary(
                    root / f"Frameworks/island_browser Helper{suffix}.app/Contents/MacOS/island_browser Helper{suffix}",
                    target,
                )
            framework = root / "Frameworks/Chromium Embedded Framework.framework"
            for name in ("icudtl.dat", "resources.pak", "chrome_100_percent.pak", "chrome_200_percent.pak", "en.lproj/locale.pak"):
                self.write(framework / "Versions/A/Resources" / name)
            if not (framework / "Versions/Current").exists():
                _ = (framework / "Versions/Current").symlink_to("A")
            if not (framework / "Resources").exists():
                _ = (framework / "Resources").symlink_to("Versions/A/Resources")
            self.stage_chrome_resources(root / "Resources/island")
            return
        root = self.build / "src/main/Release"
        self.write_binary(root / ("island_browser.exe" if target.startswith("windows") else "island_browser"), target, sandbox)
        for name in ("icudtl.dat", "resources.pak", "chrome_100_percent.pak", "chrome_200_percent.pak", "locales/en-US.pak"):
            self.write(root / name)
        if target.startswith("windows"):
            for name in ("libcef.dll", "chrome_elf.dll", "d3dcompiler_47.dll", "dxcompiler.dll", "dxil.dll", "libEGL.dll", "libGLESv2.dll", "snapshot_blob.bin", "v8_context_snapshot.bin", "vk_swiftshader.dll", "vulkan-1.dll"):
                self.write_binary(root / name, target)
            self.write(root / "vk_swiftshader_icd.json")
            if sandbox:
                self.write_binary(root / "island_browser.dll", target)
        else:
            for name in ("chrome-sandbox", "libcef.so", "libEGL.so", "libGLESv2.so", "libvk_swiftshader.so", "libvulkan.so.1", "snapshot_blob.bin", "v8_context_snapshot.bin"):
                self.write_binary(root / name, target)
        self.stage_chrome_resources(root / "resources/island")

    def runtime_root(self, target: str) -> Path:
        return self.build / ("src/main/island_browser.app/Contents" if target.startswith("macos") else "src/main/Release")

    def stage_chrome_resources(self, destination: Path) -> None:
        shutil.copytree(self.repository / "resources/island/icons", destination / "icons", dirs_exist_ok=True)
        fonts = destination / "fonts"
        fonts.mkdir(parents=True, exist_ok=True)
        for source in (self.repository / "assets/fonts").iterdir():
            if source.is_file() and source.name != ".island-dependency-receipt.json":
                shutil.copy2(source, fonts / source.name)

    @staticmethod
    def write(path: Path) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        _ = path.write_text("fixture", encoding="utf-8")

    @staticmethod
    def write_binary(path: Path, target: str, bootstrap: bool = False) -> None:
        path.parent.mkdir(parents=True, exist_ok=True)
        match target:
            case "macosx64":
                data = b"\xcf\xfa\xed\xfe\x07\x00\x00\x01"
            case "macosarm64":
                data = b"\xcf\xfa\xed\xfe\x0c\x00\x00\x01"
            case "windows64" | "windowsarm64":
                # Real MSVC/Chromium PE binaries place the PE signature at
                # e_lfanew = 0x80 (the DOS stub plus Rich header), not at 0x40,
                # so the fixture must mirror that or it hides the 128-byte
                # truncation bug in package._architecture().
                machine = 0xAA64 if target.endswith("arm64") else 0x8664
                data = bytearray(512)
                data[0:2] = b"MZ"
                data[0x3C:0x40] = (0x80).to_bytes(4, "little")
                data[0x80:0x84] = b"PE\x00\x00"
                data[0x84:0x86] = machine.to_bytes(2, "little")
                data = bytes(data)
            case "linux64" | "linuxarm64":
                data = b"\x7fELF" + b"\x00" * 14 + (183 if target.endswith("arm64") else 62).to_bytes(2, "little")
            case unexpected:
                raise AssertionError(f"unexpected fixture target: {unexpected}")
        if bootstrap:
            data += b"island_browser.dll\x00"
        _ = path.write_bytes(data)
