#!/usr/bin/env python3
"""Build the Flapped Ear Telemetry user guide into a static site.

Uses only the Python standard library. Each file in ``pages/`` is an HTML body
fragment whose first line is a metadata comment:

    <!-- title: Page title | nav: Navigation label -->

Pages are emitted in the order listed in ``NAV``. The build fails when a page
is missing from ``NAV``, or when an internal link points to a missing page or
anchor, so the published site cannot silently contain broken navigation.

Usage: python3 build.py [output-directory]   (default: _site)
"""

from __future__ import annotations

import html
import re
import shutil
import sys
from html.parser import HTMLParser
from pathlib import Path

ROOT = Path(__file__).resolve().parent
PAGES = ROOT / "pages"
ASSETS = ROOT / "assets"
SITE_NAME = "Flapped Ear Telemetry"
REPOSITORY = "https://github.com/arekkozuch/VBOOverlay"

# Navigation groups and page order. Each entry is the page file stem.
NAV: list[tuple[str, list[str]]] = [
    ("Start here", ["index", "installation", "quick-start"]),
    ("Overlay editor", ["editor", "importing", "synchronization", "widgets", "templates", "export"]),
    ("Analysis", ["analysis", "comparison", "segments", "day-report"]),
    ("Reference", ["projects", "telemetry-data", "shortcuts", "troubleshooting", "limitations"]),
]

META_RE = re.compile(r"^<!--\s*title:\s*(?P<title>[^|]+?)\s*\|\s*nav:\s*(?P<nav>.+?)\s*-->\s*$")


class _LinkCollector(HTMLParser):
    def __init__(self) -> None:
        super().__init__()
        self.ids: set[str] = set()
        self.hrefs: list[str] = []
        self.headings: list[tuple[int, str, str]] = []
        self._heading: tuple[int, str] | None = None
        self._text: list[str] = []

    def handle_starttag(self, tag, attrs):
        attributes = dict(attrs)
        if "id" in attributes and attributes["id"]:
            self.ids.add(attributes["id"])
        if tag == "a" and attributes.get("href"):
            self.hrefs.append(attributes["href"])
        if tag in ("h2", "h3") and attributes.get("id"):
            self._heading = (int(tag[1]), attributes["id"])
            self._text = []

    def handle_data(self, data):
        if self._heading:
            self._text.append(data)

    def handle_endtag(self, tag):
        if self._heading and tag == f"h{self._heading[0]}":
            level, anchor = self._heading
            self.headings.append((level, anchor, " ".join("".join(self._text).split())))
            self._heading = None


def load_pages() -> dict[str, dict]:
    pages: dict[str, dict] = {}
    for path in sorted(PAGES.glob("*.html")):
        first, _, body = path.read_text(encoding="utf-8").partition("\n")
        match = META_RE.match(first)
        if not match:
            raise SystemExit(f"{path.name}: first line must be a title/nav metadata comment")
        collector = _LinkCollector()
        collector.feed(body)
        pages[path.stem] = {
            "title": html.unescape(match["title"]),
            "nav": html.unescape(match["nav"]),
            "body": body,
            "ids": collector.ids,
            "hrefs": collector.hrefs,
            "headings": collector.headings,
        }
    return pages


def validate(pages: dict[str, dict]) -> None:
    ordered = [stem for _, stems in NAV for stem in stems]
    errors = []
    for stem in ordered:
        if stem not in pages:
            errors.append(f"NAV lists missing page '{stem}'")
    for stem in pages:
        if stem not in ordered:
            errors.append(f"page '{stem}' is not listed in NAV")
    for stem, page in pages.items():
        for href in page["hrefs"]:
            if re.match(r"^(https?:|mailto:)", href):
                continue
            target, _, anchor = href.partition("#")
            target_stem = stem if target == "" else target.removesuffix(".html")
            if target and not target.endswith(".html"):
                errors.append(f"{stem}: link '{href}' must target an .html page")
                continue
            if target_stem not in pages:
                errors.append(f"{stem}: link '{href}' points to a missing page")
            elif anchor and anchor not in pages[target_stem]["ids"]:
                errors.append(f"{stem}: link '{href}' points to a missing anchor")
    if errors:
        raise SystemExit("User guide validation failed:\n  " + "\n  ".join(errors))


def render_nav(pages: dict[str, dict], current: str) -> str:
    parts = []
    for group, stems in NAV:
        items = []
        for stem in stems:
            label = html.escape(pages[stem]["nav"])
            attrs = ' aria-current="page"' if stem == current else ""
            items.append(f'<li><a href="{stem}.html"{attrs}>{label}</a></li>')
        parts.append(f'<p class="nav-group">{html.escape(group)}</p>\n<ul>{"".join(items)}</ul>')
    return "\n".join(parts)


def render_toc(page: dict) -> str:
    entries = [(level, anchor, text) for level, anchor, text in page["headings"] if level == 2]
    if len(entries) < 3:
        return ""
    links = "".join(f'<li><a href="#{a}">{html.escape(t)}</a></li>' for _, a, t in entries)
    return f'<nav class="toc" aria-label="On this page"><p>On this page</p><ul>{links}</ul></nav>'


def place_toc(body: str, toc: str) -> str:
    """Insert the table of contents after the lead paragraph (or the h1)."""
    if not toc:
        return body
    for marker in ('<p class="lead">', "<h1"):
        start = body.find(marker)
        if start == -1:
            continue
        closing = "</p>" if marker.startswith("<p") else "</h1>"
        end = body.find(closing, start)
        if end != -1:
            end += len(closing)
            return body[:end] + "\n" + toc + body[end:]
    return toc + "\n" + body


def render_pager(pages: dict[str, dict], current: str) -> str:
    ordered = [stem for _, stems in NAV for stem in stems]
    index = ordered.index(current)
    links = []
    if index > 0:
        prev = ordered[index - 1]
        links.append(f'<a class="prev" href="{prev}.html"><span>Previous</span>{html.escape(pages[prev]["nav"])}</a>')
    if index + 1 < len(ordered):
        nxt = ordered[index + 1]
        links.append(f'<a class="next" href="{nxt}.html"><span>Next</span>{html.escape(pages[nxt]["nav"])}</a>')
    return f'<nav class="pager" aria-label="Page navigation">{"".join(links)}</nav>'


TEMPLATE = """<!doctype html>
<html lang="en">
<head>
<meta charset="utf-8">
<meta name="viewport" content="width=device-width, initial-scale=1">
<title>{title}</title>
<meta name="description" content="Flapped Ear Telemetry user guide: {page_title}">
<link rel="stylesheet" href="assets/style.css">
</head>
<body>
<a class="skip" href="#content">Skip to content</a>
<header class="topbar">
  <button class="menu" type="button" aria-controls="sidebar" aria-expanded="false">Menu</button>
  <a class="brand" href="index.html">{site} <span>User Guide</span></a>
  <a class="repo" href="{repo}">Source on GitHub</a>
</header>
<div class="layout">
  <nav id="sidebar" class="sidebar" aria-label="User guide">
{nav}
  </nav>
  <main id="content">
<article>
{body}
</article>
{pager}
<footer>Flapped Ear Telemetry is in development. This guide describes the application at the source revision it was built from; see <a href="limitations.html">Limitations</a>.</footer>
  </main>
</div>
<script>
(function () {{
  var button = document.querySelector('.menu');
  var sidebar = document.getElementById('sidebar');
  button.addEventListener('click', function () {{
    var open = sidebar.classList.toggle('open');
    button.setAttribute('aria-expanded', open ? 'true' : 'false');
  }});
}})();
</script>
</body>
</html>
"""


def build(output: Path) -> None:
    pages = load_pages()
    validate(pages)
    if output.exists():
        shutil.rmtree(output)
    shutil.copytree(ASSETS, output / "assets")
    for stem, page in pages.items():
        title = page["title"] if stem == "index" else f'{page["title"]} · {SITE_NAME}'
        document = TEMPLATE.format(
            title=html.escape(title),
            page_title=html.escape(page["title"]),
            site=SITE_NAME,
            repo=REPOSITORY,
            nav=render_nav(pages, stem),
            body=place_toc(page["body"].strip(), render_toc(page)),
            pager=render_pager(pages, stem),
        )
        (output / f"{stem}.html").write_text(document, encoding="utf-8")
    print(f"Built {len(pages)} pages into {output}")


if __name__ == "__main__":
    target = Path(sys.argv[1]) if len(sys.argv) > 1 else ROOT / "_site"
    build(target.resolve())
