import os
import sys
import json


BASE_DIR = os.path.dirname(os.path.abspath(__file__))
RAW_DIR = os.path.join(BASE_DIR, "..", "data", "raw")
TEXT_DIR = os.path.join(BASE_DIR, "..", "data", "text")
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
    for name in os.listdir(path):
        if name.endswith(".txt"):
            text = open(os.path.join(path, name), encoding="utf-8").read()
            words += len(text.split())
    return words


def collect_stats(raw_dir, text_dir):
    stats = {"sources": [], "total": {}}
    total_raw = 0
    total_text = 0
    total_docs = 0
    total_words = 0
    for source_name in sorted(os.listdir(raw_dir)):
        raw_source = os.path.join(raw_dir, source_name)
        text_source = os.path.join(text_dir, source_name)
        if not os.path.isdir(raw_source):
            continue
        raw_size, raw_count = dir_size(raw_source, ".html")
        text_size, text_count = dir_size(text_source, ".txt")
        words = 0
        if os.path.isdir(text_source):
            words = count_words(text_source)
        item = {
            "name": source_name,
            "documents": raw_count,
            "raw_bytes": raw_size,
            "text_bytes": text_size,
            "words": words,
            "avg_raw_bytes": raw_size // max(raw_count, 1),
            "avg_text_bytes": text_size // max(text_count, 1),
            "avg_words": words // max(text_count, 1),
            "text_ratio": round(text_size / max(raw_size, 1), 4),
        }
        stats["sources"].append(item)
        total_raw += raw_size
        total_text += text_size
        total_docs += raw_count
        total_words += words
    stats["total"] = {
        "documents": total_docs,
        "raw_bytes": total_raw,
        "text_bytes": total_text,
        "words": total_words,
        "avg_raw_bytes": total_raw // max(total_docs, 1),
        "avg_text_bytes": total_text // max(total_docs, 1),
        "avg_words": total_words // max(total_docs, 1),
        "text_ratio": round(total_text / max(total_raw, 1), 4),
    }
    return stats


def print_stats(stats):
    for item in stats["sources"]:
        print("source:", item["name"])
        print("  documents:", item["documents"])
        print("  raw size:", item["raw_bytes"], "bytes")
        print("  text size:", item["text_bytes"], "bytes")
        print("  avg raw doc:", item["avg_raw_bytes"], "bytes")
        print("  avg text doc:", item["avg_text_bytes"], "bytes")
        print("  avg words:", item["avg_words"])
    total = stats["total"]
    print("total documents:", total["documents"])
    print("total raw size:", total["raw_bytes"])
    print("total text size:", total["text_bytes"])
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
