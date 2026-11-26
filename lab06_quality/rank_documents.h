#ifndef IR_LAB06_QUALITY_RANK_DOCUMENTS_H
#define IR_LAB06_QUALITY_RANK_DOCUMENTS_H
struct Document {
    uint32_t id;
    uint64_t length;
    uint32_t *counts;
};
struct Documents {
    Document *items;
    size_t size, capacity;
    uint64_t total_length;
    Documents() : items(NULL), size(0), capacity(0), total_length(0) {
    }
    ~Documents() {
        for (size_t i = 0; i < size; i++)
            free(items[i].counts);
        free(items);
    }
    Document *add(uint32_t id, size_t terms) {
        if (size == capacity) {
            capacity = capacity ? capacity * 2 : 128;
            items = (Document *)checked(realloc(items, capacity * sizeof(Document)));
        }
        Document *d = &items[size++];
        d->id = id;
        d->length = 0;
        d->counts = (uint32_t *)checked(calloc(terms ? terms : 1, sizeof(uint32_t)));
        return d;
    }
};
struct DocContext {
    Document *document;
    Normalize *normalization;
};
inline void document_token(const char *token, size_t n, void *user) {
    DocContext *c = (DocContext *)user;
    c->document->length++;
    int term = c->normalization->find(token, n);
    if (term >= 0) {
        if (c->document->counts[term] == UINT32_MAX) {
            fprintf(stderr, "rank: document term count overflow\n");
            exit(2);
        }
        c->document->counts[term]++;
    }
}
inline bool read_gzip(const char *path, Buffer &text) {
    text.clear();
    gzFile file = gzopen(path, "rb");
    if (!file) {
        fprintf(stderr, "rank: cannot open %s\n", path);
        return false;
    }
    char bytes[65536];
    int n;
    while ((n = gzread(file, bytes, sizeof(bytes))) > 0)
        text.append(bytes, (size_t)n);
    if (n < 0) {
        int code;
        fprintf(stderr, "rank: read failed %s: %s\n", path, gzerror(file, &code));
        gzclose(file);
        return false;
    }
    if (gzclose(file) != Z_OK) {
        fprintf(stderr, "rank: gzip close failed %s\n", path);
        return false;
    }
    return true;
}
inline bool read_documents(const char *path, const char *mode, Map &terms, Documents &docs, uint32_t *df) {
    FILE *f = fopen(path, "r");
    if (!f) {
        perror(path);
        return false;
    }
    char *line = NULL;
    size_t cap = 0;
    ssize_t n = getline(&line, &cap, f);
    if (n < 0 || strncmp(line, "id\tpath\t", 8)) {
        fprintf(stderr, "rank: document TSV header must start id,path\n");
        free(line);
        fclose(f);
        return false;
    }
    Normalize normal(mode, &terms);
    Buffer text;
    while ((n = getline(&line, &cap, f)) >= 0) {
        char *first = strchr(line, '\t');
        char *second = first ? strchr(first + 1, '\t') : NULL;
        if (!first || !second) {
            fprintf(stderr, "rank: malformed document TSV\n");
            free(line);
            fclose(f);
            return false;
        }
        *first = 0;
        *second = 0;
        char *end = NULL;
        errno = 0;
        unsigned long id = strtoul(line, &end, 10);
        if (errno || !end || *end || id > UINT32_MAX || id != docs.size) {
            fprintf(stderr, "rank: document IDs must be contiguous from zero\n");
            free(line);
            fclose(f);
            return false;
        }
        if (!read_gzip(first + 1, text)) {
            free(line);
            fclose(f);
            return false;
        }
        Document *d = docs.add((uint32_t)id, terms.used);
        DocContext context = {d, &normal};
        ir_tokenize(text.data ? text.data : "", text.size, document_token, &context);
        docs.total_length += d->length;
        for (size_t i = 0; i < terms.used; i++)
            if (d->counts[i])
                df[i]++;
        if (docs.size % 5000 == 0)
            fprintf(stderr, "rank documents: %zu\n", docs.size);
    }
    bool ok = !ferror(f);
    free(line);
    fclose(f);
    fprintf(stderr, "rank corpus_documents=%zu total_tokens=%llu query_terms=%zu\n", docs.size,
            (unsigned long long)docs.total_length, terms.used);
    return ok;
}

#endif
