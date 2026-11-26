

import argparse
import csv
import gzip
import hashlib
import html
import json
import math
import re
import subprocess
import time
import urllib.parse
import urllib.error
import urllib.request
import xml.etree.ElementTree as ET
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path


DATA = Path(__file__).resolve().parent.parent / "lab1_corpus" / "data"
API = "https://repo.scoap3.org/api/records/"
JOURNALS = (
    "Physical Review D",
    "Physics Letters B",
    "Nuclear Physics B",
    "Physical Review Letters",
    "Chinese Physics C",
    "Physical Review C",
    "Progress of Theoretical and Experimental Physics",
    "Journal of Cosmology and Astroparticle Physics",
    "Journal of High Energy Physics",
    "European Physical Journal C",
    "Advances in High Energy Physics",
)
HEADERS = {"User-Agent": "MAI-IR-coursework/1.0 (open-access research corpus)"}
FIELDS = ("file", "record_id", "doi", "title", "journal", "year", "url", "xml_url", "pdf_url", "source_format", "license", "source_bytes", "words", "sha256")
WORD = re.compile(r"\b[A-Za-z][A-Za-z'\-]*\b")


def get(url, attempts=4, max_bytes=None):
    for attempt in range(attempts):
        try:
            request = urllib.request.Request(url, headers=HEADERS)
            with urllib.request.urlopen(request, timeout=45) as response:
                declared_size = response.headers.get("Content-Length")
                if max_bytes and declared_size and int(declared_size) > max_bytes:
                    raise ValueError("source file exceeds size limit")
                content = response.read(max_bytes + 1 if max_bytes else -1)
                if max_bytes and len(content) > max_bytes:
                    raise ValueError("source file exceeds size limit")
                return content
        except ValueError:
            raise
        except urllib.error.HTTPError as error:
            if error.code not in (408, 429, 500, 502, 503, 504):
                raise
            if attempt == attempts - 1:
                raise
            time.sleep(2 ** attempt)
        except Exception:
            if attempt == attempts - 1:
                raise
            time.sleep(2 ** attempt)


def api_page(journal, page):
    url = API + "?" + urllib.parse.urlencode({"journal": journal, "page": page})
    return json.loads(get(url))


def candidate(hit):
    meta = hit["metadata"]
    xml_file = next((item for item in meta.get("_files", []) if item.get("filetype") == "xml" and item.get("file")), None)
    pdf_file = next((item for item in meta.get("_files", []) if item.get("filetype") == "pdf" and item.get("file")), None)
    license_name = next((item.get("license", "") for item in meta.get("license", [])), "")
    if not (xml_file or pdf_file) or not license_name.upper().startswith("CC-BY"):
        return None
    material = next((item.get("material", "") for item in meta.get("publication_info", [])), "")
    if material and material.lower() not in ("article", "research article"):
        return None
    record_id = str(meta["control_number"])
    doi = next((item.get("value", "") for item in meta.get("dois", [])), "")
    title = next((item.get("title", "") for item in meta.get("titles", [])), "")
    if title.lower().startswith(("retraction", "correction", "corrigendum", "erratum", "publisher correction")):
        return None
    journal = next((item.get("primary", "") for item in meta.get("collections", [])), "")
    if journal not in JOURNALS:
        return None
    year = next((str(item.get("year", "")) for item in meta.get("publication_info", [])), "")
    return {"record_id": record_id, "doi": doi, "title": title, "journal": journal,
            "year": year, "url": "https://repo.scoap3.org/records/" + record_id,
            "xml_url": xml_file["file"] if xml_file else "",
            "pdf_url": pdf_file["file"] if pdf_file else "", "license": license_name}


def discover(path, workers=8):
    path.parent.mkdir(parents=True, exist_ok=True)
    found = {}
    if path.exists():
        with path.open(encoding="utf-8") as file:
            for line in file:
                row = json.loads(line)
                found[row["record_id"]] = row
    for journal in JOURNALS:
        first = api_page(journal, 1)
        pages = math.ceil(first["count"] / 10)
        seen_pages = {row["page"] for row in found.values() if row.get("query_journal") == journal}

        ledger = path.with_suffix(".pages")
        if ledger.exists():
            with ledger.open(encoding="utf-8") as file:
                seen_pages = {int(row["page"]) for row in csv.DictReader(file, delimiter="\t") if row["journal"] == journal}
        pending = [page for page in range(1, pages + 1) if page not in seen_pages]
        print(journal, "pages", pages, "pending", len(pending), flush=True)
        if not pending:
            continue
        with path.open("a", encoding="utf-8") as output, ledger.open("a", encoding="utf-8", newline="") as pages_file:
            writer = csv.writer(pages_file, delimiter="\t", lineterminator="\n")
            if pages_file.tell() == 0:
                writer.writerow(("journal", "page"))
            with ThreadPoolExecutor(max_workers=workers) as pool:
                futures = {pool.submit(lambda p: first if p == 1 else api_page(journal, p), page): page for page in pending}
                for future in as_completed(futures):
                    page = futures[future]
                    data = future.result()
                    for hit in data["hits"]["hits"]:
                        row = candidate(hit)
                        if row and row["record_id"] not in found:
                            row.update(query_journal=journal, page=page)
                            output.write(json.dumps(row, ensure_ascii=False) + "\n")
                            found[row["record_id"]] = row
                    output.flush()
                    writer.writerow((journal, page))
                    pages_file.flush()
                    if page % 100 == 0:
                        print(journal, "page", page, "candidates", len(found), flush=True)
    print("candidates", len(found), flush=True)
    return found


def local_tag(element):
    return element.tag.rsplit("}", 1)[-1]


def extract_body(xml_bytes):
    xml_text = xml_bytes.decode("utf-8", errors="replace")
    xml_text = re.sub(r"&(?!amp;|lt;|gt;|quot;|apos;)([A-Za-z][A-Za-z0-9._-]*);",
                      lambda match: html.unescape(match.group(0)) if html.unescape(match.group(0)) != match.group(0) else " ",
                      xml_text)
    xml_bytes = xml_text.encode("utf-8")

    used = set(re.findall(rb"</?([A-Za-z][\w.-]*):", xml_bytes))
    used.update(re.findall(rb"\s([A-Za-z][\w.-]*):[\w.-]+=", xml_bytes))
    declared = set(re.findall(rb"xmlns:([A-Za-z][\w.-]*)=", xml_bytes))
    missing = used - declared - {b"xml", b"xmlns"}
    if missing:
        additions = b"".join(
            b' xmlns:' + prefix + b'="' +
            (b"http://www.w3.org/1998/Math/MathML" if prefix == b"mml" else b"urn:scoap3:undeclared:" + prefix) +
            b'"' for prefix in sorted(missing)
        )
        xml_bytes = re.sub(rb"<(?:simple-article|article)(?=[\s>])", lambda match: match.group(0) + additions,
                           xml_bytes)
    try:
        roots = [ET.fromstring(xml_bytes)]
    except ET.ParseError as error:
        if "junk after document element" not in str(error):
            raise
        roots = [ET.fromstring(fragment) for fragment in re.findall(rb"<article(?=[\s>]).*?</article>", xml_bytes, re.DOTALL)]
    bodies = [element for root in roots for element in root.iter() if local_tag(element) == "body"]
    if not bodies:
        return None
    body = max(bodies, key=lambda element: sum(len(part) for part in element.itertext()))
    for parent in body.iter():
        for index, child in reversed(list(enumerate(list(parent)))):
            if local_tag(child) in ("math", "tex-math", "graphic", "inline-graphic"):
                if child.tail:
                    if index:
                        previous = parent[index - 1]
                        previous.tail = (previous.tail or "") + child.tail
                    else:
                        parent.text = (parent.text or "") + child.tail
                parent.remove(child)
    text = " ".join(" ".join(body.itertext()).split())
    return text


def extract_pdf_body(pdf_bytes):
    result = subprocess.run(["pdftotext", "-", "-"], input=pdf_bytes, capture_output=True,
                            check=True, timeout=30)
    text = result.stdout.decode("utf-8", errors="replace")
    start = re.search(r"(?mi)^\s*(?:1\s+)?Introduction\s*$", text)
    if start is None:
        return None
    end = re.search(r"(?mi)^\s*References\s*$", text[start.end():])
    if end is None:
        return None
    text = text[start.end():start.end() + end.start()]
    text = re.sub(r"(?m)^\s*JHEP\d{2}\(\d{4}\)\d+\s*$", "", text)
    return " ".join(text.split())


def fetch_article(row, min_words):
    if row["journal"] in ("Journal of High Energy Physics", "European Physical Journal C"):
        if not row.get("pdf_url"):
            return None
        source = get(row["pdf_url"], attempts=2, max_bytes=8_000_000)
        body = extract_pdf_body(source)
        source_format = "pdf"
    else:
        if not row.get("xml_url"):
            return None
        source = get(row["xml_url"])
        body = extract_body(source)
        source_format = "xml"
    if not body:
        return None
    words = len(WORD.findall(body))
    if words < min_words:
        return None
    title = " ".join(row["title"].split())
    content = title + "\n" + body + "\n"
    return content, words, source_format, len(source)


def collect(candidates, output_dir, manifest_path, target=50000, min_words=3000, workers=12):
    output_dir.mkdir(parents=True, exist_ok=True)
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    completed = {}
    if manifest_path.exists():
        with manifest_path.open(encoding="utf-8", newline="") as file:
            completed = {row["record_id"]: row for row in csv.DictReader(file, delimiter="\t")}
    failures_path = manifest_path.with_suffix(".rejected.txt")
    rejected = set(failures_path.read_text(encoding="utf-8").splitlines()) if failures_path.exists() else set()
    rows = [row for row in candidates.values() if row["record_id"] not in completed and row["record_id"] not in rejected]
    pdf_journals = {"Journal of High Energy Physics", "European Physical Journal C"}
    rows.sort(key=lambda row: (row["journal"] in pdf_journals, row["journal"], row["record_id"]))
    print("accepted", len(completed), "remaining candidates", len(rows), flush=True)
    with manifest_path.open("a", encoding="utf-8", newline="") as manifest, failures_path.open("a", encoding="utf-8") as failures:
        writer = csv.DictWriter(manifest, FIELDS, delimiter="\t", lineterminator="\n")
        if manifest.tell() == 0:
            writer.writeheader()
        with ThreadPoolExecutor(max_workers=workers) as pool:
            cursor = 0
            pending = {}
            while len(completed) < target and (cursor < len(rows) or pending):
                while cursor < len(rows) and len(pending) < workers * 3:
                    row = rows[cursor]
                    pending[pool.submit(fetch_article, row, min_words)] = row
                    cursor += 1
                future = next(as_completed(pending))
                row = pending.pop(future)
                try:
                    result = future.result()
                except Exception as error:
                    print("fetch error", row["record_id"], type(error).__name__, str(error)[:100], flush=True)
                    permanent_http = isinstance(error, urllib.error.HTTPError) and error.code in (403, 404, 410)
                    oversized = isinstance(error, ValueError) and str(error) == "source file exceeds size limit"
                    if permanent_http or oversized:
                        failures.write(row["record_id"] + "\n")
                        failures.flush()
                    continue
                if result is None:
                    failures.write(row["record_id"] + "\n")
                    failures.flush()
                    continue
                content, words, source_format, source_bytes = result
                filename = "scoap3_" + row["record_id"] + ".txt.gz"
                (output_dir / filename).write_bytes(gzip.compress(content.encode(), compresslevel=6, mtime=0))
                record = {"file": filename, **row, "words": words, "source_format": source_format,
                          "source_bytes": source_bytes,
                          "sha256": hashlib.sha256(content.encode()).hexdigest()}
                record["title"] = " ".join(record["title"].split())
                writer.writerow({field: record.get(field, "") for field in FIELDS})
                manifest.flush()
                completed[row["record_id"]] = record
                if len(completed) % 500 == 0:
                    print("accepted", len(completed), "scanned", cursor, flush=True)
    if len(completed) < target:
        raise RuntimeError(f"Only {len(completed)} articles with {min_words}+ words; target {target}")
    print("accepted", len(completed), "full-text articles", flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--target", type=int, default=50000)
    parser.add_argument("--min-words", type=int, default=3000)
    parser.add_argument("--metadata-workers", type=int, default=8)
    parser.add_argument("--download-workers", type=int, default=12)
    parser.add_argument("--discover-only", action="store_true")
    parser.add_argument("--collect-only", action="store_true")
    args = parser.parse_args()
    candidates_path = DATA / "provenance" / "scoap3_candidates.jsonl"
    if args.collect_only:
        with candidates_path.open(encoding="utf-8") as file:
            candidates = {row["record_id"]: row for row in map(json.loads, file)}
    else:
        candidates = discover(candidates_path, workers=args.metadata_workers)
    if not args.discover_only:
        collect(candidates, DATA / "text" / "scoap3", DATA / "provenance" / "scoap3.tsv", args.target, args.min_words, args.download_workers)


if __name__ == "__main__":
    main()
