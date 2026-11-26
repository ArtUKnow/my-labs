#!/bin/sh
set -eu
cd "$(dirname "$0")"
case "${1:-help}" in
  build)
    make -j4
    ;;
  test)
    make -j4
    ./build/ir selftest
    ;;
  search)
    shift
    if [ "$#" -gt 0 ]; then
      printf '%s\n' "$*" | ./build/ir search lab01_corpus/data/native_index --json --limit 50
    else
      ./build/ir search lab01_corpus/data/native_index --json --limit 50
    fi
    ;;
  web)
    ./build/ir serve --index-dir lab01_corpus/data/native_index --port 8000
    ;;
  prepare)
    ./build/ir prepare
    ;;
  restore-index)
    mkdir -p lab01_corpus/data/native_index
    (cd lab07_index/results && shasum -a 256 -c native_index.tar.gz.sha256)
    tar -xzf lab07_index/results/native_index.tar.gz -C lab01_corpus/data/native_index
    ;;
  download-raw)
    if [ -e lab02_crawler/data/scoap3_raw.sqlite ]; then
      printf '%s\n' 'Raw database already exists.' >&2
      exit 1
    fi
    mkdir -p lab02_crawler/data/raw_parts
    gh release download cpp-labs-v1 --repo ArtUKnow/my-labs --pattern 'scoap3_raw.sqlite.part*' --pattern SHA256SUMS --dir lab02_crawler/data/raw_parts --skip-existing
    (cd lab02_crawler/data/raw_parts && shasum -a 256 -c SHA256SUMS)
    cat lab02_crawler/data/raw_parts/scoap3_raw.sqlite.part* > lab02_crawler/data/scoap3_raw.sqlite.tmp
    mv lab02_crawler/data/scoap3_raw.sqlite.tmp lab02_crawler/data/scoap3_raw.sqlite
    ;;
  *)
    printf '%s\n' './run.sh build' './run.sh test' './run.sh search dark matter' './run.sh web' './run.sh prepare' './run.sh restore-index' './run.sh download-raw'
    ;;
esac
