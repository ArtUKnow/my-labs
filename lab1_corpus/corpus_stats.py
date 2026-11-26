import os
import sys
import json
import gzip
import csv


BASE_DIR = os.path.dirname(os.path.abspath(__file__))
RAW_DIR = os.path.join(BASE_DIR, "data", "primary_raw")
TEXT_DIR = os.path.join(BASE_DIR, "data", "primary_text")
RESULTS_DIR = os.path.join(BASE_DIR, "results")


def dir_size(path, ext):
    total = 0
    count = 0
    if not os.path.isdir(path):
        return 0, 0
    for name in os.listdir(path):
        if name.endswith(ext):
            total += os.path.getsize(os.path.join(path, name))
            count += 1
    return total, count


def count_words(path):
    words = 0
    text_bytes = 0
    for name in os.listdir(path):
        if name.endswith((".txt", ".txt.gz")):
            opener = gzip.open if name.endswith(".gz") else open
            with opener(os.path.join(path, name), "rt", encoding="utf-8") as file:
                text = file.read()
            words += len(text.split())
            text_bytes += len(text.encode("utf-8"))
    return words, text_bytes


def collect_stats(raw_dir, text_dir):
    stats = {"sources": [], "total": {}}
    total_raw = 0
    total_text = 0
    total_stored = 0
    total_docs = 0
    total_raw_docs = 0
    total_source_docs = 0
    total_words = 0
    source_names = sorted(
        {name for name in os.listdir(raw_dir) if os.path.isdir(os.path.join(raw_dir, name))}
        | {name for name in os.listdir(text_dir) if os.path.isdir(os.path.join(text_dir, name))}
    )
    for source_name in source_names:
        raw_source = os.path.join(raw_dir, source_name)
        text_source = os.path.join(text_dir, source_name)
        raw_size, raw_count = dir_size(raw_source, ".html")
        stored_bytes, text_count = dir_size(text_source, (".txt", ".txt.gz"))
        source_count = 0
        if source_name == "scoap3":
            manifest_path = os.path.join(os.path.dirname(text_dir), "provenance", "scoap3.tsv")
            if os.path.exists(manifest_path):
                with open(manifest_path, encoding="utf-8", newline="") as file:
                    rows = list(csv.DictReader(file, delimiter="\t"))
                if len(rows) == text_count and all(row.get("source_bytes") for row in rows):
                    raw_size = sum(int(row["source_bytes"]) for row in rows)
                    source_count = len(rows)
        words = 0
        text_size = 0
        if os.path.isdir(text_source):
            words, text_size = count_words(text_source)
        item = {
            "name": source_name,
            "documents": text_count,
            "raw_html_documents": raw_count,
            "raw_source_documents": source_count,
            "raw_bytes": raw_size,
            "text_bytes": text_size,
            "stored_bytes": stored_bytes,
            "words": words,
            "avg_raw_bytes": raw_size // max(raw_count + source_count, 1),
            "avg_text_bytes": text_size // max(text_count, 1),
            "avg_words": words // max(text_count, 1),
            "text_ratio": round(text_size / raw_size, 4) if text_count == raw_count + source_count and raw_size else None,
        }
        stats["sources"].append(item)
        total_raw += raw_size
        total_text += text_size
        total_stored += stored_bytes
        total_docs += text_count
        total_raw_docs += raw_count
        total_source_docs += source_count
        total_words += words
    stats["total"] = {
        "documents": total_docs,
        "raw_html_documents": total_raw_docs,
        "raw_source_documents": total_source_docs,
        "raw_bytes": total_raw,
        "text_bytes": total_text,
        "stored_bytes": total_stored,
        "words": total_words,
        "avg_raw_bytes": total_raw // max(total_raw_docs + total_source_docs, 1),
        "avg_text_bytes": total_text // max(total_docs, 1),
        "avg_words": total_words // max(total_docs, 1),
        "text_ratio": round(total_text / total_raw, 4) if total_docs == total_raw_docs + total_source_docs and total_raw else None,
    }
    return stats


def print_stats(stats):
    for item in stats["sources"]:
        print("source:", item["name"])
        print("  documents:", item["documents"])
        print("  raw HTML examples:", item["raw_html_documents"])
        print("  raw source files:", item["raw_source_documents"])
        print("  raw size:", item["raw_bytes"], "bytes")
        print("  text size:", item["text_bytes"], "bytes")
        print("  stored size:", item["stored_bytes"], "bytes")
        print("  avg raw doc:", item["avg_raw_bytes"], "bytes")
        print("  avg text doc:", item["avg_text_bytes"], "bytes")
        print("  avg words:", item["avg_words"])
    total = stats["total"]
    print("total documents:", total["documents"])
    print("raw HTML examples:", total["raw_html_documents"])
    print("raw source files:", total["raw_source_documents"])
    print("total raw size:", total["raw_bytes"])
    print("total text size:", total["text_bytes"])
    print("total stored size:", total["stored_bytes"])
    print("avg raw doc:", total["avg_raw_bytes"])
    print("avg text doc:", total["avg_text_bytes"])
    print("avg words per doc:", total["avg_words"])
    print("text ratio:", total["text_ratio"])


def main():
    raw_dir = RAW_DIR
    text_dir = TEXT_DIR
    if len(sys.argv) > 2:
        raw_dir = sys.argv[1]
        text_dir = sys.argv[2]
    stats = collect_stats(raw_dir, text_dir)
    print_stats(stats)
    os.makedirs(RESULTS_DIR, exist_ok=True)
    f = open(os.path.join(RESULTS_DIR, "stats.json"), "w", encoding="utf-8")
    json.dump(stats, f, ensure_ascii=False, indent=2)
    f.close()


if __name__ == "__main__":
    main()
