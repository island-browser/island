# Island Browser — Phase 0 (Setup) Implementation Plan

> Historical document. The current implemented browser surface is documented in
> `docs/superpowers/specs/2026-08-08-island-browser-phase1-design.md` and
> `docs/superpowers/plans/2026-08-08-island-browser-phase1.md`.

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Stand up the Island repository skeleton — dependency-fetch tooling, a CMake build that fails clearly when CEF isn't present, a GoogleTest harness, and a minimal CEF app that opens an empty `cef_views` window on macOS and closes without crashing.

**Architecture:** A CMake superbuild rooted at the repo root. `scripts/setup_deps.sh` vendors the two binary dependencies (CEF, Geist fonts) outside of git into `third_party/cef` and `assets/fonts`. `tests/` is an independent CMake target using `FetchContent(googletest)` — it builds and runs with zero CEF dependency, so it's the first fully-automatable checkpoint. `src/main/` is the actual CEF application: a `CefApp`/`CefBrowserProcessHandler` that creates one top-level `CefWindow` via a `CefWindowDelegate`, no browser view yet (that's Phase 1). Root `CMakeLists.txt` gates `src/main` behind an existence check on `third_party/cef` so a dev who hasn't run the setup script gets one clear error instead of a wall of missing-header failures.

**Tech Stack:** CEF (`cef_views` C++ API), CMake ≥ 3.21, C++20, GoogleTest (via FetchContent), bash (setup script).

> Implementation note (2026-08-08): this historical plan was executed with a newer dependency
> implementation than the sketch below. The checked-in source uses `deps/dependencies.lock.json` to
> pin CEF `150.0.14+g7c1aa68+chromium-150.0.7871.129` across six desktop targets and Geist `1.7.2`
> from an immutable commit archive. Treat embedded CEF 131 and mutable Geist `main` snippets as
> superseded planning detail, not current commands.

## Global Constraints

- C++20, explicit `std::` qualification everywhere, `using namespace std;` is forbidden.
- CMake: modern `target_*` commands only, no global `include_directories`/`link_libraries`.
- `.clang-format`: Google base style, 4-space indent, 100-column limit, `PointerAlignment: Left`.
- CEF binary distribution and Geist/Geist Mono fonts are **not** committed to git — they live in `third_party/cef/` and `assets/fonts/`, both gitignored, populated only by `scripts/setup_deps.sh`.
- Primary development/build platform for Phase 0 is macOS; CMake structure stays platform-aware (no hardcoded macOS-only assumptions outside code that is explicitly guarded), but only the macOS code path is implemented and verified in this phase.
- `CefSettings.remote_debugging_port` must be set in Phase 0's CEF initialization (Phase 9 groundwork) — no other agentic/CDP work in this phase.
- Tests use GoogleTest, discovered via `gtest_discover_tests` and run with `ctest`.

---

### Task 1: Repo scaffolding and dependency-fetch script

**Files:**
- Create: `.clang-format`
- Create: `.gitignore`
- Create: `scripts/setup_deps.sh`

**Interfaces:**
- Produces: `scripts/setup_deps.sh`, invoked as `./scripts/setup_deps.sh [--dry-run] [--force]`. On success (non-dry-run), guarantees `third_party/cef/cmake/cef_macros.cmake` exists and `assets/fonts/` contains at least one `*.ttf` file. Later tasks' CMake code depends on `third_party/cef/cmake/cef_macros.cmake` existing as the CEF-present signal.

- [ ] **Step 1: Create `.clang-format`**

```yaml
BasedOnStyle: Google
Language: Cpp
Standard: c++20
ColumnLimit: 100
IndentWidth: 4
AccessModifierOffset: -2
PointerAlignment: Left
DerivePointerAlignment: false
SortIncludes: true
```

- [ ] **Step 2: Create `.gitignore`**

```
/build/
/build-*/
/third_party/cef/
/assets/fonts/
.DS_Store
*.o
*.obj
compile_commands.json
.cache/
```

- [ ] **Step 3: Create `scripts/setup_deps.sh`**

```bash
#!/usr/bin/env bash
set -euo pipefail

# Downloads vendored, binary-only dependencies that are too large (or the
# wrong license shape) to commit to this repository: the CEF binary
# distribution and the Geist / Geist Mono font families. Safe to re-run;
# skips work that is already done unless --force is passed.

SCRIPT_DIR="$(cd "$(dirname "${BASH_SOURCE[0]}")" && pwd)"
ROOT_DIR="$(cd "${SCRIPT_DIR}/.." && pwd)"
THIRD_PARTY_DIR="${ROOT_DIR}/third_party"
FONTS_DIR="${ROOT_DIR}/assets/fonts"
CEF_DIR="${THIRD_PARTY_DIR}/cef"

CEF_VERSION="131.3.1+g4b5e926+chromium-131.0.6778.109"
GEIST_FONT_REF="main"

DRY_RUN=0
FORCE=0

for arg in "$@"; do
  case "${arg}" in
    --dry-run) DRY_RUN=1 ;;
    --force) FORCE=1 ;;
    *)
      echo "Unknown argument: ${arg}" >&2
      echo "Usage: $0 [--dry-run] [--force]" >&2
      exit 1
      ;;
  esac
done

detect_cef_platform() {
  local os
  local arch
  os="$(uname -s)"
  arch="$(uname -m)"

  case "${os}" in
    Darwin)
      case "${arch}" in
        arm64) echo "macosarm64" ;;
        x86_64) echo "macosx64" ;;
        *)
          echo "Unsupported macOS architecture: ${arch}" >&2
          exit 1
          ;;
      esac
      ;;
    Linux)
      case "${arch}" in
        x86_64) echo "linux64" ;;
        aarch64) echo "linuxarm64" ;;
        *)
          echo "Unsupported Linux architecture: ${arch}" >&2
          exit 1
          ;;
      esac
      ;;
    MINGW*|MSYS*|CYGWIN*)
      echo "windows64"
      ;;
    *)
      echo "Unsupported OS: ${os}" >&2
      exit 1
      ;;
  esac
}

cef_download_url() {
  local platform="$1"
  local encoded_version="${CEF_VERSION// /%20}"
  encoded_version="${encoded_version//+/%2B}"
  echo "https://cef-builds.spotifycdn.com/cef_binary_${encoded_version}_${platform}.tar.bz2"
}

fetch_cef() {
  local platform
  platform="$(detect_cef_platform)"
  local url
  url="$(cef_download_url "${platform}")"

  if [[ -d "${CEF_DIR}" && "${FORCE}" -eq 0 ]]; then
    echo "CEF already present at ${CEF_DIR} (use --force to re-fetch). Skipping."
    return
  fi

  echo "CEF platform:  ${platform}"
  echo "CEF version:   ${CEF_VERSION}"
  echo "CEF URL:       ${url}"

  if [[ "${DRY_RUN}" -eq 1 ]]; then
    return
  fi

  mkdir -p "${THIRD_PARTY_DIR}"
  local archive="${THIRD_PARTY_DIR}/cef_binary.tar.bz2"
  local extract_dir="${THIRD_PARTY_DIR}/.cef_extract"

  rm -rf "${CEF_DIR}" "${extract_dir}" "${archive}"
  curl --fail --location --progress-bar --output "${archive}" "${url}"

  mkdir -p "${extract_dir}"
  tar -xjf "${archive}" -C "${extract_dir}"

  local extracted_root
  extracted_root="$(find "${extract_dir}" -mindepth 1 -maxdepth 1 -type d | head -n 1)"
  if [[ -z "${extracted_root}" ]]; then
    echo "Failed to locate extracted CEF directory inside ${extract_dir}" >&2
    exit 1
  fi

  mv "${extracted_root}" "${CEF_DIR}"
  rm -rf "${extract_dir}" "${archive}"
  echo "CEF binary distribution installed at ${CEF_DIR}"
}

fetch_fonts() {
  if [[ -d "${FONTS_DIR}" ]] \
     && find "${FONTS_DIR}" -maxdepth 1 -name '*.ttf' -print -quit | grep -q . \
     && [[ "${FORCE}" -eq 0 ]]; then
    echo "Fonts already present at ${FONTS_DIR} (use --force to re-fetch). Skipping."
    return
  fi

  local url="https://github.com/vercel/geist-font/archive/refs/heads/${GEIST_FONT_REF}.tar.gz"
  echo "Geist font source: ${url}"

  if [[ "${DRY_RUN}" -eq 1 ]]; then
    return
  fi

  mkdir -p "${FONTS_DIR}" "${THIRD_PARTY_DIR}"
  local archive="${THIRD_PARTY_DIR}/geist-font.tar.gz"
  local extract_dir="${THIRD_PARTY_DIR}/.geist_extract"

  rm -rf "${extract_dir}" "${archive}"
  curl --fail --location --progress-bar --output "${archive}" "${url}"

  mkdir -p "${extract_dir}"
  tar -xzf "${archive}" -C "${extract_dir}"

  find "${extract_dir}" -type f \( -iname "Geist*.ttf" -o -iname "GeistMono*.ttf" \) \
    -exec cp {} "${FONTS_DIR}/" \;

  local copied
  copied="$(find "${FONTS_DIR}" -maxdepth 1 -type f -name '*.ttf' | wc -l | tr -d ' ')"
  if [[ "${copied}" -eq 0 ]]; then
    echo "No Geist .ttf files found in downloaded archive; font layout may have changed upstream." >&2
    exit 1
  fi

  rm -rf "${extract_dir}" "${archive}"
  echo "Copied ${copied} font file(s) to ${FONTS_DIR}"
}

fetch_cef
fetch_fonts

echo "Dependencies ready."
```

- [ ] **Step 4: Make the script executable**

Run: `chmod +x scripts/setup_deps.sh`

- [ ] **Step 5: Syntax-check the script**

Run: `bash -n scripts/setup_deps.sh`
Expected: no output, exit code 0.

- [ ] **Step 6: Dry-run the script and verify resolved URLs**

Run: `./scripts/setup_deps.sh --dry-run`
Expected output contains a line `CEF URL:       https://cef-builds.spotifycdn.com/cef_binary_131.3.1%2Bg4b5e926%2Bchromium-131.0.6778.109_macosarm64.tar.bz2` (or `_macosx64.tar.bz2` on Intel Mac) and a line `Geist font source: https://github.com/vercel/geist-font/archive/refs/heads/main.tar.gz`. No files should be created under `third_party/` or `assets/` (dry-run makes no network calls).

- [ ] **Step 7: Commit**

```bash
git add .clang-format .gitignore scripts/setup_deps.sh
git commit -m "Add repo scaffolding and dependency-fetch script"
```

---

### Task 2: CMake root project and GoogleTest smoke test

**Files:**
- Create: `CMakeLists.txt`
- Create: `tests/CMakeLists.txt`
- Create: `tests/smoke_test.cpp`

**Interfaces:**
- Consumes: nothing from Task 1 at configure time (this target has no CEF dependency).
- Produces: root `CMakeLists.txt` defines project `island_browser`, C++20, calls `enable_testing()` and `add_subdirectory(tests)`. Task 3 will append a CEF-gated `add_subdirectory(src/main)` block to this same file. Test target name: `island_tests`.

- [ ] **Step 1: Create root `CMakeLists.txt`**

```cmake
cmake_minimum_required(VERSION 3.21)
project(island_browser LANGUAGES CXX)

set(CMAKE_CXX_STANDARD 20)
set(CMAKE_CXX_STANDARD_REQUIRED ON)
set(CMAKE_EXPORT_COMPILE_COMMANDS ON)

enable_testing()

add_subdirectory(tests)
```

- [ ] **Step 2: Create `tests/smoke_test.cpp`**

```cpp
#include <gtest/gtest.h>

TEST(Smoke, TestInfrastructureBuildsAndRuns) {
  EXPECT_TRUE(true);
}
```

- [ ] **Step 3: Create `tests/CMakeLists.txt`**

```cmake
include(FetchContent)

set(INSTALL_GTEST OFF CACHE BOOL "" FORCE)
set(gtest_force_shared_crt ON CACHE BOOL "" FORCE)

FetchContent_Declare(
  googletest
  URL https://github.com/google/googletest/archive/refs/tags/v1.15.2.tar.gz
)
FetchContent_MakeAvailable(googletest)

add_executable(island_tests
  smoke_test.cpp
)

target_link_libraries(island_tests PRIVATE GTest::gtest_main)

include(GoogleTest)
gtest_discover_tests(island_tests)
```

- [ ] **Step 4: Configure and build**

Run: `cmake -B build && cmake --build build`
Expected: configure and build succeed; GoogleTest is fetched during configure (requires network access).

- [ ] **Step 5: Run the test suite**

Run: `ctest --test-dir build --output-on-failure`
Expected: `1 test from 1 test suite ran`, `Smoke.TestInfrastructureBuildsAndRuns` passes, exit code 0.

- [ ] **Step 6: Commit**

```bash
git add CMakeLists.txt tests/CMakeLists.txt tests/smoke_test.cpp
git commit -m "Add CMake root project and GoogleTest smoke test"
```

---

### Task 3: CEF app process and empty cef_views window (macOS)

**Prerequisite:** Run `./scripts/setup_deps.sh` (no `--dry-run`) before starting this task — `third_party/cef` must be populated, since this task's build step cannot be automated without it.

**Files:**
- Modify: `CMakeLists.txt` (append CEF-gated `add_subdirectory(src/main)`)
- Create: `src/main/CMakeLists.txt`
- Create: `src/main/main_mac.mm`
- Create: `src/main/Info.plist.in`

**Interfaces:**
- Consumes: `third_party/cef/cmake/cef_macros.cmake`, `third_party/cef/libcef_dll` (both ship inside the CEF binary distribution fetched by Task 1's script) — these provide the `find_package(CEF REQUIRED)` module, the `CEF_STANDARD_LIBS` variable, and the `SET_EXECUTABLE_TARGET_PROPERTIES()` macro used below.
- Produces: executable target `island_browser` (a `.app` bundle on macOS).

- [ ] **Step 1: Append CEF-gated subdirectory to root `CMakeLists.txt`**

Modify `CMakeLists.txt`, adding after `add_subdirectory(tests)`:

```cmake

set(CEF_ROOT "${CMAKE_SOURCE_DIR}/third_party/cef" CACHE PATH "Path to the CEF binary distribution")

if(NOT EXISTS "${CEF_ROOT}/cmake/cef_macros.cmake")
  message(FATAL_ERROR
    "CEF binary distribution not found at ${CEF_ROOT}.\n"
    "Run scripts/setup_deps.sh before configuring this project.")
endif()

add_subdirectory(src/main)
```

- [ ] **Step 2: Verify the guard fires when CEF is absent**

Run: `rm -rf build && cmake -B build -S . 2>&1 | tail -n 5` after temporarily renaming `third_party/cef` out of the way (`mv third_party/cef third_party/cef.bak`).
Expected: configure fails with `CMake Error ... CEF binary distribution not found at .../third_party/cef.`
Then restore it: `mv third_party/cef.bak third_party/cef`.

- [ ] **Step 3: Create `src/main/Info.plist.in`**

```xml
<?xml version="1.0" encoding="UTF-8"?>
<!DOCTYPE plist PUBLIC "-//Apple//DTD PLIST 1.0//EN" "http://www.apple.com/DTDs/PropertyList-1.0.dtd">
<plist version="1.0">
<dict>
  <key>CFBundleDevelopmentRegion</key>
  <string>en</string>
  <key>CFBundleExecutable</key>
  <string>${MACOSX_BUNDLE_EXECUTABLE_NAME}</string>
  <key>CFBundleIdentifier</key>
  <string>${MACOSX_BUNDLE_GUI_IDENTIFIER}</string>
  <key>CFBundleInfoDictionaryVersion</key>
  <string>6.0</string>
  <key>CFBundleName</key>
  <string>${MACOSX_BUNDLE_BUNDLE_NAME}</string>
  <key>CFBundlePackageType</key>
  <string>APPL</string>
  <key>CFBundleShortVersionString</key>
  <string>0.1.0</string>
  <key>CFBundleVersion</key>
  <string>0.1.0</string>
  <key>NSHighResolutionCapable</key>
  <true/>
  <key>NSSupportsAutomaticGraphicsSwitching</key>
  <true/>
</dict>
</plist>
```

- [ ] **Step 4: Create `src/main/main_mac.mm`**

```objc
#import <Cocoa/Cocoa.h>

#include "include/cef_app.h"
#include "include/cef_command_line.h"
#include "include/views/cef_window.h"
#include "include/wrapper/cef_helpers.h"
#include "include/wrapper/cef_library_loader.h"

namespace island {

class EmptyWindowDelegate : public CefWindowDelegate {
 public:
  EmptyWindowDelegate() = default;

  void OnWindowCreated(CefRefPtr<CefWindow> window) override {
    CEF_REQUIRE_UI_THREAD();
    window->SetTitle("Island");
    window->CenterWindow(CefSize(1024, 768));
    window->Show();
  }

  bool CanClose(CefRefPtr<CefWindow> window) override {
    CEF_REQUIRE_UI_THREAD();
    return true;
  }

  CefSize GetPreferredSize(CefRefPtr<CefView> view) override {
    return CefSize(1024, 768);
  }

 private:
  IMPLEMENT_REFCOUNTING(EmptyWindowDelegate);
  DISALLOW_COPY_AND_ASSIGN(EmptyWindowDelegate);
};

class IslandApp : public CefApp, public CefBrowserProcessHandler {
 public:
  IslandApp() = default;

  CefRefPtr<CefBrowserProcessHandler> GetBrowserProcessHandler() override {
    return this;
  }

  void OnContextInitialized() override {
    CEF_REQUIRE_UI_THREAD();
    CefWindow::CreateTopLevelWindow(new EmptyWindowDelegate());
  }

 private:
  IMPLEMENT_REFCOUNTING(IslandApp);
  DISALLOW_COPY_AND_ASSIGN(IslandApp);
};

}  // namespace island

int main(int argc, char* argv[]) {
  CefScopedLibraryLoader library_loader;
  if (!library_loader.LoadInMain()) {
    return 1;
  }

  CefMainArgs main_args(argc, argv);
  CefRefPtr<island::IslandApp> app(new island::IslandApp());

  int exit_code = CefExecuteProcess(main_args, app, nullptr);
  if (exit_code >= 0) {
    return exit_code;
  }

  [NSApplication sharedApplication];

  CefSettings settings;
  settings.remote_debugging_port = 9222;
  settings.no_sandbox = true;

  CefInitialize(main_args, settings, app, nullptr);
  CefRunMessageLoop();
  CefShutdown();

  return 0;
}
```

- [ ] **Step 5: Create `src/main/CMakeLists.txt`**

```cmake
list(APPEND CMAKE_MODULE_PATH "${CEF_ROOT}/cmake")
find_package(CEF REQUIRED)

add_subdirectory("${CEF_ROOT}/libcef_dll" libcef_dll_wrapper)

add_executable(island_browser MACOSX_BUNDLE main_mac.mm)

target_link_libraries(island_browser PRIVATE libcef_dll_wrapper ${CEF_STANDARD_LIBS})

set_target_properties(island_browser PROPERTIES
  MACOSX_BUNDLE_INFO_PLIST "${CMAKE_CURRENT_SOURCE_DIR}/Info.plist.in"
  MACOSX_BUNDLE_BUNDLE_NAME "Island"
  MACOSX_BUNDLE_GUI_IDENTIFIER "dev.island.browser"
)

SET_EXECUTABLE_TARGET_PROPERTIES(island_browser)
```

- [ ] **Step 6: Configure and build**

Run: `rm -rf build && cmake -B build -S . && cmake --build build`
Expected: configure succeeds (CEF found), build produces `build/src/main/island_browser.app`.

If this step fails with errors about missing macros, targets, or bundle helper processes: open `third_party/cef/tests/cefsimple/CMakeLists.txt` and `third_party/cef/tests/cefsimple/main_mac.mm` (both ship inside the vendored CEF distribution for this exact version) and diff against the files above — that sample is CEF's own authoritative reference for this integration and takes precedence over this plan if the two disagree.

- [ ] **Step 7: Run and manually verify the empty window**

Run: `open build/src/main/island_browser.app`
Expected: an empty, titled ("Island") 1024x768 window appears, centered on screen, with no crash. Quit the app via Cmd+Q or the window's close button.
Then check for stray processes: `ps aux | grep island_browser | grep -v grep`
Expected: no output (all CEF helper processes exited cleanly after quit).

- [ ] **Step 8: Run ctest once more to confirm nothing regressed**

Run: `ctest --test-dir build --output-on-failure`
Expected: still 1 passing test (Task 2's smoke test is unaffected by this task).

- [ ] **Step 9: Commit**

```bash
git add CMakeLists.txt src/main/CMakeLists.txt src/main/main_mac.mm src/main/Info.plist.in
git commit -m "Add CEF app process and empty cef_views window on macOS"
```

---

## Phase 0 done-check

- [ ] `./scripts/setup_deps.sh` populates `third_party/cef` and `assets/fonts` from a clean checkout.
- [ ] `cmake -B build && cmake --build build` succeeds end to end.
- [ ] `ctest --test-dir build` passes (1 test).
- [ ] `island_browser.app` opens an empty window and quits cleanly with no leftover processes.
