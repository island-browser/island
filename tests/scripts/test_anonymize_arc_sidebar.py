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


def run_on(tmp_path: Path, document: object) -> tuple[subprocess.CompletedProcess, Path, Path]:
    """Runs the script on `document` (any JSON value) and returns the process and its files."""
    source = tmp_path / "in.json"
    source.write_text(json.dumps(document), encoding="utf-8")
    out = tmp_path / "anonymized.json"
    counts = tmp_path / "counts.json"
    result = subprocess.run(
        [sys.executable, str(SCRIPT), str(source), str(out), "--counts", str(counts)],
        capture_output=True,
        text=True,
        check=False,
    )
    return result, out, counts


def fixture_document() -> dict:
    return json.loads(FIXTURE.read_text(encoding="utf-8"))


def sidebar_items(document: dict) -> list[dict]:
    """The item objects of the container; the flat list also holds their ids."""
    entries = document["sidebar"]["containers"][1]["items"]
    return [entry for entry in entries if isinstance(entry, dict)]


def item_by_id(document: dict, item_id: str) -> dict:
    return next(item for item in sidebar_items(document) if item["id"] == item_id)


def test_parent_id_keys_are_ids_and_stay_unchanged(tmp_path: Path) -> None:
    # Arc's item rows also carry other id-valued keys, such as parentID; they must
    # keep their values so the structure stays linked.
    document = fixture_document()
    item_by_id(document, "T1")["parentID"] = "P1"
    item_by_id(document, "T1")["bundleID"] = "com.example.app"
    result, out, _ = run_on(tmp_path, document)
    assert result.returncode == 0, result.stderr
    anonymized = json.loads(out.read_text(encoding="utf-8"))
    item = item_by_id(anonymized, "T1")
    assert item["parentID"] == "P1"
    assert item["bundleID"] == "com.example.app"


def test_non_string_saved_url_is_not_a_tab(tmp_path: Path) -> None:
    # The C++ reader reads savedURL with StringOr, so a number there is no URL and
    # the tab is skipped. Mail (T5) is the second top-level pinned tab.
    document = fixture_document()
    for item in sidebar_items(document):
        if item.get("id") == "T5":
            item["data"]["tab"]["savedURL"] = 5
    result, _, counts = run_on(tmp_path, document)
    assert result.returncode == 0, result.stderr
    assert json.loads(counts.read_text(encoding="utf-8"))["pinned_tabs"] == 1


def test_duplicate_item_id_first_one_wins(tmp_path: Path) -> None:
    # The C++ reader keeps the first item with an id (std::map::emplace); a later
    # duplicate with an empty URL must not replace Mail (T5).
    document = fixture_document()
    document["sidebar"]["containers"][1]["items"].append(
        {"id": "T5", "childrenIds": [], "data": {"tab": {"savedURL": ""}}}
    )
    result, _, counts = run_on(tmp_path, document)
    assert result.returncode == 0, result.stderr
    assert json.loads(counts.read_text(encoding="utf-8"))["pinned_tabs"] == 2


def test_pinned_tabs_are_capped_like_the_reader(tmp_path: Path) -> None:
    # kMaxImportedBookmarks is 500; the reader stops at that many pinned tabs.
    tab_ids = [f"T{n}" for n in range(600)]
    items = [{"id": "P1", "childrenIds": tab_ids, "data": {"itemContainer": {}}}]
    items += [
        {"id": tab_id, "childrenIds": [], "data": {"tab": {"savedURL": f"https://x.test/{n}"}}}
        for n, tab_id in enumerate(tab_ids)
    ]
    document = {
        "sidebar": {
            "containers": [
                {"global": {}},
                {
                    "topAppsContainerIDs": [],
                    "spaces": [{"id": "S1", "title": "Big", "containerIDs": ["pinned", "P1"]}],
                    "items": items,
                },
            ]
        }
    }
    result, _, counts = run_on(tmp_path, document)
    assert result.returncode == 0, result.stderr
    assert json.loads(counts.read_text(encoding="utf-8"))["pinned_tabs"] == 500


def test_non_object_root_is_a_clean_error(tmp_path: Path) -> None:
    result, _, _ = run_on(tmp_path, [1, 2, 3])
    assert result.returncode == 1
    assert "Traceback" not in result.stderr
    assert "not an Arc sidebar" in result.stderr
