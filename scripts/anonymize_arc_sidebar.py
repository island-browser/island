#!/usr/bin/env python3
"""Anonymize an Arc StorableSidebar.json for a committed test fixture (R-I5).

Usage: python3 scripts/anonymize_arc_sidebar.py IN OUT --counts COUNTS.json

The output keeps the structure: ids, containment lists (containerIDs, childrenIds,
topAppsContainerIDs), the space and item layout, and colours. It replaces every
other string with T<n> (titles, savedTitle and any free text), in document order,
and every URL (savedURL, or any http:// or https:// string) with
https://example.test/<n>. Empty strings and null stay as they are, so the reader's
fallbacks still apply.

COUNTS.json is {"spaces", "pinned_tabs", "folders", "favorites"}, counted on IN with
the rules of ParseArcSidebarStructure in src/main/browser_import.cc:
- the container is the first entry of sidebar.containers with "spaces" and "items";
- spaces are the objects of "spaces" (flat [id, object] pairs or plain objects);
- pinned_tabs are the tabs with a non-empty savedURL directly under the container
  that follows "pinned" in each space's containerIDs;
- folders are the pinned folders (any depth) that hold at least one tab of their
  own; empty folders are dropped, and nested folders count on their own;
- favorites are the tabs with a non-empty savedURL flattened from the container
  that follows "topApps" in topAppsContainerIDs.
- As in the reader, an item id is taken from its first occurrence, a savedURL that
  is not a string is no URL, and pinned_tabs and folders are capped at 500 per space
  and favorites at 500 in total (kMaxImportedBookmarks).

Only standard library modules are used.
"""

from __future__ import annotations

import argparse
import json
import sys
from pathlib import Path
from typing import Any, Final

# Lists whose strings are structure (ids and markers) and are kept as they are.
# † topAppsContainerIDs and its "topApps" marker are guesses (R-I5); keep them in
# step with kArcTopAppsKey and kArcTopAppsMarker in src/main/browser_import.cc.
STRUCTURAL_LISTS: Final = frozenset(
    {"spaces", "items", "containerIDs", "childrenIds", "topAppsContainerIDs"}
)
# The C++ reader caps recursion at 64 levels (kMaxTreeDepth).
MAX_DEPTH: Final = 64
# kMaxImportedBookmarks in src/main/bookmark_import.h.
MAX_IMPORTED_BOOKMARKS: Final = 500
TEXT_PREFIX: Final = "T"
URL_PREFIX: Final = "https://example.test/"
URL_SCHEMES: Final = ("http://", "https://")


def arc_objects(array: Any) -> list[dict]:
    """The object entries of an Arc collection, as ArcObjects reads them."""
    if not isinstance(array, list):
        return []
    return [entry for entry in array if isinstance(entry, dict)]


def marker_ids(ids: Any, marker: str) -> list[str]:
    """The ids that follow `marker` in an alternating marker and id list."""
    if not isinstance(ids, list):
        return []
    return [
        ids[index + 1]
        for index in range(len(ids) - 1)
        if ids[index] == marker and isinstance(ids[index + 1], str)
    ]


def tab_url(item: dict) -> str | None:
    """The savedURL of a tab item ("" when it is not a string), or None when not a tab."""
    data = item.get("data")
    tab = data.get("tab") if isinstance(data, dict) else None
    if not isinstance(tab, dict):
        return None
    url = tab.get("savedURL")
    return url if isinstance(url, str) else ""


def is_id_key(key: str | None) -> bool:
    """Keys whose string is an identifier: "id", and "...ID" or "...Id" (parentID)."""
    return key is not None and (key == "id" or key.endswith(("ID", "Id")))


def children_of(items: dict[str, dict], node: dict) -> list[dict]:
    children = node.get("childrenIds")
    if not isinstance(children, list):
        return []
    return [
        items[child_id]
        for child_id in children
        if isinstance(child_id, str) and child_id in items
    ]


def count_folder(items: dict[str, dict], folder: dict, depth: int) -> int:
    """Counts the pinned folders under `folder`, the folder itself included."""
    if depth > MAX_DEPTH:
        return 0
    own_tabs = 0
    nested = 0
    for child in children_of(items, folder):
        url = tab_url(child)
        if url is None:
            nested += count_folder(items, child, depth + 1)
        elif url:
            own_tabs += 1
    return nested + (1 if own_tabs else 0)


def count_favorites(items: dict[str, dict], node: dict, depth: int) -> int:
    if depth > MAX_DEPTH:
        return 0
    total = 0
    for child in children_of(items, node):
        url = tab_url(child)
        if url is None:
            total += count_favorites(items, child, depth + 1)
        elif url:
            total += 1
    return total


def count_arc(document: dict) -> dict[str, int]:
    """The counts COUNTS.json records, computed on the input document."""
    sidebar = document.get("sidebar")
    containers = sidebar.get("containers", []) if isinstance(sidebar, dict) else []
    container = next(
        (c for c in containers if isinstance(c, dict) and "spaces" in c and "items" in c),
        None,
    )
    counts = {"spaces": 0, "pinned_tabs": 0, "folders": 0, "favorites": 0}
    if container is None:
        return counts
    items: dict[str, dict] = {}
    for item in arc_objects(container.get("items")):
        item_id = item.get("id")
        if isinstance(item_id, str) and item_id:
            items.setdefault(item_id, item)
    spaces = arc_objects(container.get("spaces"))
    counts["spaces"] = len(spaces)
    for space in spaces:
        pinned_tabs = 0
        folders = 0
        for pinned_id in marker_ids(space.get("containerIDs"), "pinned"):
            pinned = items.get(pinned_id)
            if pinned is None:
                continue
            for child in children_of(items, pinned):
                url = tab_url(child)
                if url is None:
                    folders += count_folder(items, child, 0)
                elif url:
                    pinned_tabs += 1
        counts["pinned_tabs"] += min(pinned_tabs, MAX_IMPORTED_BOOKMARKS)
        counts["folders"] += min(folders, MAX_IMPORTED_BOOKMARKS)
    favorites = 0
    for top_apps_id in marker_ids(container.get("topAppsContainerIDs"), "topApps"):
        top_apps = items.get(top_apps_id)
        if top_apps is not None:
            favorites += count_favorites(items, top_apps, 0)
    counts["favorites"] = min(favorites, MAX_IMPORTED_BOOKMARKS)
    return counts


class Namer:
    """Hands out T<n> and example.test URLs in document order."""

    def __init__(self) -> None:
        self.texts = 0
        self.urls = 0

    def text(self) -> str:
        self.texts += 1
        return f"{TEXT_PREFIX}{self.texts}"

    def url(self) -> str:
        self.urls += 1
        return f"{URL_PREFIX}{self.urls}"


def anonymize(node: Any, namer: Namer, key: str | None = None) -> Any:
    """Returns `node` with its free text and URLs replaced, structure untouched."""
    if isinstance(node, dict):
        return {k: anonymize(v, namer, k) for k, v in node.items()}
    if isinstance(node, list):
        keep_strings = key in STRUCTURAL_LISTS
        return [
            v if keep_strings and isinstance(v, str) else anonymize(v, namer, key)
            for v in node
        ]
    if isinstance(node, str):
        if not node or is_id_key(key):
            return node
        if key == "savedURL" or node.startswith(URL_SCHEMES):
            return namer.url()
        return namer.text()
    return node


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("input", type=Path)
    parser.add_argument("output", type=Path)
    parser.add_argument("--counts", type=Path, required=True)
    args = parser.parse_args(argv)
    try:
        document = json.loads(args.input.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        print(f"anonymize_arc_sidebar: cannot read {args.input}: {error}", file=sys.stderr)
        return 1
    if not isinstance(document, dict):
        print(
            f"anonymize_arc_sidebar: {args.input} is not an Arc sidebar: its root is not an object",
            file=sys.stderr,
        )
        return 1
    counts = count_arc(document)
    anonymized = anonymize(document, Namer())
    args.output.write_text(
        json.dumps(anonymized, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
    )
    args.counts.write_text(json.dumps(counts, indent=2) + "\n", encoding="utf-8")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
