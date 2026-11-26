#include "index.h"
#include "rank.h"
#include "text.h"
#include <stdio.h>
#include <string.h>
int ir_metrics_main(int argc, char **argv);
int ir_metrics_tests();
int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: ir "
                        "tokenize|stem|lemma|text-stats|zipf|build|search|metrics|"
                        "selftest ...\n");
        return 2;
    }
    const char *command = argv[1];
    if (!strcmp(command, "build"))
        return ir_index_main(argc - 1, argv + 1);
    if (!strcmp(command, "search"))
        return ir_search_main(argc - 1, argv + 1);
    if (!strcmp(command, "metrics"))
        return ir_metrics_main(argc - 1, argv + 1);
    if (!strcmp(command, "rank"))
        return ir_rank_main(argc - 1, argv + 1);
    if (!strcmp(command, "selftest")) {
        int errors = ir_text_tests() + ir_index_tests() + ir_metrics_tests() + ir_rank_tests();
        fprintf(stderr, "native selftests: %s (%d failures)\n", errors ? "FAILED" : "OK", errors);
        return errors ? 1 : 0;
    }
    return ir_text_main(argc - 1, argv + 1);
}
