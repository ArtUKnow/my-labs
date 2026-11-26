#include "index.h"
#include "text.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <zlib.h>

namespace {
#include "index_io.h"
#include "block_builder.h"
#include "normalization.h"
#include "document_input.h"
}

int ir_index_main(int argc, char **argv) {
    if (argc < 3 || argc > 5) {
        fprintf(stderr, "usage: build DOCUMENTS_TSV INDEX_DIR [raw|stem|lemma] "
                        "[block_token_limit]\n");
        return 2;
    }
    int mode = 1;
    if (argc >= 4) {
        if (!strcmp(argv[3], "raw"))
            mode = 0;
        else if (!strcmp(argv[3], "stem"))
            mode = 1;
        else if (!strcmp(argv[3], "lemma"))
            mode = 2;
        else {
            fprintf(stderr, "unknown normalization mode\n");
            return 2;
        }
    }
    uint64_t limit = 500000;
    if (argc >= 5) {
        char *end = 0;
        limit = strtoull(argv[4], &end, 10);
        if (!*argv[4] || *end || limit == 0) {
            fprintf(stderr, "invalid block limit\n");
            return 2;
        }
    }
    if (mkdir(argv[2], 0755) && errno != EEXIST)
        stop_with_error("cannot create index directory");
    FILE *manifest = fopen(argv[1], "r");
    if (!manifest)
        stop_with_error("cannot open document manifest");
    FILE *docs = open_file(argv[2], "documents.bin", "wb");
    header(docs, "IRDOCS01");
    put64(docs, 0);
    char *line = 0;
    size_t capacity = 0;
    ssize_t n = getline(&line, &capacity, manifest);
    if (n < 0 || strcmp(line, "id\tpath\turl\ttitle\tjournal\n"))
        stop_with_error("invalid document manifest header");
    WordMap map;
    BuildContext context = {&map, 0, mode, 0, 0};
    unsigned blocks = 0;
    uint64_t input_bytes = 0;
    struct timespec started, finished;
    clock_gettime(CLOCK_MONOTONIC, &started);
    while ((n = getline(&line, &capacity, manifest)) >= 0) {
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = 0;
        char *cursor = line;
        char *id = field(cursor), *path = field(cursor), *url = field(cursor),
             *title = field(cursor), *journal = field(cursor);
        if (!id || !path || !url || !title || !journal || cursor)
            stop_with_error("invalid document row");
        char *end = 0;
        unsigned long value = strtoul(id, &end, 10);
        if (!*id || *end || value != context.doc)
            stop_with_error("document ids must be contiguous from zero");
        put32(docs, context.doc);
        put_string(docs, title);
        put_string(docs, url);
        put_string(docs, journal);
        size_t length = 0;
        char *text = read_gzip(path, length);
        input_bytes += length;
        ir_tokenize(text, length, add_token, &context);
        free(text);
        ++context.doc;
        if (context.block_tokens >= limit) {
            write_block(map, argv[2], blocks++);
            context.block_tokens = 0;
        }
        if (context.doc % 5000 == 0)
            fprintf(stderr, "indexed %u documents\n", context.doc);
    }
    if (ferror(manifest))
        stop_with_error("manifest read failed");
    free(line);
    close_file(manifest);
    if (map.entries.length || !blocks)
        write_block(map, argv[2], blocks++);
    if (fseeko(docs, 12, SEEK_SET))
        stop_with_error("seek failed");
    put64(docs, context.doc);
    close_file(docs);
    uint64_t terms = 0, postings = 0, characters = 0;
    merge_blocks(argv[2], blocks, terms, postings, characters);
    FILE *meta = open_file(argv[2], "meta.bin", "wb");
    header(meta, "IRMETA01");
    put32(meta, (uint32_t)mode);
    put64(meta, context.doc);
    put64(meta, context.tokens);
    put64(meta, terms);
    put64(meta, postings);
    close_file(meta);
    clock_gettime(CLOCK_MONOTONIC, &finished);
    double seconds =
        (finished.tv_sec - started.tv_sec) + (finished.tv_nsec - started.tv_nsec) / 1e9;
    FILE *stats = open_file(argv[2], "stats.json", "w");
    fprintf(stats,
            "{\n  \"documents\": %u,\n  \"tokens\": %llu,\n  \"terms\": %llu,\n  "
            "\"postings\": %llu,\n "
            " \"blocks\": %u,\n  \"block_token_limit\": %llu,\n  "
            "\"input_bytes\": %llu,\n  "
            "\"time_seconds\": %.6f,\n  \"seconds_per_document\": %.9f,\n  "
            "\"kilobytes_per_second\": "
            "%.3f,\n  \"average_term_length\": %.6f,\n  \"mode\": \"%s\"\n}\n",
            context.doc, (unsigned long long)context.tokens, (unsigned long long)terms,
            (unsigned long long)postings, blocks, (unsigned long long)limit,
            (unsigned long long)input_bytes, seconds, seconds / (context.doc ? context.doc : 1),
            input_bytes / 1024.0 / (seconds ? seconds : 1),
            characters / (double)(terms ? terms : 1),
            mode == 0   ? "raw"
            : mode == 1 ? "stem"
                        : "lemma");
    close_file(stats);
    fprintf(stderr, "built %u documents, %llu terms, %llu postings in %.3f seconds\n", context.doc,
            (unsigned long long)terms, (unsigned long long)postings, seconds);
    return 0;
}
