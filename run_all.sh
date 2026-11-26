#!/bin/bash
set -euo pipefail
cd "$(dirname "$0")"
case "${1:-tests}" in
  build)
    python3 scripts/build_native_results.py
    ;;
  crawl)
    python3 lab2_crawler/crawler.py lab2_crawler/config.yaml
    ;;
  tests)
    make -j2
    mkdir -p lab8_boolean_search/results
    ./build/ir selftest 2>&1 | tee lab8_boolean_search/results/native_selftest.txt
    python3 scripts/test_native_pipeline.py -v 2>&1 | tee lab8_boolean_search/results/native_integration_tests.txt
    python3 scripts/test_corpus_pipeline.py -v 2>&1 | tee lab2_crawler/results/native_crawler_tests.txt
    python3 scripts/prepare_corpus.py
    ;;
  reports)
    PYTHONPATH="../pdfdeps${PYTHONPATH:+:$PYTHONPATH}" python3 scripts/generate_reports.py
    ;;
  legacy-tests)
    for lab in lab1_corpus lab2_crawler lab3_tokenizer lab4_stemmer lab5_zipf lab6_search_quality lab7_boolean_index lab8_boolean_search; do
      python3 "$lab/test_${lab%%_*}.py"
    done
    ;;
  *)
    echo "usage: $0 [tests|build|crawl|reports|legacy-tests]" >&2
    exit 2
    ;;
esac
