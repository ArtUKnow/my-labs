"""Measure Boolean search over the full index after a warm-up query."""

from collections import Counter
import json
from pathlib import Path
import statistics
import time

from boolean_search import BooleanSearch
from catalog import DATA_DIR


CASES = [
    ("two_terms", "mars rover"),
    ("two_languages", "(марс || mars) (ракета || rocket)"),
    ("exclusion", "(космос || space) !марс"),
    ("nested", "((asteroid || астероид) (orbit || орбита)) || (спутник !луна)"),
    ("broad", "(the || и || в || a) !(space || космос)"),
]


def run(index_dir, repeats=7):
    started = time.perf_counter()
    engine = BooleanSearch(str(index_dir))
    startup_ms = (time.perf_counter() - started) * 1000
    document_count = engine.document_count
    rows = []
    try:
        for label, query in CASES:
            engine.search(query)
            timings = []
            ids = None
            for _ in range(repeats):
                started = time.perf_counter()
                ids = engine.search(query)
                timings.append((time.perf_counter() - started) * 1000)
            sources = Counter(engine.index.doc_names[doc_id].split("/", 1)[0] for doc_id in ids)
            rows.append({
                "case": label,
                "query": query,
                "documents": len(ids),
                "median_ms": round(statistics.median(timings), 3),
                "min_ms": round(min(timings), 3),
                "max_ms": round(max(timings), 3),
                "sources": dict(sorted(sources.items())),
            })
    finally:
        engine.close()
    return {"index_documents": document_count, "startup_ms": round(startup_ms, 3), "repeats": repeats, "cache": "warm", "cases": rows}


def main():
    result = run(DATA_DIR / "index")
    results_dir = Path(__file__).resolve().parent / "results"
    results_dir.mkdir(exist_ok=True)
    (results_dir / "benchmark.json").write_text(json.dumps(result, ensure_ascii=False, indent=2), encoding="utf-8")
    lines = ["Индекс: %d документов; загрузка: %.3f мс" % (result["index_documents"], result["startup_ms"]), "Поиск после прогрева, %d повторов:" % result["repeats"]]
    for row in result["cases"]:
        lines.append("%s | %d документов | медиана %.3f мс | максимум %.3f мс | %s" % (row["case"], row["documents"], row["median_ms"], row["max_ms"], row["query"]))
    report = "\n".join(lines) + "\n"
    (results_dir / "benchmark.txt").write_text(report, encoding="utf-8")
    print(report, end="")


if __name__ == "__main__":
    main()
