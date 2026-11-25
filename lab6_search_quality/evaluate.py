import os
import sys
import json

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lab4_stemmer"))

import metrics


BASE_DIR = os.path.dirname(os.path.abspath(__file__))
QUERIES_PATH = os.path.join(BASE_DIR, "..", "lab1_corpus", "data", "queries", "queries.txt")
QRELS_PATH = os.path.join(BASE_DIR, "..", "lab1_corpus", "data", "queries", "qrels.txt")
WIKI_PATH = os.path.join(BASE_DIR, "..", "lab1_corpus", "data", "queries", "wikipedia_results.txt")
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


def load_run(lines):
    run = {}
    for line in lines:
        line = line.strip()
        if line == "":
            continue
        parts = line.split("\t")
        query_id = parts[0]
        doc_id = parts[1]
        rank = int(parts[2])
        if query_id not in run:
            run[query_id] = []
        run[query_id].append((rank, doc_id))
    for query_id in run:
        run[query_id].sort()
        run[query_id] = [doc_id for rank, doc_id in run[query_id]]
    return run


def evaluate_run(run, queries, qrels, k):
    rows = []
    per_query = []
    for query_id, text in queries:
        results = run.get(query_id, [])
        grades = qrels.get(query_id, {})
        row = metrics.all_metrics(results, grades, k)
        rows.append(row)
        per_query.append({"query_id": query_id, "query": text, "metrics": row, "results": results[:k]})
    return {"average": metrics.average_metrics(rows), "queries": per_query}


def run_from_engine(engine, queries, k):
    run = {}
    for query_id, text in queries:
        run[query_id] = engine.search(text, k)
    return run


def print_table(name, result, k):
    avg = result["average"]
    print("%-12s P@%d=%.3f R@%d=%.3f MAP=%.3f DCG=%.3f NDCG=%.3f ERR=%.3f" % (name, k, avg["p"], k, avg["r"], avg["ap"], avg["dcg"], avg["ndcg"], avg["err"]))


def main():
    k = 5
    queries = load_queries(QUERIES_PATH)
    qrels = load_qrels(QRELS_PATH)
    if len(sys.argv) > 1 and sys.argv[1] == "--stdin":
        run = load_run(sys.stdin)
        result = evaluate_run(run, queries, qrels, k)
        print_table("stdin", result, k)
        for item in result["queries"]:
            m = item["metrics"]
            print("  %s\tP=%.2f\tNDCG=%.3f\tERR=%.3f" % (item["query_id"], m["p"], m["ndcg"], m["err"]))
        return

    from search import SimpleSearch, load_documents, TEXT_DIR, TOKENS_DIR
    from lemmatizer import load_vocabulary

    labeled_ids = {doc_id for grades in qrels.values() for doc_id in grades}
    documents = load_documents(TEXT_DIR, labeled_ids)
    vocabulary = load_vocabulary(TOKENS_DIR, labeled_ids)
    report = {"k": k, "evaluation_documents": len(documents), "systems": []}
    systems = []
    if os.path.exists(WIKI_PATH):
        systems.append(("wikipedia", load_run(open(WIKI_PATH, encoding="utf-8"))))
    for mode in ["raw", "stem", "lemma"]:
        engine = SimpleSearch(documents, mode, vocabulary)
        systems.append((mode, run_from_engine(engine, queries, k)))
    for name, run in systems:
        result = evaluate_run(run, queries, qrels, k)
        result["name"] = name
        report["systems"].append(result)
        print_table(name, result, k)
    print("NDCG per query:")
    for i in range(len(queries)):
        row = "  " + queries[i][1]
        for system in report["systems"]:
            row += "\t%s=%.3f" % (system["name"], system["queries"][i]["metrics"]["ndcg"])
        print(row)
    os.makedirs(RESULTS_DIR, exist_ok=True)
    f = open(os.path.join(RESULTS_DIR, "metrics.json"), "w", encoding="utf-8")
    json.dump(report, f, ensure_ascii=False, indent=2)
    f.close()


if __name__ == "__main__":
    main()
