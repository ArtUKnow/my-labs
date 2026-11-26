#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "metric_types.h"
#include "metric_formulas.h"
#include "metric_input.h"

int ir_metrics_main(int argc, char **argv) {
    if (argc < 3 || argc > 4) {
        fprintf(stderr, "usage: ir metrics QRELS_TSV RUN_TSV [k]\n");
        return 2;
    }
    int k = 5;
    if (argc == 4) {
        char *end = NULL;
        errno = 0;
        long value = strtol(argv[3], &end, 10);
        if (errno || end == argv[3] || *end || value <= 0 || value > INT_MAX) {
            fprintf(stderr, "k must be a positive integer\n");
            return 2;
        }
        k = (int)value;
    }
    FILE *qfile = fopen(argv[1], "r");
    FILE *rfile = fopen(argv[2], "r");
    if (!qfile || !rfile) {
        perror("metrics input");
        if (qfile)
            fclose(qfile);
        if (rfile)
            fclose(rfile);
        return 1;
    }
    Buffer<Judgment> rows;
    Buffer<RunItem> run;
    Buffer<char *> queries;
    int error = 0;
    error = read_judgments(qfile, rows, queries);
    if (!error)
        error = read_results(rfile, run, queries);
    fclose(qfile);
    fclose(rfile);
    if (error || !queries.size) {
        fprintf(stderr, "invalid/empty qrels or run: need unique query/doc "
                        "judgments and unique positive ranks\n");
        error = 1;
    } else {
        Scores avg = {0, 0, 0, 0, 0, 0};
        printf("{\"k\":%d,\"query_count\":%zu,\"queries\":[", k, queries.size);
        for (size_t i = 0; i < queries.size; ++i) {
            Scores s = calculate(rows.data, rows.size, run.data, run.size, queries.data[i], k);
            if (i)
                putchar(',');
            printf("{\"query_id\":");
            quoted(queries.data[i]);
            printf(",\"metrics\":");
            print_score(s);
            putchar('}');
            avg.precision += s.precision;
            avg.recall += s.recall;
            avg.ap += s.ap;
            avg.dcg += s.dcg;
            avg.ndcg += s.ndcg;
            avg.err += s.err;
        }
        double n = (double)queries.size;
        avg.precision /= n;
        avg.recall /= n;
        avg.ap /= n;
        avg.dcg /= n;
        avg.ndcg /= n;
        avg.err /= n;
        printf("],\"average\":");
        print_score(avg);
        puts("}");
    }
    for (size_t i = 0; i < rows.size; ++i) {
        free(rows.data[i].query);
        free(rows.data[i].document);
    }
    for (size_t i = 0; i < run.size; ++i) {
        free(run.data[i].query);
        free(run.data[i].document);
    }
    for (size_t i = 0; i < queries.size; ++i)
        free(queries.data[i]);
    return error;
}
int ir_metrics_tests() {
    Judgment judgments[] = {{(char *)"q", (char *)"a", 3}, {(char *)"q", (char *)"b", 1}};
    RunItem run[] = {{(char *)"q", (char *)"z", 1}, {(char *)"q", (char *)"a", 2}};
    Scores s = calculate(judgments, 2, run, 2, "q", 5);
    double expected_dcg = 7. / log2(3.);
    double expected_ideal = 7. + 1. / log2(3.);
    int failures = 0;
    if (fabs(s.precision - .2) > 1e-9 || fabs(s.recall - .5) > 1e-9 || fabs(s.ap - .25) > 1e-9 ||
        fabs(s.dcg - expected_dcg) > 1e-9 || fabs(s.ndcg - expected_dcg / expected_ideal) > 1e-9 ||
        fabs(s.err - .4375) > 1e-9)
        ++failures;
    RunItem duplicate[] = {{(char *)"q", (char *)"a", 1}};
    s = calculate(judgments, 2, duplicate, 1, "q", 1);
    if (fabs(s.ndcg - 1) > 1e-9 || fabs(s.ap - .5) > 1e-9)
        ++failures;
    fprintf(stderr, "metrics fixtures: %s\n", failures ? "FAILED" : "OK");
    return failures;
}
