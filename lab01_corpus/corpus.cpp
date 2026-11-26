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

namespace {
char *copy_field(const char *start, size_t length) {
    char *result = (char *)malloc(length + 1);
    if (!result) {
        perror("memory");
        exit(1);
    }
    memcpy(result, start, length);
    result[length] = 0;
    return result;
}
struct Row {
    char *fields[32];
    int count;
    Row() : count(0) {
    }
    ~Row() {
        clear();
    }
    void clear() {
        for (int i = 0; i < count; i++)
            free(fields[i]);
        count = 0;
    }
    bool read(FILE *file) {
        clear();
        char *line = NULL;
        size_t capacity = 0;
        ssize_t length = getline(&line, &capacity, file);
        if (length < 0) {
            free(line);
            return false;
        }
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
            line[--length] = 0;
        char *current = line;
        while (count < 32) {
            char *end = strchr(current, '\t');
            size_t size = end ? (size_t)(end - current) : strlen(current);
            fields[count++] = copy_field(current, size);
            if (!end)
                break;
            current = end + 1;
        }
        free(line);
        return true;
    }
    int column(const char *name) {
        for (int i = 0; i < count; i++)
            if (!strcmp(fields[i], name))
                return i;
        return -1;
    }
};
void clean_field(char *field) {
    size_t length = strlen(field);
    if (length >= 2 && field[0] == '"' && field[length - 1] == '"') {
        size_t output = 0;
        for (size_t i = 1; i < length - 1; i++) {
            if (field[i] == '"' && i + 1 < length - 1 && field[i + 1] == '"')
                i++;
            field[output++] = field[i];
        }
        field[output] = 0;
    }
}
}

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
int ir_corpus_tests() {
    const char *test = "\"a\"\"b\"";
    char *field = copy_field(test, strlen(test));
    clean_field(field);
    int error = strcmp(field, "a\"b") != 0;
    free(field);
    fprintf(stderr, "corpus field tests: %s\n", error ? "FAILED" : "OK");
    return error;
}
