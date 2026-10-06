#!/usr/bin/env python3
"""Point the product site's absolute SEO URLs at a deployment domain.

The site is static and fully relative, so it works on any host as-is; the only
domain-bound parts are the SEO absolute URLs (canonical, og:url, JSON-LD ids,
sitemap locations, and the robots sitemap line). Run this once after deploying
to a new domain, then commit the result:

    python3 scripts/site_set_domain.py https://island.example.com
    python3 scripts/site_set_domain.py https://impelixx.github.io/island   # back to Pages

The trailing slash of |domain| is optional and normalized. Only the exact
current domain (https://impelixx.github.io/island) is rewritten, so running
the script twice is safe.
"""

from __future__ import annotations

import sys
from pathlib import Path

REPOSITORY = Path(__file__).resolve().parents[1]
SITE = REPOSITORY / "site"

OLD = "https://impelixx.github.io/island"
TARGETS = ("index.html", "docs.html", "changelog.html", "privacy.html", "sitemap.xml", "robots.txt")


def main() -> int:
    if len(sys.argv) != 2:
        print(__doc__)
        return 2
    domain = sys.argv[1].rstrip("/")
    if not domain.startswith("https://"):
        print("error: the domain must be an https:// URL", file=sys.stderr)
        return 2
    new = domain
    for name in TARGETS:
        path = SITE / name
        text = path.read_text(encoding="utf-8")
        if OLD in text:
            path.write_text(text.replace(OLD, new), encoding="utf-8")
            print(f"{name}: rewritten")
        elif new in text:
            print(f"{name}: already points at {new}")
        else:
            print(f"{name}: no absolute site URLs found (left as is)")
    print("done — commit the result so the deploy and the SEO URLs agree")
    return 0


if __name__ == "__main__":
    sys.exit(main())
