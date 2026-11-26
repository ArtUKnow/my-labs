#include "corpus.h"
#include <errno.h>
#include <limits.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

#include "table.h"

using namespace corpus_table;

int ir_corpus_main(int argc, char **argv) {
    if (argc != 4) {
        fprintf(stderr, "usage: corpus-stats SOURCE_MANIFEST TEXT_DIRECTORY OUTPUT_JSON\n");
        return 2;
    }
    FILE *input = fopen(argv[1], "r");
    if (!input) {
        perror(argv[1]);
        return 1;
    }
    Row row;
    if (!row.read(input)) {
        fclose(input);
        return 1;
    }
    int file_column = row.column("file"), raw_column = row.column("source_bytes"),
        words_column = row.column("words");
    if (file_column < 0 || raw_column < 0 || words_column < 0) {
        fprintf(stderr, "Missing corpus metadata fields\n");
        fclose(input);
        return 1;
    }
    uint64_t documents = 0, raw_bytes = 0, text_bytes = 0, stored_bytes = 0, words = 0;
    int error = 0;
    while (row.read(input)) {
        if (row.count <= file_column || row.count <= raw_column || row.count <= words_column) {
            error = 1;
            break;
        }
        char path[PATH_MAX];
        if (snprintf(path, sizeof path, "%s/%s", argv[2], row.fields[file_column]) >=
            (int)sizeof path) {
            error = 1;
            break;
        }
        struct stat info;
        if (stat(path, &info)) {
            perror(path);
            error = 1;
            break;
        }
        gzFile file = gzopen(path, "rb");
        if (!file) {
            error = 1;
            break;
        }
        char buffer[65536];
        int size;
        while ((size = gzread(file, buffer, sizeof buffer)) > 0)
            text_bytes += (uint64_t)size;
        if (size < 0)
            error = 1;
        if (gzclose(file) != Z_OK)
            error = 1;
        if (error)
            break;
        documents++;
        raw_bytes += strtoull(row.fields[raw_column], NULL, 10);
        words += strtoull(row.fields[words_column], NULL, 10);
        stored_bytes += (uint64_t)info.st_size;
    }
    fclose(input);
    if (error)
        return 1;
    FILE *output = fopen(argv[3], "w");
    if (!output) {
        perror(argv[3]);
        return 1;
    }
    fprintf(output,
            "{\n  \"implementation\":\"C++ without STL\",\n  \"documents\":%llu,\n  "
            "\"raw_bytes\":%llu,\n  \"text_bytes\":%llu,\n  \"stored_bytes\":%llu,\n  "
            "\"words\":%llu,\n  \"avg_raw_bytes\":%.3f,\n  \"avg_text_bytes\":%.3f,\n  "
            "\"avg_words\":%.3f\n}\n",
            (unsigned long long)documents, (unsigned long long)raw_bytes,
            (unsigned long long)text_bytes, (unsigned long long)stored_bytes,
            (unsigned long long)words, documents ? (double)raw_bytes / documents : 0,
            documents ? (double)text_bytes / documents : 0,
            documents ? (double)words / documents : 0);
    return fclose(output) ? 1 : 0;
}
