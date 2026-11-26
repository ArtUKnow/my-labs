#ifndef IR_LAB07_INDEX_INDEX_IO_H
#define IR_LAB07_INDEX_INDEX_IO_H
inline void stop_with_error(const char *message) {
    fprintf(stderr, "native index: %s (%s)\n", message, strerror(errno));
    exit(2);
}
void *allocate(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p)
        stop_with_error("out of memory");
    return p;
}
inline char *duplicate(const char *p, size_t n) {
    char *r = (char *)allocate(n + 1);
    memcpy(r, p, n);
    r[n] = 0;
    return r;
}
template <class T> struct Array {
    T *items;
    size_t length, capacity;
    Array() : items(0), length(0), capacity(0) {
    }
    ~Array() {
        free(items);
    }
    Array(const Array &) = delete;
    Array &operator=(const Array &) = delete;
    void add(T value) {
        if (length == capacity) {
            size_t c = capacity ? capacity * 2 : 16;
            if (c < capacity || c > SIZE_MAX / sizeof(T))
                stop_with_error("array overflow");
            void *p = realloc(items, c * sizeof(T));
            if (!p)
                stop_with_error("out of memory");
            items = (T *)p;
            capacity = c;
        }
        items[length++] = value;
    }
};
inline void write_bytes(FILE *f, const void *p, size_t n) {
    if (n && fwrite(p, 1, n, f) != n)
        stop_with_error("write failed");
}
inline void read_bytes(FILE *f, void *p, size_t n) {
    if (n && fread(p, 1, n, f) != n)
        stop_with_error("truncated index");
}
inline void put32(FILE *f, uint32_t x) {
    unsigned char b[4];
    for (int i = 0; i < 4; ++i)
        b[i] = (unsigned char)(x >> (8 * i));
    write_bytes(f, b, 4);
}
inline void put64(FILE *f, uint64_t x) {
    unsigned char b[8];
    for (int i = 0; i < 8; ++i)
        b[i] = (unsigned char)(x >> (8 * i));
    write_bytes(f, b, 8);
}
inline uint32_t get32(FILE *f) {
    unsigned char b[4];
    read_bytes(f, b, 4);
    uint32_t x = 0;
    for (int i = 0; i < 4; ++i)
        x |= (uint32_t)b[i] << (8 * i);
    return x;
}
inline uint64_t get64(FILE *f) {
    unsigned char b[8];
    read_bytes(f, b, 8);
    uint64_t x = 0;
    for (int i = 0; i < 8; ++i)
        x |= (uint64_t)b[i] << (8 * i);
    return x;
}
inline void header(FILE *f, const char *magic) {
    write_bytes(f, magic, 8);
    put32(f, 1);
}
inline void check_header(FILE *f, const char *magic) {
    char b[8];
    read_bytes(f, b, 8);
    if (memcmp(b, magic, 8) || get32(f) != 1)
        stop_with_error("unsupported index format");
}
inline void put_string(FILE *f, const char *s) {
    size_t n = strlen(s);
    if (n > UINT32_MAX)
        stop_with_error("oversized string");
    put32(f, (uint32_t)n);
    write_bytes(f, s, n);
}
inline char *get_string(FILE *f) {
    uint32_t n = get32(f);
    if (n > 16 * 1024 * 1024)
        stop_with_error("invalid index string length");
    char *s = (char *)allocate((size_t)n + 1);
    read_bytes(f, s, n);
    s[n] = 0;
    return s;
}
inline FILE *open_file(const char *dir, const char *name, const char *mode) {
    char p[4096];
    if (snprintf(p, sizeof p, "%s/%s", dir, name) >= (int)sizeof p)
        stop_with_error("path too long");
    FILE *f = fopen(p, mode);
    if (!f)
        stop_with_error(p);
    return f;
}
inline void close_file(FILE *f) {
    if (fclose(f))
        stop_with_error("close failed");
}
inline void json_string(FILE *f, const char *s) {
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)s; *p; ++p) {
        switch (*p) {
        case '"':
            fputs("\\\"", f);
            break;
        case '\\':
            fputs("\\\\", f);
            break;
        case '\n':
            fputs("\\n", f);
            break;
        case '\r':
            fputs("\\r", f);
            break;
        case '\t':
            fputs("\\t", f);
            break;
        default:
            if (*p < 32)
                fprintf(f, "\\u%04x", *p);
            else
                fputc(*p, f);
        }
    }
    fputc('"', f);
}
inline uint64_t hash_word(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}

#endif
