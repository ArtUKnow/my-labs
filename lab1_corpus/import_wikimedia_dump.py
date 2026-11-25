"""Import real article text from Wikimedia's current-revision XML dumps.

The input may be the first 128 MiB of a multistream .bz2 dump. Parsing stops
as soon as the requested number of valid articles has been collected.
"""

import argparse
import bz2
import csv
import hashlib
import html
import os
import re
import urllib.parse
import xml.etree.ElementTree as ET
from pathlib import Path


BASE_DIR = Path(__file__).resolve().parent
TEXT_DIR = BASE_DIR / "data" / "text"
PROVENANCE_DIR = BASE_DIR / "data" / "provenance"
SOURCE_HOSTS = {
    "ru_wikipedia": "https://ru.wikipedia.org",
    "simple_wikipedia": "https://simple.wikipedia.org",
}


def strip_nested(text, opening, closing):
    result = []
    depth = 0
    index = 0
    while index < len(text):
        if text.startswith(opening, index):
            depth += 1
            index += len(opening)
        elif depth and text.startswith(closing, index):
            depth -= 1
            index += len(closing)
        else:
            if depth == 0:
                result.append(text[index])
            index += 1
    return "".join(result)


def clean_wikitext(raw):
    text = re.sub(r"<!--.*?-->", " ", raw, flags=re.S)
    text = re.sub(r"<ref\b[^>]*>.*?</ref\s*>", " ", text, flags=re.I | re.S)
    text = re.sub(r"<ref\b[^>]*/>", " ", text, flags=re.I)
    text = strip_nested(text, "{|", "|}")
    text = strip_nested(text, "{{", "}}")
    text = re.sub(r"\[\[(?:File|Image|Category|Файл|Изображение|Категория):[^\]]*\]\]", " ", text, flags=re.I)
    text = re.sub(r"\[\[([^\]]+)\]\]", lambda match: match.group(1).split("|")[-1].split("#")[0], text)
    text = re.sub(r"\[https?://[^\s\]]+(?:\s+([^\]]+))?\]", lambda match: match.group(1) or " ", text)
    text = re.sub(r"<[^>]+>", " ", text)
    text = re.sub(r"https?://\S+", " ", text)
    text = re.sub(r"__[^_]+__", " ", text)
    text = text.replace("'''", "").replace("''", "")
    text = html.unescape(text)
    lines = []
    for line in text.splitlines():
        line = re.sub(r"^[*#;:|!={}\s]+", "", line)
        line = re.sub(r"[=\s]+$", "", line)
        line = " ".join(line.split())
        if line:
            lines.append(line)
    return "\n".join(lines)


def existing_titles(source):
    path = BASE_DIR / "data" / "raw" / source / "urls.txt"
    titles = set()
    if path.exists():
        for line in path.read_text(encoding="utf-8").splitlines():
            url = line.split("\t", 1)[0]
            title = urllib.parse.unquote(urllib.parse.urlsplit(url).path[len("/wiki/"):])
            titles.add(title.replace("_", " ").casefold())
    return titles


def import_dump(input_path, source, target, output_dir, manifest_path):
    if source not in SOURCE_HOSTS:
        raise ValueError("unknown source: " + source)
    output_dir = Path(output_dir)
    manifest_path = Path(manifest_path)
    output_dir.mkdir(parents=True, exist_ok=True)
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    excluded = existing_titles(source)
    seen_page_ids = set()
    written = set()
    added = 0
    with manifest_path.open("w", encoding="utf-8", newline="") as manifest:
        writer = csv.writer(manifest, delimiter="\t", lineterminator="\n")
        writer.writerow(["file", "pageid", "title", "url", "text_sha256"])
        try:
            with bz2.open(input_path, "rb") as stream:
                for _, page in ET.iterparse(stream, events=("end",)):
                    if not page.tag.endswith("}page"):
                        continue
                    if page.findtext("{*}ns") != "0" or page.find("{*}redirect") is not None:
                        page.clear()
                        continue
                    pageid = page.findtext("{*}id")
                    title = page.findtext("{*}title")
                    raw = page.findtext("{*}revision/{*}text") or ""
                    if not pageid or not title or pageid in seen_page_ids or title.casefold() in excluded:
                        page.clear()
                        continue
                    seen_page_ids.add(pageid)
                    if len(raw) < 500 or len(raw) > 250000 or raw.lstrip().lower().startswith(("#redirect", "#перенаправление")):
                        page.clear()
                        continue
                    cleaned = clean_wikitext(raw)
                    if len(cleaned) < 200:
                        page.clear()
                        continue
                    name = "wiki_" + pageid + ".txt"
                    content = title + "\n" + cleaned + "\n"
                    destination = output_dir / name
                    destination.write_text(content, encoding="utf-8")
                    written.add(name)
                    url = SOURCE_HOSTS[source] + "/wiki/" + urllib.parse.quote(title.replace(" ", "_"))
                    writer.writerow([name, pageid, title, url, hashlib.sha256(content.encode("utf-8")).hexdigest()])
                    added += 1
                    page.clear()
                    if added >= target:
                        break
        except (EOFError, ET.ParseError, OSError) as error:
            raise RuntimeError("dump ended before %d valid articles: %d imported" % (target, added)) from error
    if added != target:
        raise RuntimeError("dump contained only %d valid articles, expected %d" % (added, target))
    for stale in output_dir.glob("wiki_*.txt"):
        if stale.name not in written:
            stale.unlink()
    return added


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("input", help="local Wikimedia pages-articles-multistream .bz2 file")
    parser.add_argument("source", choices=sorted(SOURCE_HOSTS))
    parser.add_argument("target", type=int)
    parser.add_argument("--output-dir")
    parser.add_argument("--manifest")
    args = parser.parse_args()
    output_dir = args.output_dir or TEXT_DIR / args.source
    manifest = args.manifest or PROVENANCE_DIR / (args.source + ".tsv")
    count = import_dump(args.input, args.source, args.target, output_dir, manifest)
    print(args.source, "imported", count, "articles")


if __name__ == "__main__":
    main()
