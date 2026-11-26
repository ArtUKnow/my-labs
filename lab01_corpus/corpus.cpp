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

int ir_prepare_main(int argc, char **argv) {
    const char *input = argc > 1 ? argv[1] : "lab01_corpus/data/provenance/documents.tsv";
    const char *output = argc > 2 ? argv[2] : "lab01_corpus/data/native_documents.tsv";
    if (argc > 3) {
        fprintf(stderr, "usage: prepare [PORTABLE_TSV OUTPUT_TSV]\n");
        return 2;
    }
    FILE *source = fopen(input, "r");
    if (!source) {
        perror(input);
        return 1;
    }
    char temporary[PATH_MAX];
    if (snprintf(temporary, sizeof temporary, "%s.tmp", output) >= (int)sizeof temporary) {
        fclose(source);
        return 1;
    }
    FILE *target = fopen(temporary, "w");
    if (!target) {
        perror(temporary);
        fclose(source);
        return 1;
    }
    Row row;
    int count = 0, error = 0;
    char directory[PATH_MAX];
    if (!getcwd(directory, sizeof directory)) {
        fclose(source);
        fclose(target);
        return 1;
    }
    if (!row.read(source) || row.count != 5 || strcmp(row.fields[0], "id") ||
        strcmp(row.fields[1], "path")) {
        fprintf(stderr, "Invalid portable manifest header\n");
        error = 1;
    }
    fprintf(target, "id\tpath\turl\ttitle\tjournal\n");
    while (!error && row.read(source)) {
        if (row.count != 5) {
            error = 1;
            break;
        }
        char path[PATH_MAX];
        const char *relative = row.fields[1];
        const char *tail = !strncmp(relative, "lab1_corpus/", 12) ? relative + 12 : NULL;
        int written = tail ? snprintf(path, sizeof path, "%s/lab01_corpus/%s", directory, tail)
                      : relative[0] == '/'
                          ? snprintf(path, sizeof path, "%s", relative)
                          : snprintf(path, sizeof path, "%s/%s", directory, relative);
        struct stat info;
        if (written < 0 || written >= (int)sizeof path || stat(path, &info) != 0 ||
            !S_ISREG(info.st_mode)) {
            fprintf(stderr, "Missing corpus file: %s\n", row.fields[1]);
            error = 1;
            break;
        }
        char *end = NULL;
        long id = strtol(row.fields[0], &end, 10);
        if (*end || id != count) {
            fprintf(stderr, "Nonsequential document ID\n");
            error = 1;
            break;
        }
        fprintf(target, "%d\t%s\t%s\t%s\t%s\n", count, path, row.fields[2], row.fields[3],
                row.fields[4]);
        count++;
    }
    if (ferror(source) || ferror(target))
        error = 1;
    fclose(source);
    if (fclose(target))
        error = 1;
    if (error) {
        unlink(temporary);
        return 1;
    }
    if (rename(temporary, output)) {
        perror(output);
        return 1;
    }
    fprintf(stderr, "Prepared paths for %d documents\n", count);
    return 0;
}

int ir_corpus_tests() {
    const char *test = "\"a\"\"b\"";
    char *field = copy_field(test, strlen(test));
    clean_field(field);
    int error = strcmp(field, "a\"b") != 0;
    free(field);
    fprintf(stderr, "corpus field tests: %s\n", error ? "FAILED" : "OK");
    return error;
}
