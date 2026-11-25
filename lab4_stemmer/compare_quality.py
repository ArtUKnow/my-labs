import os
import sys
import json

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from search import SimpleSearch, load_documents, TEXT_DIR, TOKENS_DIR
from lemmatizer import load_vocabulary


BASE_DIR = os.path.dirname(os.path.abspath(__file__))
QUERIES_PATH = os.path.join(BASE_DIR, "..", "lab1_corpus", "data", "queries", "queries.txt")
QRELS_PATH = os.path.join(BASE_DIR, "..", "lab1_corpus", "data", "queries", "qrels.txt")
RESULTS_DIR = os.path.join(BASE_DIR, "results")


def load_queries(path):
    queries = []
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if line == "":
            continue
        query_id, text = line.split("\t")
        queries.append((query_id, text))
    return queries


def load_qrels(path):
    qrels = {}
    for line in open(path, encoding="utf-8"):
        line = line.strip()
        if line == "":
            continue
        query_id, doc_id, grade = line.split("\t")
        if query_id not in qrels:
            qrels[query_id] = {}
        qrels[query_id][doc_id] = int(grade)
    return qrels


def precision_at_k(results, relevant, k):
    hits = 0
    for doc_id in results[:k]:
        if relevant.get(doc_id, 0) > 0:
            hits += 1
    return hits / k


def recall_at_k(results, relevant, k):
    total = 0
    for doc_id in relevant:
        if relevant[doc_id] > 0:
            total += 1
    if total == 0:
        return 0
    hits = 0
    for doc_id in results[:k]:
        if relevant.get(doc_id, 0) > 0:
            hits += 1
    return hits / total


def evaluate_mode(mode, documents, vocabulary, queries, qrels, k):
    engine = SimpleSearch(documents, mode, vocabulary)
    per_query = []
    sum_p = 0
    sum_r = 0
    for query_id, text in queries:
        results = engine.search(text, k)
        relevant = qrels.get(query_id, {})
        p = precision_at_k(results, relevant, k)
        r = recall_at_k(results, relevant, k)
        sum_p += p
        sum_r += r
        per_query.append({"query_id": query_id, "query": text, "precision": round(p, 3), "recall": round(r, 3), "results": results})
    return {
        "mode": mode,
        "terms_in_index": len(engine.index),
        "avg_precision": round(sum_p / len(queries), 4),
        "avg_recall": round(sum_r / len(queries), 4),
        "queries": per_query,
    }


def main():
    k = 5
    documents = load_documents(TEXT_DIR)
    vocabulary = load_vocabulary(TOKENS_DIR)
    queries = load_queries(QUERIES_PATH)
    qrels = load_qrels(QRELS_PATH)
    report = {"k": k, "modes": []}
    for mode in ["raw", "stem", "lemma"]:
        result = evaluate_mode(mode, documents, vocabulary, queries, qrels, k)
        report["modes"].append(result)
        print("mode:", mode)
        print("  terms in index:", result["terms_in_index"])
        print("  P@%d: %.3f" % (k, result["avg_precision"]))
        print("  R@%d: %.3f" % (k, result["avg_recall"]))
    print("per query precision:")
    for i in range(len(queries)):
        row = queries[i][1]
        for mode_result in report["modes"]:
            row += "\t" + str(mode_result["queries"][i]["precision"])
        print("  " + row)
    os.makedirs(RESULTS_DIR, exist_ok=True)
    f = open(os.path.join(RESULTS_DIR, "quality.json"), "w", encoding="utf-8")
    json.dump(report, f, ensure_ascii=False, indent=2)
    f.close()


if __name__ == "__main__":
    main()
