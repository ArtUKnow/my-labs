#!/bin/bash
cd "$(dirname "$0")"

MODE=${1:-tests}

LABS="lab1_corpus lab2_crawler lab3_tokenizer lab4_stemmer lab5_zipf lab6_search_quality lab7_boolean_index"

if [ "$MODE" = "build" ]; then
  echo "=== lab1: extract text and stats"
  python3 lab1_corpus/extract_text.py
  python3 lab1_corpus/corpus_stats.py
  echo "=== lab3: tokenize corpus"
  python3 lab3_tokenizer/tokenize_corpus.py
  echo "=== lab4: compare quality"
  python3 lab4_stemmer/compare_quality.py
  echo "=== lab5: zipf"
  python3 lab5_zipf/zipf.py
  python3 lab5_zipf/plot_svg.py
  echo "=== lab6: evaluate"
  python3 lab6_search_quality/evaluate.py
  echo "=== lab7: build index"
  python3 lab7_boolean_index/build_index.py lab1_corpus/data/text lab1_corpus/data/index stem 10000
  exit 0
fi

FAILED=0
for LAB in $LABS; do
  echo "=================== $LAB ==================="
  mkdir -p "$LAB/results"
  python3 "$LAB/test_${LAB%%_*}.py" > "$LAB/results/test_report.txt" 2>&1
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
