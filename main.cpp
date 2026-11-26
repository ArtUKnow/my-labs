#include "corpus.h"
#include "crawler.h"
#include "demo.h"
#include "index.h"
#include "inspection.h"
#include "server.h"
#include "text.h"
#include <stdio.h>
#include <string.h>
int main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: ir "
                        "prepare|corpus-stats|crawl|tokenize|stem|lemma|text-stats|zipf|build|"
                        "search|serve|"
                        "selftest ...\n");
        return 2;
    }
    const char *command = argv[1];
    if (!strcmp(command, "inspect"))
        return web::ir_inspection_main(argc - 1, argv + 1);
    if (!strcmp(command, "test-crawler"))
        return ir_crawler_tests();
    if (!strcmp(command, "test-tokenizer"))
        return ir_tokenizer_tests();
    if (!strcmp(command, "test-stemmer"))
        return ir_stemmer_tests();
    if (!strcmp(command, "demo"))
        return ir_demo_main(argc - 1, argv + 1);
    if (!strcmp(command, "prepare"))
        return ir_prepare_main(argc - 1, argv + 1);
    if (!strcmp(command, "corpus-stats"))
        return ir_corpus_main(argc - 1, argv + 1);
    if (!strcmp(command, "crawl"))
        return ir_crawler_main(argc - 1, argv + 1);
    if (!strcmp(command, "serve"))
        return ir_server_main(argc - 1, argv + 1);
    if (!strcmp(command, "build"))
        return ir_index_main(argc - 1, argv + 1);
    if (!strcmp(command, "search"))
        return ir_search_main(argc - 1, argv + 1);
    if (!strcmp(command, "selftest")) {
        int errors = ir_text_tests() + ir_index_tests() +
                     ir_corpus_tests() + ir_crawler_tests() + ir_server_tests();
        fprintf(stderr, "native selftests: %s (%d failures)\n", errors ? "FAILED" : "OK", errors);
        return errors ? 1 : 0;
    }
    return ir_text_main(argc - 1, argv + 1);
}
