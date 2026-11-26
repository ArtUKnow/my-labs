#include "../common/text.h"
#include "../common/text_support.h"

namespace {
#include "statistics.h"
void print_token(const char *t, size_t n, void *) {
    fwrite(t, 1, n, stdout);
    fputc('\n', stdout);
}
}
int ir_tokenizer_main(int argc, char **argv) {
    if (argc == 1 && !strcmp(argv[0], "tokenize")) {
        Buffer input;
        char bytes[65536];
        size_t n;
        while ((n = fread(bytes, 1, sizeof(bytes), stdin)))
            input.append(bytes, n);
        if (ferror(stdin)) {
            perror("stdin");
            return 1;
        }
        ir_tokenize(input.data ? input.data : "", input.size, print_token, NULL);
        return ferror(stdout) ? 1 : 0;
    }
    if (argc == 3 && !strcmp(argv[0], "text-stats")) {
        Stats stats;
        double start = now();
        if (!corpus(argv[1], stats))
            return 1;
        return write_stats(argv[2], stats, now() - start);
    }
    fprintf(stderr, "usage: tokenize | text-stats DOCUMENTS_TSV OUTPUT_JSON\n");
    return 2;
}
