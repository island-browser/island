#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
build_dir="${TMPDIR:-/tmp}/island-icon-pipeline-build"
mkdir -p "$build_dir"
"${CXX:-c++}" -std=c++20 -Wall -Wextra -Werror -pedantic \
  -Wno-deprecated-declarations -Wno-missing-field-initializers -Wno-sign-compare \
  "$repo_root/tools/icon_pipeline/icon_pipeline.cc" \
  "$repo_root/tools/icon_pipeline/sha256.cc" \
  -o "$build_dir/icon_pipeline"
exec "$build_dir/icon_pipeline" "$1" "$repo_root"
