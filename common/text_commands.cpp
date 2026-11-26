#include "text.h"
#include <stdio.h>
#include <string.h>

int ir_text_main(int argc, char **argv) {
    if (argc < 1)
        return 2;
    if (!strcmp(argv[0], "tokenize") || !strcmp(argv[0], "text-stats"))
        return ir_tokenizer_main(argc, argv);
    if (!strcmp(argv[0], "stem") || !strcmp(argv[0], "lemma"))
        return ir_stem_main(argc, argv);
    if (!strcmp(argv[0], "zipf"))
        return ir_zipf_main(argc, argv);
    fprintf(stderr, "usage: tokenize | stem | lemma | text-stats | zipf\n");
    return 2;
}
