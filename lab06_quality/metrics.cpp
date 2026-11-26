#include <errno.h>
#include <limits.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

struct Judgment {
    char *query;
    char *document;
    int grade;
};
struct RunItem {
    char *query;
    char *document;
    int rank;
};
struct Scores {
    double precision, recall, ap, dcg, ndcg, err;
};
static void *resize_memory(void *p, size_t n) {
    void *q = realloc(p, n);
    if (!q) {
        fprintf(stderr, "out of memory\n");
        exit(1);
    }
    return q;
}
static char *copy_text(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)resize_memory(NULL, n);
    memcpy(p, s, n);
    return p;
}
template <class T> struct Buffer {
    T *data;
    size_t size, capacity;
    Buffer() : data(NULL), size(0), capacity(0) {
    }
    ~Buffer() {
        free(data);
    }
    void add(T item) {
        if (size == capacity) {
            capacity = capacity ? capacity * 2 : 16;
            data = (T *)resize_memory(data, capacity * sizeof(T));
        }
        data[size++] = item;
    }
};
static int relevance(const Judgment *rows, size_t n, const char *query, const char *doc) {
    for (size_t i = 0; i < n; ++i)
        if (!strcmp(rows[i].query, query) && !strcmp(rows[i].document, doc))
            return rows[i].grade;
    return 0;
}
static Scores calculate(const Judgment *rows, size_t n, const RunItem *run, size_t count,
                        const char *query, int k) {
    Scores s = {0, 0, 0, 0, 0, 0};
    int total = 0, hits = 0;
    double continuation = 1, ideal = 0;
    Buffer<int> grades;
    for (size_t i = 0; i < n; ++i)
        if (!strcmp(rows[i].query, query)) {
            grades.add(rows[i].grade);
            if (rows[i].grade > 0)
                ++total;
        }
    for (size_t i = 1; i < grades.size; ++i) {
        int value = grades.data[i];
        size_t j = i;
        while (j && grades.data[j - 1] < value) {
            grades.data[j] = grades.data[j - 1];
            --j;
        }
        grades.data[j] = value;
    }
    for (int i = 0; i < k && (size_t)i < grades.size; ++i)
        ideal += (pow(2., grades.data[i]) - 1.) / log2(i + 2.);
    for (int rank = 1; rank <= k; ++rank) {
        const char *doc = NULL;
        for (size_t i = 0; i < count; ++i)
            if (run[i].rank == rank && !strcmp(run[i].query, query)) {
                doc = run[i].document;
                break;
            }
        int grade = doc ? relevance(rows, n, query, doc) : 0;
        if (grade > 0) {
            ++hits;
            s.ap += (double)hits / rank;
        }
        s.dcg += (pow(2., grade) - 1.) / log2(rank + 1.);
        double probability = (pow(2., grade) - 1.) / 8.;
        s.err += continuation * probability / rank;
        continuation *= 1. - probability;
    }
    s.precision = (double)hits / k;
    s.recall = total ? (double)hits / total : 0;
    s.ap = total ? s.ap / total : 0;
    s.ndcg = ideal ? s.dcg / ideal : 0;
    return s;
}
static bool fields(char *line, char **a, char **b, char **c) {
    *a = line;
    *b = strchr(line, '\t');
    if (!*b)
        return false;
    *(*b)++ = 0;
    *c = strchr(*b, '\t');
    if (!*c)
        return false;
    *(*c)++ = 0;
    (*c)[strcspn(*c, "\r\n")] = 0;
    return **a && **b && **c && !strchr(*c, '\t');
}
static void print_score(Scores s) {
    printf("{\"p\":%.8f,\"r\":%.8f,\"ap\":%.8f,\"dcg\":%.8f,\"ndcg\":%.8f,"
           "\"err\":%.8f}",
           s.precision, s.recall, s.ap, s.dcg, s.ndcg, s.err);
}
static void quoted(const char *s) {
    putchar('"');
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            putchar('\\');
            putchar(c);
        } else if (c < 32)
            printf("\\u%04x", c);
        else
            putchar(c);
    }
    putchar('"');
}
static int read_judgments(FILE *file, Buffer<Judgment> &rows, Buffer<char *> &queries) {
    char *line = NULL;
    size_t capacity = 0;
    int error = 0;
    while (getline(&line, &capacity, file) >= 0) {
        if (!line[0] || line[0] == '\n')
            continue;
        char *a, *b, *c, *end;
        if (!fields(line, &a, &b, &c)) {
            error = 1;
            break;
        }
        long grade = strtol(c, &end, 10);
        if (*end || grade < 0 || grade > 3) {
            error = 1;
            break;
        }
        bool duplicate = false;
        for (size_t i = 0; i < rows.size; ++i)
            if (!strcmp(rows.data[i].query, a) && !strcmp(rows.data[i].document, b))
                duplicate = true;
        if (duplicate) {
            error = 1;
            break;
        }
        rows.add({copy_text(a), copy_text(b), (int)grade});
        bool exists = false;
        for (size_t i = 0; i < queries.size; ++i)
            if (!strcmp(queries.data[i], a))
                exists = true;
        if (!exists)
            queries.add(copy_text(a));
    }
    free(line);
    return error;
}

static int read_results(FILE *file, Buffer<RunItem> &run, Buffer<char *> &queries) {
    char *line = NULL;
    size_t capacity = 0;
    int error = 0;
    while (getline(&line, &capacity, file) >= 0) {
        if (!line[0] || line[0] == '\n')
            continue;
        char *a, *b, *c, *end;
        if (!fields(line, &a, &b, &c)) {
            error = 1;
            break;
        }
        errno = 0;
        long rank = strtol(c, &end, 10);
        if (errno || end == c || *end || rank <= 0 || rank > INT_MAX) {
            error = 1;
            break;
        }
        bool known = false;
        for (size_t i = 0; i < queries.size; ++i)
            if (!strcmp(queries.data[i], a))
                known = true;
        if (!known) {
            error = 1;
            break;
        }
        for (size_t i = 0; i < run.size; ++i)
            if (!strcmp(run.data[i].query, a) &&
                (run.data[i].rank == rank || !strcmp(run.data[i].document, b)))
                error = 1;
        if (error)
            break;
        run.add({copy_text(a), copy_text(b), (int)rank});
    }
    free(line);
    return error;
}

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
