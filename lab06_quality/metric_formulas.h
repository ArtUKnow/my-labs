#ifndef IR_LAB06_QUALITY_METRIC_FORMULAS_H
#define IR_LAB06_QUALITY_METRIC_FORMULAS_H
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

#endif
