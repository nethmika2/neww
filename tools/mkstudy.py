#!/usr/bin/env python3
"""Convert HTML revision notes into the plain-text format the STUDY app reads.

Usage
-----
    python3 tools/mkstudy.py NOTES.html --out sd-card/study
    python3 tools/mkstudy.py *.html --out sd-card/study --split 20000

Why a converter instead of reading HTML on the device: the CYD gets a compact,
word-wrapped text file it can stream line by line from the SD card, and the HTML
stays on the PC where it is easy to edit.

Output format (see sd-card/README.md):
    # Subject title          first one wins
    ## Topic heading         topic + flashcard front
    Q: ... / A: ...          extra flashcards
    - bullet
    body text, wrapped to the reader's width
"""
from __future__ import annotations

import argparse
import os
import re
import sys
from html.parser import HTMLParser

WRAP = 48           # characters per line; the firmware wraps at 49
HEADINGS = ("h1", "h2", "h3", "h4", "h5", "h6")
SKIP_TAGS = {"script", "style", "head", "title", "nav", "svg", "noscript"}
BLOCK_TAGS = {"p", "div", "section", "article", "header", "footer", "blockquote", "pre", "figure",
              "table", "thead", "tbody", "tr", "ul", "ol", "dl", "dt", "dd", "hr", "form"}
# Sub-headings that carry no information on a 320 px screen; dropped so they do
# not waste flashcard fronts.
NOISE_HEADINGS = {"contents", "table of contents", "index", "summary", "overview", "home"}


class NoteExtractor(HTMLParser):
    """Pulls headings, list items and paragraphs out of an HTML document."""

    def __init__(self) -> None:
        super().__init__(convert_charrefs=True)
        self.blocks: list[tuple[str, str]] = []      # (kind, text)
        self._text: list[str] = []
        self._kind = "body"
        self._skip = 0
        self._table_cells: list[str] = []
        self._in_table_row = False

    # -- helpers ---------------------------------------------------------
    def _flush(self) -> None:
        text = " ".join("".join(self._text).split())
        self._text = []
        if text:
            self.blocks.append((self._kind, text))
        self._kind = "body"

    def _push(self, kind: str) -> None:
        self._flush()
        self._kind = kind

    # -- HTMLParser hooks -------------------------------------------------
    def handle_starttag(self, tag: str, attrs) -> None:
        tag = tag.lower()
        if tag in SKIP_TAGS:
            self._skip += 1
            return
        if self._skip:
            return
        if tag in HEADINGS:
            self._push("h")
        elif tag == "li":
            self._push("li")
        elif tag == "tr":
            self._flush()
            self._in_table_row = True
            self._table_cells = []
        elif tag in ("td", "th"):
            self._text = []
        elif tag == "br":
            self._flush()
        elif tag in BLOCK_TAGS:
            self._flush()

    def handle_endtag(self, tag: str) -> None:
        tag = tag.lower()
        if tag in SKIP_TAGS:
            self._skip = max(0, self._skip - 1)
            return
        if self._skip:
            return
        if tag in ("td", "th") and self._in_table_row:
            cell = " ".join("".join(self._text).split())
            self._text = []
            if cell:
                self._table_cells.append(cell)
        elif tag == "tr" and self._in_table_row:
            self._in_table_row = False
            row = " | ".join(self._table_cells)
            self._table_cells = []
            self._text = []
            if row:
                self.blocks.append(("li", row))
        elif tag in HEADINGS or tag == "li" or tag in BLOCK_TAGS:
            self._flush()

    def handle_data(self, data: str) -> None:
        if self._skip:
            return
        if self._in_table_row:
            self._text.append(data)
        elif data.strip():
            self._text.append(data)

    def close(self) -> None:  # noqa: D102 - matches HTMLParser API
        super().close()
        self._flush()


def slugify(name: str) -> str:
    name = os.path.splitext(os.path.basename(name))[0]
    name = re.sub(r"\(\d+\)$", "", name)                 # drop "(1)" from downloads
    name = name.replace("_", " ")
    name = re.sub(r"[^A-Za-z0-9]+", "-", name).strip("-").lower()
    return name or "notes"


def wrap(text: str, width: int, indent: int = 0) -> list[str]:
    """Greedy word wrap; continuation lines get `indent` spaces."""
    words = text.split()
    lines: list[str] = []
    cur = ""
    for word in words:
        if not cur:
            cur = word
        elif len(cur) + 1 + len(word) <= width:
            cur += " " + word
        else:
            lines.append(cur)
            cur = " " * indent + word
    if cur:
        lines.append(cur)
    return lines


def convert(path: str, split: int) -> list[tuple[str, str]]:
    """Returns [(suggested file stem, file text)] for one HTML document."""
    with open(path, "r", encoding="utf-8", errors="replace") as fh:
        raw = fh.read()

    parser = NoteExtractor()
    parser.feed(raw)
    parser.close()

    title = ""
    out: list[str] = []
    skipping = False                             # inside a dropped section
    for kind, text in parser.blocks:
        if kind == "h":
            low = text.strip().lower()
            if not title:
                title = text.strip()
                skipping = False
                continue                        # the document title, not a topic
            if low in NOISE_HEADINGS or len(low) < 3:
                skipping = True
                continue
            skipping = False
            out.append("")
            out.append("## " + " ".join(text.split()))
            continue
        if skipping:
            continue
        if kind == "li":
            for i, line in enumerate(wrap(text, WRAP - 2)):
                out.append(("- " if i == 0 else "  ") + line)
            continue
        # Body text: keep "Question: / Answer:" style lines as flashcards.
        m = re.match(r"^(question|q)\s*[:\-]\s*(.+)$", text, re.IGNORECASE)
        if m:
            out.append("")
            out.append("Q: " + " ".join(m.group(2).split()))
            continue
        m = re.match(r"^(answer|a)\s*[:\-]\s*(.+)$", text, re.IGNORECASE)
        if m:
            for i, line in enumerate(wrap(m.group(2), WRAP - 3)):
                out.append(("A: " if i == 0 else "   ") + line)
            out.append("")
            continue
        out.append("")
        for line in wrap(text, WRAP):
            out.append(line)
        out.append("")

    if not title:
        title = os.path.splitext(os.path.basename(path))[0].replace("_", " ").strip().title()

    # Collapse the runs of blank lines the block handling leaves behind.
    body: list[str] = []
    for line in out:
        if line == "" and (not body or body[-1] == ""):
            continue
        body.append(line)
    while body and body[-1] == "":
        body.pop()

    # Split oversized notes on block boundaries so no part is silently
    # truncated by the reader's pool.
    chunks: list[list[str]] = []
    current: list[str] = []
    size = 0
    for line in body:
        if split and size + len(line) + 1 > split and current and current[-1] == "":
            chunks.append(current)
            current = []
            size = 0
        current.append(line)
        size += len(line) + 1
    chunks.append(current)

    stem = slugify(path)
    if len(chunks) == 1:
        return [(stem, "# " + title + "\n" + "\n".join(chunks[0]).lstrip("\n") + "\n")]
    out_files = []
    for i, chunk in enumerate(chunks):
        head = f"# {title} (part {i + 1})" if len(chunks) > 1 else f"# {title}"
        out_files.append((f"{stem}-part{i + 1}", head + "\n" + "\n".join(chunk).lstrip("\n") + "\n"))
    return out_files


def main() -> int:
    ap = argparse.ArgumentParser(description="Convert HTML revision notes for the CYD STUDY app.")
    ap.add_argument("inputs", nargs="+", help="HTML files to convert")
    ap.add_argument("--out", default="sd-card/study", help="output folder (default: sd-card/study)")
    ap.add_argument("--split", type=int, default=15000,
                    help="split a note into parts above this many characters "
                         "(0 = never; the reader holds about 16000)")
    args = ap.parse_args()

    os.makedirs(args.out, exist_ok=True)
    written = 0
    for path in args.inputs:
        if not os.path.isfile(path):
            print(f"skip (not a file): {path}", file=sys.stderr)
            continue
        for stem, text in convert(path, args.split):
            target = os.path.join(args.out, stem + ".txt")
            with open(target, "w", encoding="utf-8") as fh:
                fh.write(text)
            topics = text.count("\n## ")
            cards = topics + text.count("\nQ: ")
            print(f"{os.path.basename(path)} -> {target}  "
                  f"({len(text)} chars, {topics} topics, {cards} cards)")
            written += 1
    if not written:
        print("nothing converted", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
