"""Titles and source links for documents stored in the shared corpus."""

import csv
import gzip
from collections import Counter
from pathlib import Path


DATA_DIR = Path(__file__).resolve().parent.parent / "lab1_corpus" / "data"
SOURCE_LABELS = {
    "ru_wikipedia": "Википедия · RU",
    "scoap3": "SCOAP³ · физика высоких энергий",
    "simple_wikipedia": "Википедия · Simple English",
    "arxiv_astronomy": "arXiv · астрономия",
    "space_stackexchange": "Space Exploration",
    "astronomy_stackexchange": "Astronomy",
}


class DocumentCatalog:
    def __init__(self, data_dir, doc_names):
        self.data_dir = Path(data_dir)
        metadata = {}
        provenance_dir = self.data_dir / "provenance"
        if provenance_dir.exists():
            for manifest in provenance_dir.glob("*.tsv"):
                source = manifest.stem
                with manifest.open(encoding="utf-8", newline="") as file:
                    for row in csv.DictReader(file, delimiter="\t"):
                        filename = row["file"]
                        suffix = 7 if filename.endswith(".txt.gz") else 4
                        key = source + "/" + filename[:-suffix]
                        metadata[key] = self._record(key, row.get("title", ""), row["url"], row.get("authors") or row.get("author") or "")
        raw_dir = self.data_dir / "raw"
        if raw_dir.exists():
            for urls_file in raw_dir.glob("*/urls.txt"):
                source = urls_file.parent.name
                with urls_file.open(encoding="utf-8") as file:
                    for line in file:
                        if not line.strip():
                            continue
                        url, filename = line.rstrip("\n").split("\t", 1)
                        key = source + "/" + Path(filename).stem
                        metadata[key] = self._record(key, Path(filename).stem.replace("_", " "), url, "")
        missing = [name for name in doc_names if name not in metadata]
        if missing:
            raise ValueError("Нет заголовков и ссылок для %d документов; первый: %s" % (len(missing), missing[0]))
        self.records = [metadata[name] for name in doc_names]
        self.source_counts = Counter(record["source"] for record in self.records)

    @staticmethod
    def _record(doc_id, title, url, authors):
        if not url.startswith(("https://", "http://")):
            raise ValueError("Недопустимая ссылка документа: " + doc_id)
        source = doc_id.split("/", 1)[0]
        return {
            "id": doc_id,
            "title": title or doc_id.split("/", 1)[1],
            "url": url,
            "source": source,
            "source_label": SOURCE_LABELS.get(source, source),
            "authors": authors,
        }

    def get(self, doc_num):
        return self.records[doc_num]

    def snippet(self, doc_num, limit=190):
        record = self.records[doc_num]
        source, filename = record["id"].split("/", 1)
        path = self.data_dir / "text" / source / (filename + ".txt")
        if not path.exists():
            path = path.with_suffix(".txt.gz")
        opener = gzip.open if path.suffix == ".gz" else open
        with opener(path, "rt", encoding="utf-8") as file:
            text = file.read(1600)
        body = text.split("\n", 1)[-1]
        body = " ".join(body.split())
        return body[:limit].rstrip() + ("…" if len(body) > limit else "")
