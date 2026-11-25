import os
import sys
import json
import time

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from tokenizer import tokenize, token_kind


BASE_DIR = os.path.dirname(os.path.abspath(__file__))
TEXT_DIR = os.path.join(BASE_DIR, "..", "data", "text")
TOKENS_DIR = os.path.join(BASE_DIR, "..", "data", "tokens")
RESULTS_DIR = os.path.join(BASE_DIR, "results")


def list_text_files(text_dir):
    files = []
    for source in sorted(os.listdir(text_dir)):
        source_dir = os.path.join(text_dir, source)
        if not os.path.isdir(source_dir):
            continue
        for name in sorted(os.listdir(source_dir)):
            if name.endswith(".txt"):
                files.append((source, name, os.path.join(source_dir, name)))
    return files


def tokenize_corpus(text_dir, tokens_dir):
    files = list_text_files(text_dir)
    total_tokens = 0
    total_length = 0
    total_bytes = 0
    total_time = 0.0
    kinds = {"word": 0, "number": 0, "mixed": 0}
    length_hist = {}
    timing_points = []
    long_tokens = []
    for source, name, path in files:
        text = open(path, encoding="utf-8").read()
        size = len(text.encode("utf-8"))
        start = time.perf_counter()
        tokens = tokenize(text)
        spent = time.perf_counter() - start
        out_dir = os.path.join(tokens_dir, source)
        os.makedirs(out_dir, exist_ok=True)
        f = open(os.path.join(out_dir, name), "w", encoding="utf-8")
        f.write("\n".join(tokens))
        f.close()
        total_tokens += len(tokens)
        total_bytes += size
        total_time += spent
        for token in tokens:
            total_length += len(token)
            kinds[token_kind(token)] += 1
            n = len(token)
            if n > 20:
                n = 20
            length_hist[n] = length_hist.get(n, 0) + 1
            if len(token) >= 25 and len(long_tokens) < 15 and token not in long_tokens:
                long_tokens.append(token)
        timing_points.append({"bytes": size, "ms": round(spent * 1000, 3), "tokens": len(tokens)})
    stats = {
        "documents": len(files),
        "tokens": total_tokens,
        "avg_token_length": round(total_length / max(total_tokens, 1), 3),
        "input_bytes": total_bytes,
        "time_seconds": round(total_time, 4),
        "kb_per_second": round(total_bytes / 1024 / max(total_time, 0.000001), 1),
        "ms_per_kb": round(total_time * 1000 / max(total_bytes / 1024, 0.001), 4),
        "kinds": kinds,
        "length_histogram": length_hist,
        "timing_points": sorted(timing_points, key=lambda p: p["bytes"]),
        "long_tokens": long_tokens,
    }
    return stats


def main():
    text_dir = TEXT_DIR
    tokens_dir = TOKENS_DIR
    if len(sys.argv) > 2:
        text_dir = sys.argv[1]
        tokens_dir = sys.argv[2]
    stats = tokenize_corpus(text_dir, tokens_dir)
    print("documents:", stats["documents"])
    print("tokens:", stats["tokens"])
    print("avg token length:", stats["avg_token_length"])
    print("input bytes:", stats["input_bytes"])
    print("time:", stats["time_seconds"], "s")
    print("speed:", stats["kb_per_second"], "KB/s")
    print("kinds:", stats["kinds"])
    os.makedirs(RESULTS_DIR, exist_ok=True)
    f = open(os.path.join(RESULTS_DIR, "stats.json"), "w", encoding="utf-8")
    json.dump(stats, f, ensure_ascii=False, indent=2)
    f.close()


if __name__ == "__main__":
    main()
