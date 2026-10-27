"""Tests for scripts/anonymize_arc_sidebar.py (R-I5).

The synthetic fixture is the same file the C++ reader test uses, so the counts
asserted here are the counts BrowserImportTest.ArcFoldersKeepTitlesAndOrder,
ArcNestedFoldersFlattenToPathNames and ArcFavoritesInOrder expect.
"""

from __future__ import annotations

import json
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parents[2]
SCRIPT = ROOT / "scripts" / "anonymize_arc_sidebar.py"
FIXTURE = ROOT / "tests" / "fixtures" / "arc" / "synthetic-folders.json"

# Values the C++ reader tests assert for this fixture.
EXPECTED_COUNTS = {"spaces": 2, "pinned_tabs": 2, "folders": 3, "favorites": 2}

ORIGINAL_TEXTS = [
    "Work",
    "Linear",
    "Mail",
    "Spec",
    "Specs",
    "Plan",
    "Empty",
    "Today tab",
    "Reading",
    "Article",
    "Docs",
    "Calendar",
    "linear.app",
    "spec.test",
    "plan.test",
    "mail.test",
    "today.test",
    "read.test",
    "docs.test",
    "cal.test",
]


def run_script(tmp_path: Path) -> tuple[dict, dict, Path]:
    out = tmp_path / "anonymized.json"
    counts = tmp_path / "counts.json"
    result = subprocess.run(
        [sys.executable, str(SCRIPT), str(FIXTURE), str(out), "--counts", str(counts)],
        capture_output=True,
        text=True,
        check=False,
    )
    assert result.returncode == 0, result.stderr
    return json.loads(out.read_text(encoding="utf-8")), json.loads(
        counts.read_text(encoding="utf-8")
    ), out


def structure_of(document: dict) -> dict:
    """Ids, containment lists and colours, with every free-text string removed."""
    containers = document["sidebar"]["containers"]
    sidebar = containers[1]
    return {
        "spaces_ids": sidebar["spaces"][::2],
        "items_ids": sidebar["items"][::2],
        "space_ids": [s["id"] for s in sidebar["spaces"][1::2]],
        "container_ids": [s["containerIDs"] for s in sidebar["spaces"][1::2]],
        "children": {
            item["id"]: item.get("childrenIds")
            for item in sidebar["items"][1::2]
        },
        "colors": sidebar["spaces"][1]["customInfo"],
        "top_apps": sidebar["topAppsContainerIDs"],
    }


def test_script_exists() -> None:
    assert SCRIPT.is_file()


def test_no_original_title_or_url_remains(tmp_path: Path) -> None:
    _, _, out = run_script(tmp_path)
    text = out.read_text(encoding="utf-8")
    for original in ORIGINAL_TEXTS:
        assert original not in text, original
    assert "https://example.test/" in text


def test_ids_structure_and_colours_are_unchanged(tmp_path: Path) -> None:
    anonymized, _, _ = run_script(tmp_path)
    original = json.loads(FIXTURE.read_text(encoding="utf-8"))
    assert structure_of(anonymized) == structure_of(original)


def test_titles_become_numbered_placeholders(tmp_path: Path) -> None:
    anonymized, _, _ = run_script(tmp_path)
    sidebar = anonymized["sidebar"]["containers"][1]
    titles = [s["title"] for s in sidebar["spaces"][1::2]]
    assert titles[0].startswith("T")
    # An empty space title stays empty so the reader still applies its fallback.
    assert titles[1] == ""
    items = {item["id"]: item for item in sidebar["items"][1::2]}
    assert items["T1"]["title"] is None
    assert items["T2"]["data"]["tab"]["savedURL"].startswith("https://example.test/")


def test_counts_match_the_reader_tests(tmp_path: Path) -> None:
    _, counts, _ = run_script(tmp_path)
    assert counts == EXPECTED_COUNTS

