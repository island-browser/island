#!/usr/bin/env bash
# In-container Linux product build for the island.py "build linux" lane.
#
# Runs inside the locked linux-build image as a non-root user. The repository
# is mounted read-only at /src; this script copies a build subset into the
# writable /work volume, installs CEF/Geist from the deps lock (the only step
# with network access), configures and builds with Ninja, runs CTest under
# Xvfb, and exports logs to /work/out for the host wrapper to collect.
#
# Target resolution: ISLAND_TARGET env (set by the router) or --target <t>.
set -euo pipefail

target="${ISLAND_TARGET:-}"
if [[ -z "$target" && "${1:-}" == "--target" ]]; then
    target="${2:-}"
fi
if [[ -z "$target" ]]; then
    echo "error: no build target (set ISLAND_TARGET or pass --target <linux64|linuxarm64>)" >&2
    exit 4
fi
case "$target" in
    linux64 | linuxarm64) ;;
    *)
        echo "error: unsupported container build target: $target" >&2
        exit 2
        ;;
esac

out=/work/out
mkdir -p "$out"

# Copy the source subset needed to configure and build. /src is read-only; the
# build tree, dependency installations, and test runs all happen under /work.
work_src=/work/src
rm -rf "$work_src"
mkdir -p "$work_src"
tar -C /src -cf - \
    --exclude=./.git \
    --exclude='./build' \
    --exclude='./build-*' \
    --exclude='./dist-*' \
    --exclude=./third_party \
    --exclude=./assets/fonts \
    --exclude='./.omc' \
    --exclude='./.omo' \
    --exclude='./.playwright-mcp' \
    --exclude='./site/.omc' \
    --exclude='__pycache__' \
    . | tar -C "$work_src" -xf -

python3 "$work_src/scripts/deps.py" install --root "$work_src" --target "$target" \
    2>&1 | tee "$out/deps-install.log"

cmake -S "$work_src" -B "$work_src/build" -G Ninja 2>&1 | tee "$out/configure.log"
cmake --build "$work_src/build" 2>&1 | tee "$out/build.log"

# island_cef_tests calls CefInitialize, which needs an X11 display even for
# tests that never open a visible window (mirrors the native Linux CI job).
xvfb-run -a ctest --test-dir "$work_src/build" --output-on-failure --no-tests=error \
    2>&1 | tee "$out/test.log"

( cd "$out" && sha256sum deps-install.log configure.log build.log test.log > SHA256SUMS.txt )
echo "container build complete: target=$target"
