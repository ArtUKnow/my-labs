#ifndef IR_LAB06_QUALITY_METRIC_TYPES_H
#define IR_LAB06_QUALITY_METRIC_TYPES_H
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

#endif
