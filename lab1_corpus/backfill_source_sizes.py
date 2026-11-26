"""Fill missing original SCOAP3 XML/PDF byte sizes via HTTP HEAD requests."""

import argparse
import csv
import os
import time
import urllib.request
from concurrent.futures import ThreadPoolExecutor, as_completed
from pathlib import Path


DEFAULT_MANIFEST = Path(__file__).resolve().parent / "data" / "provenance" / "scoap3.tsv"


def source_size(row):
    url = row["pdf_url"] if row["source_format"] == "pdf" else row["xml_url"]
    if not url.startswith("https://"):
        raise ValueError("missing source URL for " + row["record_id"])
    for attempt in range(4):
        try:
            request = urllib.request.Request(url, method="HEAD", headers={"User-Agent": "MAI-IR-coursework/1.0"})
            with urllib.request.urlopen(request, timeout=30) as response:
                value = response.headers.get("Content-Length")
            if not value or int(value) <= 0:
                raise ValueError("missing Content-Length for " + row["record_id"])
            return int(value)
        except Exception:
            if attempt == 3:
                raise
            time.sleep(2 ** attempt)


def backfill(path, workers):
    with path.open(encoding="utf-8", newline="") as file:
        reader = csv.DictReader(file, delimiter="\t")
        fields = reader.fieldnames
        rows = list(reader)
    missing = [index for index, row in enumerate(rows) if not row["source_bytes"]]
    print("missing", len(missing), flush=True)
    errors = []
    with ThreadPoolExecutor(max_workers=workers) as pool:
        futures = {pool.submit(source_size, rows[index]): index for index in missing}
        for finished, future in enumerate(as_completed(futures), 1):
            index = futures[future]
            try:
                rows[index]["source_bytes"] = str(future.result())
            except Exception as error:
                errors.append((rows[index]["record_id"], str(error)))
            if finished % 1000 == 0:
                print("checked", finished, "of", len(missing), flush=True)
    temporary = path.with_suffix(".tmp")
    with temporary.open("w", encoding="utf-8", newline="") as file:
        writer = csv.DictWriter(file, fields, delimiter="\t", lineterminator="\n")
        writer.writeheader()
        writer.writerows(rows)
    os.replace(temporary, path)
    if errors:
        raise RuntimeError("Failed sizes: " + repr(errors[:10]))
    print("raw bytes", sum(int(row["source_bytes"]) for row in rows), flush=True)


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--manifest", type=Path, default=DEFAULT_MANIFEST)
    parser.add_argument("--workers", type=int, default=64)
    arguments = parser.parse_args()
    backfill(arguments.manifest, arguments.workers)


if __name__ == "__main__":
    main()
