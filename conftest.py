"""Root conftest for test discovery.

Adds the scripts directory to sys.path so tests can import the flat script
modules (container_runner, image_lock, task_model, report_writer) the same way
the router itself imports them. The repository root stays ahead of it so the
`deps` package resolves before the flat `scripts/deps.py` entry point can
shadow it.
"""

from __future__ import annotations

import sys
from pathlib import Path

_ROOT = Path(__file__).resolve().parent
_SCRIPTS = _ROOT / "scripts"
if str(_ROOT) not in sys.path:
    sys.path.insert(0, str(_ROOT))
if str(_SCRIPTS) not in sys.path:
    sys.path.append(str(_SCRIPTS))
