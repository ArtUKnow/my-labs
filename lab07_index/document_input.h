#ifndef IR_LAB07_INDEX_DOCUMENT_INPUT_H
#define IR_LAB07_INDEX_DOCUMENT_INPUT_H
struct BuildContext {
    WordMap *map;
    uint32_t doc;
    int mode;
    uint64_t tokens, block_tokens;
};
inline void add_token(const char *token, size_t n, void *state) {
    BuildContext &c = *(BuildContext *)state;
    char *word = normalize_word(token, n, c.mode);
    if (*word)
        c.map->add(word, c.doc);
    free(word);
    ++c.tokens;
    ++c.block_tokens;
}
inline char *read_gzip(const char *path, size_t &length) {
    gzFile f = gzopen(path, "rb");
    if (!f)
        stop_with_error("cannot open corpus document");
    Array<char> b;
    char chunk[65536];
    int n;
    while ((n = gzread(f, chunk, sizeof chunk)) > 0) {
        for (int i = 0; i < n; ++i)
            b.add(chunk[i]);
    }
    if (n < 0) {
        gzclose(f);
        stop_with_error("cannot decompress corpus document");
    }
    if (gzclose(f) != Z_OK)
        stop_with_error("cannot close gzip document");
    length = b.length;
    char *result = duplicate(b.items, b.length);
    return result;
}
inline char *field(char *&cursor) {
    if (!cursor)
        return 0;
    char *start = cursor;
    char *end = strchr(cursor, '\t');
    if (end) {
        *end = 0;
        cursor = end + 1;
    } else
        cursor = 0;
    return start;
}

#endif
