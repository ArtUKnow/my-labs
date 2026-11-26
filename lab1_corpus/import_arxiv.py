

import argparse
import csv
import hashlib
import re
import time
import urllib.parse
import urllib.request
import xml.etree.ElementTree as ET
from pathlib import Path


BASE = Path(__file__).resolve().parent / "data"
API = "https://oaipmh.arxiv.org/oai"
OAI = "{http://www.openarchives.org/OAI/2.0/}"
ARXIV = "{http://arxiv.org/OAI/arXiv/}"


def fetch_xml(url):
    request = urllib.request.Request(url, headers={"User-Agent": "MAI-IR-coursework/1.0"})
    for attempt in range(4):
        try:
            with urllib.request.urlopen(request, timeout=90) as response:
                return ET.fromstring(response.read())
        except (OSError, ET.ParseError):
            if attempt == 3:
                raise
            time.sleep(2 ** attempt)


def collect(target, output_dir, manifest_path, start_date="2025-01-01"):
    output_dir = Path(output_dir)
    manifest_path = Path(manifest_path)
    output_dir.mkdir(parents=True, exist_ok=True)
    manifest_path.parent.mkdir(parents=True, exist_ok=True)
    query = urllib.parse.urlencode({"verb": "ListRecords", "metadataPrefix": "arXiv", "set": "physics:astro-ph", "from": start_date})
    url = API + "?" + query
    seen = set()
    count = 0
    with manifest_path.open("w", encoding="utf-8", newline="") as file:
        writer = csv.writer(file, delimiter="\t", lineterminator="\n")
        writer.writerow(["file", "arxiv_id", "title", "url", "authors", "updated", "text_sha256"])
        while url and count < target:
            root = fetch_xml(url)
            error = root.find(OAI + "error")
            if error is not None:
                raise RuntimeError("arXiv OAI: " + (error.text or error.attrib.get("code", "error")))
            for record in root.findall(".//" + OAI + "record"):
                item = record.find(OAI + "metadata/" + ARXIV + "arXiv")
                if item is None:
                    continue
                arxiv_id = item.findtext(ARXIV + "id")
                title = " ".join((item.findtext(ARXIV + "title") or "").split())
                abstract = " ".join((item.findtext(ARXIV + "abstract") or "").split())
                if not arxiv_id or arxiv_id in seen or len(abstract) < 200:
                    continue
                seen.add(arxiv_id)
                authors = []
                for author in item.findall(ARXIV + "authors/" + ARXIV + "author"):
                    name = " ".join(filter(None, [author.findtext(ARXIV + "forenames"), author.findtext(ARXIV + "keyname")]))
                    if name:
                        authors.append(name)
                name = "paper_" + re.sub(r"[^A-Za-z0-9_-]", "_", arxiv_id) + ".txt"
                content = title + "\n" + abstract + "\n"
                (output_dir / name).write_text(content, encoding="utf-8")
                writer.writerow([name, arxiv_id, title, "https://arxiv.org/abs/" + arxiv_id, "; ".join(authors), item.findtext(ARXIV + "updated") or "", hashlib.sha256(content.encode()).hexdigest()])
                count += 1
                if count >= target:
                    break
            token = root.findtext(".//" + OAI + "resumptionToken")
            url = API + "?verb=ListRecords&resumptionToken=" + urllib.parse.quote(token, safe="") if token else None
            print("arxiv:", count, "documents", flush=True)
    if count != target:
        raise RuntimeError("arXiv returned %d documents; expected %d" % (count, target))


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("target", type=int)
    parser.add_argument("--output-dir", default=BASE / "text" / "arxiv_astronomy")
    parser.add_argument("--manifest", default=BASE / "provenance" / "arxiv_astronomy.tsv")
    parser.add_argument("--start-date", default="2025-01-01")
    args = parser.parse_args()
    collect(args.target, args.output_dir, args.manifest, args.start_date)
