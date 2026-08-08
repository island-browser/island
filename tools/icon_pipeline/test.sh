#!/bin/sh
set -eu

repo_root=$(CDPATH= cd -- "$(dirname -- "$0")/../.." && pwd)
pipeline="$repo_root/tools/icon_pipeline/run.sh"
source="$repo_root/resources/island/icons/source/chevron-left.svg"
png="$repo_root/resources/island/icons/png/chevron-left-text-13@1x.png"
lock="$repo_root/tools/icon_pipeline/icon-sources.lock.json"
source_backup=$(mktemp)
png_backup=$(mktemp)
lock_backup=$(mktemp)
trap 'mv "$source_backup" "$source"; mv "$png_backup" "$png"; mv "$lock_backup" "$lock"' EXIT

cp "$source" "$source_backup"
cp "$png" "$png_backup"
cp "$lock" "$lock_backup"

"$pipeline" verify
printf '\n' >> "$source"
if "$pipeline" verify; then
  exit 1
fi
mv "$source_backup" "$source"
source_backup=$(mktemp)
cp "$source" "$source_backup"
printf '\n' >> "$png"
if "$pipeline" verify; then
  exit 1
fi
mv "$png_backup" "$png"
png_backup=$(mktemp)
cp "$png" "$png_backup"
printf '\n' >> "$lock"
if "$pipeline" verify; then
  exit 1
fi
