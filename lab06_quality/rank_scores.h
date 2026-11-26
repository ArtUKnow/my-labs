#ifndef IR_LAB06_QUALITY_RANK_SCORES_H
#define IR_LAB06_QUALITY_RANK_SCORES_H
struct Hit {
    uint32_t id;
    double score;
};
struct TopFive {
    Hit hits[5];
    size_t size;
    TopFive() : size(0) {
    }
    void add(uint32_t id, double score) {
        size_t at = 0;
        while (at < size &&
               (hits[at].score > score || (hits[at].score == score && hits[at].id < id)))
            at++;
        if (at >= 5)
            return;
        size_t end = size < 5 ? size : 4;
        for (size_t i = end; i > at; i--)
            hits[i] = hits[i - 1];
        hits[at] = Hit{id, score};
        if (size < 5)
            size++;
    }
};
inline void escaped(const char *value) {
    fputc('"', stdout);
    for (const unsigned char *p = (const unsigned char *)value; *p; p++) {
        if (*p == '"' || *p == '\\') {
            fputc('\\', stdout);
            fputc(*p, stdout);
        } else if (*p < 32)
            fprintf(stdout, "\\u%04x", *p);
        else
            fputc(*p, stdout);
    }
    fputc('"', stdout);
}
inline void hits_json(const TopFive &hits, bool scores) {
    fputc('[', stdout);
    for (size_t i = 0; i < hits.size; i++) {
        if (i)
            fputc(',', stdout);
        if (scores)
            fprintf(stdout, "%.17g", hits.hits[i].score);
        else
            fprintf(stdout, "%u", hits.hits[i].id);
    }
    fputc(']', stdout);
}
inline void evaluate(const Queries &queries, const Documents &docs, const uint32_t *df) {
    double avg = docs.size ? (double)docs.total_length / docs.size : 0;
    for (size_t qi = 0; qi < queries.size; qi++) {
        const Query &q = queries.items[qi];
        TopFive boolean, tfidf, bm25;
        for (size_t di = 0; di < docs.size; di++) {
            const Document &d = docs.items[di];
            bool all = q.size > 0, any = false;
            double tfidf_score = 0, bm25_score = 0;
            for (size_t i = 0; i < q.size; i++) {
                uint32_t term = q.terms[i], tf = d.counts[term];
                if (!tf) {
                    all = false;
                    continue;
                }
                any = true;
                double idf = 1 + log(((double)docs.size + 1) / (df[term] + 1.0));
                if (d.length)
                    tfidf_score += q.frequency[i] * (double)tf / d.length * idf * idf;
                double bm_idf = log(1 + ((double)docs.size - df[term] + 0.5) / (df[term] + 0.5));
                double divisor = tf + 1.2 * (1 - 0.75 + (avg ? 0.75 * d.length / avg : 0));
                if (divisor)
                    bm25_score += q.frequency[i] * bm_idf * tf * (1.2 + 1) / divisor;
            }
            if (all)
                boolean.add(d.id, 0);
            if (any) {
                tfidf.add(d.id, tfidf_score);
                bm25.add(d.id, bm25_score);
            }
        }
        fputs("{\"query_id\":", stdout);
        escaped(q.id);
        fputs(",\"boolean\":", stdout);
        hits_json(boolean, false);
        fputs(",\"tfidf\":", stdout);
        hits_json(tfidf, false);
        fputs(",\"bm25\":", stdout);
        hits_json(bm25, false);
        fputs(",\"tfidf_scores\":", stdout);
        hits_json(tfidf, true);
        fputs(",\"bm25_scores\":", stdout);
        hits_json(bm25, true);
        fprintf(stdout, ",\"corpus_documents\":%zu}\n", docs.size);
    }
}
#endif
