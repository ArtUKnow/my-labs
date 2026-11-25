#!/bin/bash
cd "$(dirname "$0")"

MODE=${1:-tests}

LABS="lab01_corpus lab02_crawler lab03_tokenizer lab04_stemmer lab05_zipf lab06_quality lab07_boolean_index"

if [ "$MODE" = "build" ]; then
  echo "=== lab01: extract text and stats"
  python3 lab01_corpus/extract_text.py
  python3 lab01_corpus/corpus_stats.py
  echo "=== lab03: tokenize corpus"
  python3 lab03_tokenizer/tokenize_corpus.py
  echo "=== lab04: compare quality"
  python3 lab04_stemmer/compare_quality.py
  echo "=== lab05: zipf"
  python3 lab05_zipf/zipf.py
  python3 lab05_zipf/plot_svg.py
  echo "=== lab06: evaluate"
  python3 lab06_quality/evaluate.py
  echo "=== lab07: build index"
  python3 lab07_boolean_index/build_index.py data/text data/index stem 10000
  exit 0
fi

FAILED=0
for LAB in $LABS; do
  echo "=================== $LAB ==================="
  mkdir -p "$LAB/results"
  python3 "$LAB/test_${LAB:0:5}.py" > "$LAB/results/test_report.txt" 2>&1
  CODE=$?
  grep -E "^(Ran|OK|FAILED)" "$LAB/results/test_report.txt"
  if [ $CODE -ne 0 ]; then
    FAILED=1
  fi
done

if [ $FAILED -ne 0 ]; then
  echo "SOME TESTS FAILED"
  exit 1
fi
echo "ALL TESTS PASSED"
