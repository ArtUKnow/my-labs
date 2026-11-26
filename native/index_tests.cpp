#include "index.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <unistd.h>
#include <zlib.h>

namespace {
int failures = 0;
void expect(bool yes, const char *message) {
    if (!yes) {
        fprintf(stderr, "FAIL index: %s\n", message);
        ++failures;
    }
}
void gzip_text(const char *path, const char *text) {
    gzFile f = gzopen(path, "wb");
    expect(f != 0, "create fixture gzip");
    if (f) {
        expect(gzwrite(f, text, (unsigned)strlen(text)) == (int)strlen(text), "write fixture gzip");
        expect(gzclose(f) == Z_OK, "close fixture gzip");
    }
}
char *search_capture(const char *directory, const char *queries, int &status,
                     const char *offset = 0, const char *limit = 0) {
    FILE *input = tmpfile();
    FILE *output = tmpfile();
    if (!input || !output) {
        expect(false, "temporary search streams");
        return 0;
    }
    fputs(queries, input);
    rewind(input);
    fflush(stdout);
    int saved_in = dup(STDIN_FILENO), saved_out = dup(STDOUT_FILENO);
    dup2(fileno(input), STDIN_FILENO);
    dup2(fileno(output), STDOUT_FILENO);
    clearerr(stdin);
    char *args[8] = {(char *)"search", (char *)directory, (char *)"--json", 0, 0, 0, 0, 0};
    int count = 3;
    if (offset) {
        args[count++] = (char *)"--offset";
        args[count++] = (char *)offset;
    }
    if (limit) {
        args[count++] = (char *)"--limit";
        args[count++] = (char *)limit;
    }
    status = ir_search_main(count, args);
    fflush(stdout);
    dup2(saved_in, STDIN_FILENO);
    dup2(saved_out, STDOUT_FILENO);
    close(saved_in);
    close(saved_out);
    clearerr(stdin);
    fseek(output, 0, SEEK_END);
    long length = ftell(output);
    rewind(output);
    char *text = (char *)malloc((size_t)length + 1);
    if (text) {
        expect(fread(text, 1, (size_t)length, output) == (size_t)length, "read search output");
        text[length] = 0;
    }
    fclose(input);
    fclose(output);
    return text;
}
void check_query(const char *directory, const char *query, const char *expected) {
    int status = 0;
    char *t = search_capture(directory, query, status);
    expect(status == 0, "valid query exit code");
    expect(t && strstr(t, expected), expected);
    free(t);
}
} // namespace
int ir_index_tests() {
    char folder[] = "/tmp/ir-index-tests-XXXXXX";
    char *root = mkdtemp(folder);
    if (!root) {
        fprintf(stderr, "cannot create index test folder\n");
        return 1;
    }
    char manifest[4096], directory[4096];
    snprintf(manifest, sizeof manifest, "%s/documents.tsv", root);
    snprintf(directory, sizeof directory, "%s/index", root);
    FILE *f = fopen(manifest, "w");
    expect(f != 0, "manifest fixture");
    if (!f)
        return 1;
    fputs("id\tpath\turl\ttitle\tjournal\n", f);
    const char *texts[] = {"dark dark matter particles", "dark energy electron", "quark matter",
                           "electron quark ракеты has дети"};
    for (unsigned i = 0; i < 4; ++i) {
        char path[4096];
        snprintf(path, sizeof path, "%s/doc%u.txt.gz", root, i);
        gzip_text(path, texts[i]);
        fprintf(f, "%u\t%s\thttps://example.test/%u\tArticle \"%u\"\tTest journal\n", i, path, i,
                i);
    }
    fclose(f);
    char *args[] = {(char *)"build", manifest, directory, (char *)"raw", (char *)"3"};
    expect(ir_index_main(5, args) == 0, "build raw blocked index");
    check_query(directory, "dark matter\n", "\"ids\":[0]");
    check_query(directory, "[dark || quark] !electron\n", "\"ids\":[0,2]");
    check_query(directory, "dark || quark matter\n", "\"ids\":[0,1,2]");
    check_query(directory, "!!dark\n", "\"ids\":[0,1]");
    check_query(directory, "!!!dark\n", "\"ids\":[2,3]");
    check_query(directory, "ракеты\n", "\"ids\":[3]");
    check_query(directory, "missing\n", "\"ids\":[]");
    check_query(directory, "\n", "\"ids\":[]");
    int status = 0;
    char *t = search_capture(directory, "!missing\n", status, "1", "2");
    expect(status == 0 && t && strstr(t, "\"count\":4") && strstr(t, "\"ids\":[1,2]"),
           "paged results preserve full result count");
    expect(t && strstr(t, "Article \\\"1\\\""), "JSON escaping of metadata");
    free(t);
    t = search_capture(directory, "dark & matter\n(dark]\ndark ||\n()\ndark\n", status);
    expect(status == 1, "invalid query causes nonzero exit after batch");
    expect(t && strstr(t, "\"error\":") && strstr(t, "\"ids\":[0,1]"),
           "malformed query does not suppress following valid query");
    free(t);
    char deep[1100];
    memset(deep, '!', 600);
    memcpy(deep + 600, "dark\n", 6);
    t = search_capture(directory, deep, status);
    expect(status == 1 && t && strstr(t, "exceeds 256"), "limit adversarial recursive query depth");
    free(t);
    args[3] = (char *)"stem";
    expect(ir_index_main(5, args) == 0, "rebuild existing directory as stem index");
    check_query(directory, "particle\n", "\"ids\":[0]");
    check_query(directory, "ракета\n", "\"ids\":[3]");
    args[3] = (char *)"lemma";
    expect(ir_index_main(5, args) == 0, "build lemma index with lengthening exceptions");
    check_query(directory, "have\n", "\"ids\":[3]");
    check_query(directory, "ребенок\n", "\"ids\":[3]");
    const char *names[] = {"dictionary.bin", "postings.bin", "documents.bin", "meta.bin",
                           "stats.json"};
    for (unsigned i = 0; i < 5; ++i) {
        char path[4096];
        snprintf(path, sizeof path, "%s/%s", directory, names[i]);
        remove(path);
    }
    rmdir(directory);
    remove(manifest);
    for (unsigned i = 0; i < 4; ++i) {
        char path[4096];
        snprintf(path, sizeof path, "%s/doc%u.txt.gz", root, i);
        remove(path);
    }
    rmdir(root);
    fprintf(stderr, "native index tests: %s (%d failures)\n", failures ? "FAILED" : "OK", failures);
    return failures ? 1 : 0;
}
