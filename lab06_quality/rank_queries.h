#ifndef IR_LAB06_QUALITY_RANK_QUERIES_H
#define IR_LAB06_QUALITY_RANK_QUERIES_H
struct Query {
    char *id;
    uint32_t *terms;
    uint32_t *frequency;
    size_t size, capacity;
    void add(uint32_t term) {
        for (size_t i = 0; i < size; i++)
            if (terms[i] == term) {
                frequency[i]++;
                return;
            }
        if (size == capacity) {
            capacity = capacity ? capacity * 2 : 16;
            terms = (uint32_t *)checked(realloc(terms, capacity * sizeof(uint32_t)));
            frequency = (uint32_t *)checked(realloc(frequency, capacity * sizeof(uint32_t)));
        }
        terms[size] = term;
        frequency[size++] = 1;
    }
    void release() {
        free(id);
        free(terms);
        free(frequency);
    }
};
struct Queries {
    Query *items;
    size_t size, capacity;
    Queries() : items(NULL), size(0), capacity(0) {
    }
    ~Queries() {
        for (size_t i = 0; i < size; i++)
            items[i].release();
        free(items);
    }
    Query *add(const char *id) {
        for (size_t i = 0; i < size; i++)
            if (!strcmp(items[i].id, id))
                return NULL;
        if (size == capacity) {
            capacity = capacity ? capacity * 2 : 32;
            items = (Query *)checked(realloc(items, capacity * sizeof(Query)));
        }
        Query *q = &items[size++];
        q->id = copy(id);
        q->terms = NULL;
        q->frequency = NULL;
        q->size = q->capacity = 0;
        return q;
    }
};
struct Normalize {
    const char *mode;
    Map *union_terms;
    Map cache;
    Buffer buffer;
    Normalize(const char *m, Map *terms) : mode(m), union_terms(terms) {
    }
    const char *token(const char *input, size_t n) {
        buffer.reserve(n + 64);
        memcpy(buffer.data, input, n);
        buffer.data[n] = 0;
        if (!strcmp(mode, "stem"))
            ir_stem(buffer.data);
        else if (!strcmp(mode, "lemma")) {
            Buffer result;
            result.reserve(n + 64);
            if (!ir_lemma_copy(input, result.data, result.capacity)) {
                fprintf(stderr, "rank: lemma output exceeds capacity\n");
                exit(2);
            }
            memcpy(buffer.data, result.data, strlen(result.data) + 1);
        }
        return buffer.data;
    }
    int find(const char *input, size_t n) {
        if (!strcmp(mode, "raw")) {
            int value;
            return union_terms->get(input, value) ? value : -1;
        }
        int value;
        if (cache.get(input, value))
            return value;
        const char *normalized = token(input, n);
        value = -1;
        union_terms->get(normalized, value);
        cache.put(input, value);
        return value;
    }
};
struct QueryContext {
    Query *query;
    Map *terms;
    Normalize *normalization;
};
inline void query_token(const char *token, size_t n, void *user) {
    QueryContext *c = (QueryContext *)user;
    const char *term = c->normalization->token(token, n);
    int id;
    if (!c->terms->get(term, id)) {
        id = (int)c->terms->used;
        c->terms->put(term, id);
    }
    c->query->add((uint32_t)id);
}
inline bool read_queries(const char *path, const char *mode, Queries &queries, Map &terms) {
    FILE *f = fopen(path, "r");
    if (!f) {
        perror(path);
        return false;
    }
    char *line = NULL;
    size_t cap = 0;
    ssize_t n;
    Normalize normal(mode, &terms);
    bool first = true;
    while ((n = getline(&line, &cap, f)) >= 0) {
        while (n && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = 0;
        if (!n)
            continue;
        char *tab = strchr(line, '\t');
        if (!tab || tab == line) {
            fprintf(stderr, "rank: malformed query TSV\n");
            free(line);
            fclose(f);
            return false;
        }
        *tab = 0;
        if (first && (!strcmp(line, "query_id") || !strcmp(line, "qid")) &&
            (!strcmp(tab + 1, "query") || !strcmp(tab + 1, "text"))) {
            first = false;
            continue;
        }
        first = false;
        Query *q = queries.add(line);
        if (!q) {
            fprintf(stderr, "rank: duplicate query ID %s\n", line);
            free(line);
            fclose(f);
            return false;
        }
        QueryContext context = {q, &terms, &normal};
        ir_tokenize(tab + 1, strlen(tab + 1), query_token, &context);
    }
    bool ok = !ferror(f);
    free(line);
    fclose(f);
    if (!queries.size) {
        fprintf(stderr, "rank: no queries\n");
        return false;
    }
    return ok;
}

#endif
