#include "../common/text.h"
#include "../common/text_support.h"

namespace {
#include "frequency_table.h"
#include "corpus_count.h"
#include "frequency_sort.h"
#include "fitting.h"
#include "plot.h"
int zipf(const char *manifest, const char *output, const char *svg) {
    Stats stats;
    Frequencies freq;
    stats.frequencies = &freq;
    double begin = now();
    if (!corpus(manifest, stats))
        return 1;
    Entry **ranked = (Entry **)checked(malloc(freq.used * sizeof(Entry *)));
    Entry **temp = (Entry **)checked(malloc(freq.used * sizeof(Entry *)));
    size_t n = 0;
    uint64_t hapax = 0;
    for (size_t i = 0; i < freq.capacity; i++)
        if (freq.slots[i].key) {
            ranked[n++] = &freq.slots[i];
            if (freq.slots[i].count == 1)
                hapax++;
        }
    merge_entries(ranked, temp, 0, n);
    free(temp);
    ZipfFit fit = fit_distribution(ranked, n);
    double exponent = fit.exponent, c = fit.c, classical = fit.classical, error = fit.error;
    FILE *f = fopen(output, "w");
    if (!f) {
        perror(output);
        free(ranked);
        return 1;
    }
    fprintf(f,
            "{\n\"implementation\":\"C++ custom hash table and merge "
            "sort\",\n\"documents\":%llu,\n\"total_tokens\":%llu,\n\"unique_terms\":%"
            "zu,\n\"hapax_legomena\":%llu,\n\"hapax_share\":%.8f,\n\"time_seconds\":%"
            ".6f,\n\"zipf_classical\":{\"c\":%.8f,\"s\":1},\n\"zipf_fit\":{\"c\":%."
            "8f,\"s\":%.8f,\"log_mse\":%.8f},\n\"top_terms\":[",
            (unsigned long long)stats.documents, (unsigned long long)stats.tokens, n,
            (unsigned long long)hapax, n ? (double)hapax / n : 0, now() - begin, classical, c,
            exponent, error);
    for (size_t i = 0; i < n && i < 30; i++) {
        if (i)
            fputc(',', f);
        fputs("{\"term\":", f);
        json_string(f, ranked[i]->key);
        fprintf(f, ",\"freq\":%llu}", (unsigned long long)ranked[i]->count);
    }
    fputs("],\n\"points\":[", f);
    size_t last = (size_t)-1;
    for (size_t point = 0; point < 400 && n; point++) {
        size_t at = (size_t)floor(exp(log((double)n) * point / 399.0)) - 1;
        if (at >= n)
            at = n - 1;
        if (at == last)
            continue;
        if (last != (size_t)-1)
            fputc(',', f);
        fprintf(f, "{\"rank\":%zu,\"freq\":%llu,\"zipf\":%.8f,\"fit\":%.8f}", at + 1,
                (unsigned long long)ranked[at]->count, classical / (at + 1),
                c / pow(at + 1, exponent));
        last = at;
    }
    fputs("]\n}\n", f);
    if (fclose(f)) {
        free(ranked);
        return 1;
    }
    int result = plot_distribution(svg, ranked, n, classical, c, exponent);
    free(ranked);
    return result;
}
}
int ir_zipf_main(int argc, char **argv) {
    if (argc == 4 && !strcmp(argv[0], "zipf"))
        return zipf(argv[1], argv[2], argv[3]);
    fprintf(stderr, "usage: zipf DOCUMENTS_TSV OUTPUT_JSON OUTPUT_SVG\n");
    return 2;
}
