#include "../common/text.h"
#include "../common/text_support.h"

namespace {
#include "russian_stem.h"
#include "english_stem.h"
}
void ir_stem(char *token) {
    to_lowercase(token);
    bool ru = false;
    size_t n = strlen(token), w;
    for (size_t i = 0; i < n; i += w) {
        uint32_t c = read_utf8(token + i, n - i, &w);
        if (c >= 0x430 && c <= 0x44f) {
            ru = true;
            break;
        }
    }
    if (ru)
        stem_russian(token);
    else
        stem_english(token);
}
int ir_stem_main(int argc, char **argv) {
    if (argc < 1)
        return 2;
    if (!strcmp(argv[0], "stem") || !strcmp(argv[0], "lemma")) {
        char *line = NULL;
        size_t cap = 0;
        ssize_t n;
        while ((n = getline(&line, &cap, stdin)) >= 0) {
            while (n && strchr("\r\n \t", line[n - 1]))
                line[--n] = 0;
            char *start = line;
            while (*start == ' ' || *start == '\t')
                start++;
            if (!*start)
                continue;
            if (!strcmp(argv[0], "stem")) {
                ir_stem(start);
                puts(start);
            } else {
                Buffer result;
                result.reserve(strlen(start) + 64);
                if (!ir_lemma_copy(start, result.data, result.capacity)) {
                    fprintf(stderr, "lemma output buffer too small\n");
                    free(line);
                    return 1;
                }
                puts(result.data);
            }
        }
        free(line);
        return ferror(stdin) || ferror(stdout) ? 1 : 0;
    }
    fprintf(stderr, "usage: stem | lemma\n");
    return 2;
}
