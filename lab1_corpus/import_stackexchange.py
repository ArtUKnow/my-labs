"""Collect complete question texts from Space Exploration or Astronomy Stack Exchange."""

import argparse
import csv
import hashlib
import html
from html.parser import HTMLParser
import json
import re
import time
import urllib.parse
import urllib.request
from pathlib import Path


BASE = Path(__file__).resolve().parent / "data"
API = "https://api.stackexchange.com/2.3/questions"
SITES = {"space": "space_stackexchange", "astronomy": "astronomy_stackexchange"}


class TextParser(HTMLParser):
    def __init__(self):
        super().__init__(convert_charrefs=True)
        self.parts = []

    def handle_starttag(self, tag, attrs):
        if tag in {"p", "br", "li", "div", "pre", "blockquote"}:
            self.parts.append("\n")

    def handle_data(self, data):
        self.parts.append(data)


def clean_body(value):
    parser = TextParser()
    parser.feed(value)
    return "\n".join(line for line in (" ".join(part.split()) for part in "".join(parser.parts).splitlines()) if line)


def fetch_page(site, page, before=None):
    parameters = {"page": page, "pagesize": 100, "order": "desc", "sort": "creation", "site": site, "filter": "withbody"}
    if before is not None:
        parameters["todate"] = before
    query = urllib.parse.urlencode(parameters)
    request = urllib.request.Request(API + "?" + query, headers={"User-Agent": "MAI-IR-coursework/1.0"})
    for attempt in range(4):
        try:
            with urllib.request.urlopen(request, timeout=45) as response:
                return json.load(response)
        except (OSError, json.JSONDecodeError):
            if attempt == 3:
                raise
            time.sleep(2 ** attempt)


def creation_before(site, question_id):
    query = urllib.parse.urlencode({"site": site})
    url = "https://api.stackexchange.com/2.3/questions/" + question_id + "?" + query
    request = urllib.request.Request(url, headers={"User-Agent": "MAI-IR-coursework/1.0"})
    with urllib.request.urlopen(request, timeout=45) as response:
        items = json.load(response)["items"]
    if not items:
        raise RuntimeError("saved question no longer available: " + question_id)
    return items[0]["creation_date"] - 1


def collect(site, target, output_dir, manifest_path):
    if site not in SITES:
        raise ValueError(site)
    output_dir = Path(output_dir)
    manifest_path = Path(manifest_path)
    output_dir.mkdir(parents=True, exist_ok=True)
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    seen = set()
    if manifest_path.exists():
        with manifest_path.open(encoding="utf-8", newline="") as old:
            reader = csv.DictReader(old, delimiter="\t")
            fields = reader.fieldnames
            rows = list(reader)
        licensed = [row for row in rows if row["license"]]
        if len(licensed) != len(rows):
            with manifest_path.open("w", encoding="utf-8", newline="") as cleaned:
                writer = csv.DictWriter(cleaned, fieldnames=fields, delimiter="\t", lineterminator="\n")
                writer.writeheader()
                writer.writerows(licensed)
            for row in rows:
                if not row["license"]:
                    (output_dir / row["file"]).unlink(missing_ok=True)
        seen = {row["question_id"] for row in licensed}
    count = len(seen)
    page = 1
    before = creation_before(site, licensed[-1]["question_id"]) if count else None
    with manifest_path.open("a" if count else "w", encoding="utf-8", newline="") as file:
        writer = csv.writer(file, delimiter="\t", lineterminator="\n")
        if not count:
            writer.writerow(["file", "question_id", "title", "url", "author", "license", "text_sha256"])
        while count < target:
            response = fetch_page(site, page, before)
            if "error_message" in response:
                raise RuntimeError(response["error_message"])
            for item in response.get("items", []):
                question_id = str(item["question_id"])
                title = html.unescape(item["title"])
                body = clean_body(item.get("body", ""))
                if question_id in seen or len(body) < 200 or not item.get("content_license") or item.get("closed_reason") == "Duplicate":
                    continue
                seen.add(question_id)
                name = "question_" + question_id + ".txt"
                content = title + "\n" + body + "\n"
                (output_dir / name).write_text(content, encoding="utf-8")
                writer.writerow([name, question_id, title, item["link"], html.unescape(item.get("owner", {}).get("display_name", "")), item.get("content_license", ""), hashlib.sha256(content.encode()).hexdigest()])
                count += 1
                if count >= target:
                    break
            print(site, "page", page, "before", before, "documents", count, "quota", response.get("quota_remaining"), flush=True)
            if count >= target:
                break
            if not response.get("has_more"):
                raise RuntimeError("site had only %d qualifying questions" % count)
            if response.get("quota_remaining", 0) < 1:
                raise RuntimeError("Stack Exchange API quota exhausted")
            time.sleep(max(0.5, response.get("backoff", 0)))
            if page == 25:
                before = min(item["creation_date"] for item in response["items"]) - 1
                page = 1
            else:
                page += 1


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("site", choices=sorted(SITES))
    parser.add_argument("target", type=int)
    parser.add_argument("--output-dir")
    parser.add_argument("--manifest")
    args = parser.parse_args()
    collect(args.site, args.target, args.output_dir or BASE / "text" / SITES[args.site], args.manifest or BASE / "provenance" / (SITES[args.site] + ".tsv"))
