#!/usr/bin/env bash
set -euo pipefail

root_dir="$(cd "$(dirname "${BASH_SOURCE[0]}")/.." && pwd)"
exec python3 "${root_dir}/scripts/deps.py" --root "${root_dir}" install "$@"
