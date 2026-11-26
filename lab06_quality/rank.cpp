#include "rank.h"
#include "text.h"
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <zlib.h>

namespace {
#include "rank_structures.h"
#include "rank_queries.h"
#include "rank_documents.h"
#include "rank_scores.h"
}

int ir_rank_main(int argc, char **argv) {
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "usage: rank DOCUMENTS_TSV QUERIES_TSV [stem|raw|lemma]\n");
        return 2;
    }
    const char *mode = argc == 4 ? argv[3] : "stem";
    if (strcmp(mode, "stem") && strcmp(mode, "raw") && strcmp(mode, "lemma")) {
        fprintf(stderr, "rank: unknown normalization %s\n", mode);
        return 2;
    }
    Queries queries;
    Map terms;
    if (!read_queries(argv[2], mode, queries, terms))
        return 1;
    Documents docs;
    uint32_t *df = (uint32_t *)checked(calloc(terms.used ? terms.used : 1, sizeof(uint32_t)));
    if (!read_documents(argv[1], mode, terms, docs, df)) {
        free(df);
        return 1;
    }
    evaluate(queries, docs, df);
    free(df);
    return ferror(stdout) ? 1 : 0;
}
