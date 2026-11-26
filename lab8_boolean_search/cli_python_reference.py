"""Read one Boolean query per UTF-8 input line and print matching documents."""

import argparse
import json
import sys
import time

from boolean_search import BooleanSearch, QuerySyntaxError
from catalog import DATA_DIR, DocumentCatalog


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--index-dir", default=str(DATA_DIR / "index"))
    parser.add_argument("--data-dir", default=str(DATA_DIR))
    parser.add_argument("--limit", type=int, default=50)
    parser.add_argument("--offset", type=int, default=0)
    parser.add_argument("--all", action="store_true", help="print every matching document")
    parser.add_argument("--jsonl", action="store_true", help="one JSON object per input query")
    args = parser.parse_args()
    if args.limit < 1 or args.offset < 0:
        parser.error("--limit must be positive and --offset must be nonnegative")
    engine = BooleanSearch(args.index_dir)
    failed = False
    try:
        catalog = DocumentCatalog(args.data_dir, engine.index.doc_names)
        for line in sys.stdin:
            query = line.strip()
            if not query:
                continue
            start = time.perf_counter()
            try:
                ids = engine.search(query)
            except QuerySyntaxError as error:
                failed = True
                if args.jsonl:
                    print(json.dumps({"query": query, "error": str(error)}, ensure_ascii=False))
                else:
                    print("Запрос:", query)
                    print("  Ошибка:", error)
                continue
            elapsed_ms = round((time.perf_counter() - start) * 1000, 3)
            limit = len(ids) if args.all else args.limit
            selected = ids[args.offset:args.offset + limit]
            documents = [catalog.get(doc_num) for doc_num in selected]
            if args.jsonl:
                print(json.dumps({"query": query, "count": len(ids), "offset": args.offset, "elapsed_ms": elapsed_ms, "documents": documents}, ensure_ascii=False))
            else:
                print("Запрос:", query)
                print("Найдено:", len(ids), "·", elapsed_ms, "мс")
                for rank, document in enumerate(documents, args.offset + 1):
                    print("%d. %s [%s]" % (rank, document["title"], document["source_label"]))
                    print("   " + document["url"])
                if len(ids) > args.offset + limit:
                    print("Следующие результаты: --offset", args.offset + limit)
    finally:
        engine.close()
    return 1 if failed else 0


if __name__ == "__main__":
    sys.exit(main())
