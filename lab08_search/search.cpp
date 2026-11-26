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
#include "../lab07_index/index_io.h"
#include "../lab07_index/normalization.h"
#include "../lab07_index/index_reader.h"
#include "query_parser.h"
}

int ir_search_main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: search INDEX_DIR [--json] [--offset N] [--limit N]\n");
        return 2;
    }
    bool json = false;
    uint64_t offset = 0, limit = UINT64_MAX;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--json"))
            json = true;
        else if ((!strcmp(argv[i], "--offset") || !strcmp(argv[i], "--limit")) && i + 1 < argc) {
            bool off = !strcmp(argv[i], "--offset");
            char *end = 0;
            const char *value = argv[++i];
            uint64_t number = strtoull(value, &end, 10);
            if (!*value || *value == '-' || *end) {
                fprintf(stderr, "invalid pagination value\n");
                return 2;
            }
            if (off)
                offset = number;
            else
                limit = number;
        } else {
            fprintf(stderr, "unknown search option\n");
            return 2;
        }
    }
    Index index(argv[1]);
    char *line = 0;
    size_t capacity = 0;
    ssize_t n;
    int status = 0;
    while ((n = getline(&line, &capacity, stdin)) >= 0) {
        timespec query_started, query_finished;
        clock_gettime(CLOCK_MONOTONIC, &query_started);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = 0;
        Array<QueryWord> tokens;
        const char *error = 0;
        Array<uint32_t> *results = 0;
        if (read_query_words(line, tokens, error)) {
            Parser parser(index, tokens);
            results = parser.parse();
            error = parser.error;
        }
        if (error) {
            status = 1;
            if (json) {
                fputs("{\"query\":", stdout);
                json_string(stdout, line);
                fputs(",\"error\":", stdout);
                json_string(stdout, error);
                fputs("}\n", stdout);
            } else
                fprintf(stderr, "query error: %s\n", error);
        } else {
            clock_gettime(CLOCK_MONOTONIC, &query_finished);
            double elapsed_ms = (query_finished.tv_sec - query_started.tv_sec) * 1000.0 +
                                (query_finished.tv_nsec - query_started.tv_nsec) / 1000000.0;
            size_t start = offset < results->length ? (size_t)offset : results->length;
            size_t end = results->length;
            if (limit < (uint64_t)(end - start))
                end = start + (size_t)limit;
            if (json) {
                fputs("{\"query\":", stdout);
                json_string(stdout, line);
                fprintf(stdout,
                        ",\"count\":%zu,\"elapsed_ms\":%.6f,\"offset\":%llu,\"limit\":%"
                        "llu,\"ids\":[",
                        results->length, elapsed_ms, (unsigned long long)offset,
                        (unsigned long long)limit);
                for (size_t i = start; i < end; ++i) {
                    if (i > start)
                        fputc(',', stdout);
                    fprintf(stdout, "%u", results->items[i]);
                }
                fputs("],\"documents\":[", stdout);
                for (size_t i = start; i < end; ++i) {
                    if (i > start)
                        fputc(',', stdout);
                    uint32_t id = results->items[i];
                    Document &doc = index.docs.items[id];
                    fprintf(stdout, "{\"id\":%u,\"title\":", id);
                    json_string(stdout, doc.title);
                    fputs(",\"url\":", stdout);
                    json_string(stdout, doc.url);
                    fputs(",\"journal\":", stdout);
                    json_string(stdout, doc.journal);
                    fputc('}', stdout);
                }
                fputs("]}\n", stdout);
            } else {
                fprintf(stdout, "query: %s -> %zu documents\n", line, results->length);
                for (size_t i = start; i < end; ++i) {
                    uint32_t id = results->items[i];
                    Document &doc = index.docs.items[id];
                    fprintf(stdout, "%u\t%s\t%s\n", id, doc.title, doc.url);
                }
            }
        }
        delete results;
        for (size_t i = 0; i < tokens.length; ++i)
            free(tokens.items[i].text);
    }
    free(line);
    if (ferror(stdin) || ferror(stdout))
        stop_with_error("query input/output failed");
    return status;
}
