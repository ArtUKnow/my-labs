import os
import sys
import json
import time
import struct
import gzip
import heapq
from functools import lru_cache

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lab3_tokenizer"))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lab4_stemmer"))

from tokenizer import tokenize
from stemmer import stem


BASE_DIR = os.path.dirname(os.path.abspath(__file__))
TEXT_DIR = os.path.join(BASE_DIR, "..", "lab1_corpus", "data", "primary_text")
INDEX_DIR = os.path.join(BASE_DIR, "..", "lab1_corpus", "data", "index")
RESULTS_DIR = os.path.join(BASE_DIR, "results")


def list_documents(text_dir):
    documents = []
    for source in sorted(os.listdir(text_dir)):
        source_dir = os.path.join(text_dir, source)
        if not os.path.isdir(source_dir):
            continue
        for name in sorted(os.listdir(source_dir)):
            if name.endswith((".txt", ".txt.gz")):
                suffix = 7 if name.endswith(".txt.gz") else 4
                documents.append((source + "/" + name[:-suffix], os.path.join(source_dir, name)))
    return documents


@lru_cache(maxsize=200000)
def normalize(token, mode):
    if mode == "stem":
        return stem(token)
    return token.replace("ё", "е")


def write_block(block, block_path):
    terms = sorted(block.keys())
    f = open(block_path, "w", encoding="utf-8")
    for term in terms:
        doc_ids = sorted(block[term])
        f.write(term + "\t" + " ".join(str(d) for d in doc_ids) + "\n")
    f.close()


def read_block_line(handle):
    line = handle.readline()
    if line == "":
        return None, None
    term, ids = line.rstrip("\n").split("\t")
    return term, [int(x) for x in ids.split(" ")]


def merge_sorted_lists(a, b):
    result = []
    i = 0
    j = 0
    while i < len(a) and j < len(b):
        if a[i] < b[j]:
            result.append(a[i])
            i += 1
        elif a[i] > b[j]:
            result.append(b[j])
            j += 1
        else:
            result.append(a[i])
            i += 1
            j += 1
    result.extend(a[i:])
    result.extend(b[j:])
    return result


def merge_blocks(block_paths, index_dir):
    handles = [open(p, encoding="utf-8") for p in block_paths]
    heap = []
    for index, handle in enumerate(handles):
        term, ids = read_block_line(handle)
        if term is not None:
            heapq.heappush(heap, (term, index, ids))
    postings_file = open(os.path.join(index_dir, "postings.bin"), "wb")
    dictionary_file = open(os.path.join(index_dir, "dictionary.txt"), "w", encoding="utf-8")
    offset = 0
    term_count = 0
    posting_count = 0
    while heap:
        smallest = heap[0][0]
        merged = []
        while heap and heap[0][0] == smallest:
            _, index, ids = heapq.heappop(heap)
            merged = merge_sorted_lists(merged, ids)
            term, next_ids = read_block_line(handles[index])
            if term is not None:
                heapq.heappush(heap, (term, index, next_ids))
        data = struct.pack("<%dI" % len(merged), *merged)
        postings_file.write(data)
        dictionary_file.write("%s\t%d\t%d\t%d\n" % (smallest, len(merged), offset, len(data)))
        offset += len(data)
        term_count += 1
        posting_count += len(merged)
    postings_file.close()
    dictionary_file.close()
    for h in handles:
        h.close()
    return term_count, posting_count


def build_index(text_dir, index_dir, mode="stem", block_limit=200000):
    start = time.time()
    os.makedirs(index_dir, exist_ok=True)
    blocks_dir = os.path.join(index_dir, "blocks")
    os.makedirs(blocks_dir, exist_ok=True)
    documents = list_documents(text_dir)
    docs_file = open(os.path.join(index_dir, "docs.txt"), "w", encoding="utf-8")
    block = {}
    block_tokens = 0
    block_paths = []
    total_tokens = 0
    for doc_id in range(len(documents)):
        name, path = documents[doc_id]
        docs_file.write("%d\t%s\n" % (doc_id, name))
        opener = gzip.open if path.endswith(".gz") else open
        with opener(path, "rt", encoding="utf-8") as document:
            text = document.read()
        for token in tokenize(text):
            term = normalize(token, mode)
            if term not in block:
                block[term] = set()
            block[term].add(doc_id)
            block_tokens += 1
            total_tokens += 1
        if block_tokens >= block_limit:
            block_path = os.path.join(blocks_dir, "block_%03d.txt" % len(block_paths))
            write_block(block, block_path)
            block_paths.append(block_path)
            block = {}
            block_tokens = 0
    if len(block) > 0 or len(block_paths) == 0:
        block_path = os.path.join(blocks_dir, "block_%03d.txt" % len(block_paths))
        write_block(block, block_path)
        block_paths.append(block_path)
    docs_file.close()
    term_count, posting_count = merge_blocks(block_paths, index_dir)
    for p in block_paths:
        os.remove(p)
    os.rmdir(blocks_dir)
    f = open(os.path.join(index_dir, "meta.txt"), "w", encoding="utf-8")
    f.write("mode\t%s\n" % mode)
    f.close()
    stats = {
        "mode": mode,
        "documents": len(documents),
        "tokens": total_tokens,
        "terms": term_count,
        "postings": posting_count,
        "blocks": len(block_paths),
        "block_limit": block_limit,
        "dictionary_bytes": os.path.getsize(os.path.join(index_dir, "dictionary.txt")),
        "postings_bytes": os.path.getsize(os.path.join(index_dir, "postings.bin")),
        "avg_postings_per_term": round(posting_count / max(term_count, 1), 3),
        "time_seconds": round(time.time() - start, 3),
    }
    return stats


def main():
    text_dir = TEXT_DIR
    index_dir = INDEX_DIR
    mode = "stem"
    block_limit = 200000
    if len(sys.argv) > 2:
        text_dir = sys.argv[1]
        index_dir = sys.argv[2]
    if len(sys.argv) > 3:
        mode = sys.argv[3]
    if len(sys.argv) > 4:
        block_limit = int(sys.argv[4])
    stats = build_index(text_dir, index_dir, mode, block_limit)
    for key in stats:
        print(key + ":", stats[key])
    os.makedirs(RESULTS_DIR, exist_ok=True)
    f = open(os.path.join(RESULTS_DIR, "stats.json"), "w", encoding="utf-8")
    json.dump(stats, f, ensure_ascii=False, indent=2)
    f.close()


if __name__ == "__main__":
    main()
