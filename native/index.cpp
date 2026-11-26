#include "index.h"
#include "text.h"
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>
#include <time.h>
#include <zlib.h>

namespace {
void stop_with_error(const char *message) {
    fprintf(stderr, "native index: %s (%s)\n", message, strerror(errno));
    exit(2);
}
void *allocate(size_t n) {
    void *p = malloc(n ? n : 1);
    if (!p)
        stop_with_error("out of memory");
    return p;
}
char *duplicate(const char *p, size_t n) {
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
void write_bytes(FILE *f, const void *p, size_t n) {
    if (n && fwrite(p, 1, n, f) != n)
        stop_with_error("write failed");
}
void read_bytes(FILE *f, void *p, size_t n) {
    if (n && fread(p, 1, n, f) != n)
        stop_with_error("truncated index");
}
void put32(FILE *f, uint32_t x) {
    unsigned char b[4];
    for (int i = 0; i < 4; ++i)
        b[i] = (unsigned char)(x >> (8 * i));
    write_bytes(f, b, 4);
}
void put64(FILE *f, uint64_t x) {
    unsigned char b[8];
    for (int i = 0; i < 8; ++i)
        b[i] = (unsigned char)(x >> (8 * i));
    write_bytes(f, b, 8);
}
uint32_t get32(FILE *f) {
    unsigned char b[4];
    read_bytes(f, b, 4);
    uint32_t x = 0;
    for (int i = 0; i < 4; ++i)
        x |= (uint32_t)b[i] << (8 * i);
    return x;
}
uint64_t get64(FILE *f) {
    unsigned char b[8];
    read_bytes(f, b, 8);
    uint64_t x = 0;
    for (int i = 0; i < 8; ++i)
        x |= (uint64_t)b[i] << (8 * i);
    return x;
}
void header(FILE *f, const char *magic) {
    write_bytes(f, magic, 8);
    put32(f, 1);
}
void check_header(FILE *f, const char *magic) {
    char b[8];
    read_bytes(f, b, 8);
    if (memcmp(b, magic, 8) || get32(f) != 1)
        stop_with_error("unsupported index format");
}
void put_string(FILE *f, const char *s) {
    size_t n = strlen(s);
    if (n > UINT32_MAX)
        stop_with_error("oversized string");
    put32(f, (uint32_t)n);
    write_bytes(f, s, n);
}
char *get_string(FILE *f) {
    uint32_t n = get32(f);
    if (n > 16 * 1024 * 1024)
        stop_with_error("invalid index string length");
    char *s = (char *)allocate((size_t)n + 1);
    read_bytes(f, s, n);
    s[n] = 0;
    return s;
}
FILE *open_file(const char *dir, const char *name, const char *mode) {
    char p[4096];
    if (snprintf(p, sizeof p, "%s/%s", dir, name) >= (int)sizeof p)
        stop_with_error("path too long");
    FILE *f = fopen(p, mode);
    if (!f)
        stop_with_error(p);
    return f;
}
void close_file(FILE *f) {
    if (fclose(f))
        stop_with_error("close failed");
}
void json_string(FILE *f, const char *s) {
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
uint64_t hash_word(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    while (*s) {
        h ^= (unsigned char)*s++;
        h *= 1099511628211ULL;
    }
    return h;
}
struct Posting {
    uint32_t *ids;
    size_t length, capacity;
};
struct Entry {
    char *word;
    Posting posting;
};
struct WordMap {
    Array<Entry> entries;
    int64_t *slots;
    size_t capacity;
    WordMap() : slots(0), capacity(0) {
    }
    ~WordMap() {
        clear();
    }
    void rehash(size_t n) {
        int64_t *s = (int64_t *)allocate(n * sizeof(int64_t));
        for (size_t i = 0; i < n; ++i)
            s[i] = -1;
        for (size_t i = 0; i < entries.length; ++i) {
            size_t k = hash_word(entries.items[i].word) & (n - 1);
            while (s[k] >= 0)
                k = (k + 1) & (n - 1);
            s[k] = (int64_t)i;
        }
        free(slots);
        slots = s;
        capacity = n;
    }
    void add(const char *word, uint32_t doc) {
        if (!capacity)
            rehash(1024);
        if ((entries.length + 1) * 10 > capacity * 7)
            rehash(capacity * 2);
        size_t k = hash_word(word) & (capacity - 1);
        while (slots[k] >= 0 && strcmp(entries.items[slots[k]].word, word))
            k = (k + 1) & (capacity - 1);
        if (slots[k] < 0) {
            Entry e = {duplicate(word, strlen(word)), {0, 0, 0}};
            entries.add(e);
            slots[k] = (int64_t)entries.length - 1;
        }
        Posting &p = entries.items[slots[k]].posting;
        if (p.length && p.ids[p.length - 1] == doc)
            return;
        if (p.length == p.capacity) {
            size_t n = p.capacity ? p.capacity * 2 : 4;
            void *q = realloc(p.ids, n * sizeof(uint32_t));
            if (!q)
                stop_with_error("out of memory");
            p.ids = (uint32_t *)q;
            p.capacity = n;
        }
        p.ids[p.length++] = doc;
    }
    void clear() {
        for (size_t i = 0; i < entries.length; ++i) {
            free(entries.items[i].word);
            free(entries.items[i].posting.ids);
        }
        entries.length = 0;
        free(slots);
        slots = 0;
        capacity = 0;
    }
};
void merge_entries(Entry **a, Entry **tmp, size_t lo, size_t hi) {
    if (hi - lo < 2)
        return;
    size_t mid = lo + (hi - lo) / 2;
    merge_entries(a, tmp, lo, mid);
    merge_entries(a, tmp, mid, hi);
    size_t i = lo, j = mid, k = lo;
    while (i < mid && j < hi)
        tmp[k++] = strcmp(a[i]->word, a[j]->word) <= 0 ? a[i++] : a[j++];
    while (i < mid)
        tmp[k++] = a[i++];
    while (j < hi)
        tmp[k++] = a[j++];
    for (k = lo; k < hi; ++k)
        a[k] = tmp[k];
}
void write_block(WordMap &map, const char *dir, unsigned number) {
    char name[64];
    snprintf(name, sizeof name, "block_%06u.run", number);
    FILE *f = open_file(dir, name, "wb");
    header(f, "IRRUN001");
    put64(f, map.entries.length);
    Entry **order = (Entry **)allocate(map.entries.length * sizeof(Entry *));
    Entry **tmp = (Entry **)allocate(map.entries.length * sizeof(Entry *));
    for (size_t i = 0; i < map.entries.length; ++i)
        order[i] = map.entries.items + i;
    merge_entries(order, tmp, 0, map.entries.length);
    for (size_t i = 0; i < map.entries.length; ++i) {
        Entry &e = *order[i];
        put_string(f, e.word);
        put32(f, (uint32_t)e.posting.length);
        for (size_t j = 0; j < e.posting.length; ++j)
            put32(f, e.posting.ids[j]);
    }
    free(tmp);
    free(order);
    close_file(f);
    map.clear();
}
struct Run {
    FILE *f;
    uint64_t remaining;
    char *word;
    Array<uint32_t> ids;
    Run() : f(0), remaining(0), word(0) {
    }
    ~Run() {
        if (f)
            close_file(f);
        free(word);
    }
};
void advance(Run &r) {
    free(r.word);
    r.word = 0;
    r.ids.length = 0;
    if (!r.remaining)
        return;
    --r.remaining;
    r.word = get_string(r.f);
    uint32_t n = get32(r.f);
    for (uint32_t i = 0; i < n; ++i)
        r.ids.add(get32(r.f));
}
struct Heap {
    Array<unsigned> ids;
    Run **runs;
    Heap(Run **r) : runs(r) {
    }
    bool before(unsigned a, unsigned b) {
        int c = strcmp(runs[a]->word, runs[b]->word);
        return c < 0 || (c == 0 && a < b);
    }
    void push(unsigned x) {
        ids.add(x);
        size_t k = ids.length - 1;
        while (k) {
            size_t p = (k - 1) / 2;
            if (!before(ids.items[k], ids.items[p]))
                break;
            unsigned t = ids.items[k];
            ids.items[k] = ids.items[p];
            ids.items[p] = t;
            k = p;
        }
    }
    unsigned pop() {
        unsigned top = ids.items[0];
        ids.items[0] = ids.items[--ids.length];
        size_t k = 0;
        while (k * 2 + 1 < ids.length) {
            size_t c = k * 2 + 1;
            if (c + 1 < ids.length && before(ids.items[c + 1], ids.items[c]))
                ++c;
            if (!before(ids.items[c], ids.items[k]))
                break;
            unsigned t = ids.items[k];
            ids.items[k] = ids.items[c];
            ids.items[c] = t;
            k = c;
        }
        return top;
    }
};
void merge_blocks(const char *dir, unsigned count, uint64_t &terms, uint64_t &postings,
                  uint64_t &characters) {
    Run **runs = (Run **)allocate(count * sizeof(Run *));
    for (unsigned i = 0; i < count; ++i) {
        runs[i] = new Run;
        char name[64];
        snprintf(name, sizeof name, "block_%06u.run", i);
        runs[i]->f = open_file(dir, name, "rb");
        check_header(runs[i]->f, "IRRUN001");
        runs[i]->remaining = get64(runs[i]->f);
        advance(*runs[i]);
    }
    Heap heap(runs);
    for (unsigned i = 0; i < count; ++i)
        if (runs[i]->word)
            heap.push(i);
    FILE *d = open_file(dir, "dictionary.bin", "wb");
    FILE *p = open_file(dir, "postings.bin", "wb");
    header(d, "IRDICT01");
    put64(d, 0);
    header(p, "IRPOST01");
    terms = postings = characters = 0;
    uint64_t offset = 12;
    while (heap.ids.length) {
        char *word =
            duplicate(runs[heap.ids.items[0]]->word, strlen(runs[heap.ids.items[0]]->word));
        uint64_t start = offset;
        uint32_t frequency = 0, previous = 0;
        bool have = false;
        while (heap.ids.length && !strcmp(word, runs[heap.ids.items[0]]->word)) {
            unsigned n = heap.pop();
            Run &r = *runs[n];
            for (size_t j = 0; j < r.ids.length; ++j) {
                uint32_t id = r.ids.items[j];
                if (have && id < previous)
                    stop_with_error("postings are not sorted");
                if (!have || id != previous) {
                    put32(p, id);
                    offset += 4;
                    ++frequency;
                    previous = id;
                    have = true;
                }
            }
            advance(r);
            if (r.word)
                heap.push(n);
        }
        put_string(d, word);
        put32(d, frequency);
        put64(d, start);
        ++terms;
        postings += frequency;
        for (const unsigned char *c = (const unsigned char *)word; *c; ++c)
            if ((*c & 0xc0) != 0x80)
                ++characters;
        free(word);
    }
    if (fseeko(d, 12, SEEK_SET))
        stop_with_error("seek failed");
    put64(d, terms);
    close_file(d);
    close_file(p);
    for (unsigned i = 0; i < count; ++i) {
        delete runs[i];
        char path[4096];
        snprintf(path, sizeof path, "%s/block_%06u.run", dir, i);
        if (remove(path))
            stop_with_error("cannot remove temporary run");
    }
    free(runs);
}
char *normalize_word(const char *token, size_t length, int mode) {
    if (mode != 2) {
        char *word = duplicate(token, length);
        if (mode == 1)
            ir_stem(word);
        return word;
    }
    if (length > SIZE_MAX - 128)
        stop_with_error("token length overflow");
    char *input = duplicate(token, length);
    char *word = (char *)allocate(length + 128);
    if (!ir_lemma_copy(input, word, length + 128)) {
        free(input);
        free(word);
        stop_with_error("lemma output exceeds allocated capacity");
    }
    free(input);
    return word;
}
struct BuildContext {
    WordMap *map;
    uint32_t doc;
    int mode;
    uint64_t tokens, block_tokens;
};
void add_token(const char *token, size_t n, void *state) {
    BuildContext &c = *(BuildContext *)state;
    char *word = normalize_word(token, n, c.mode);
    if (*word)
        c.map->add(word, c.doc);
    free(word);
    ++c.tokens;
    ++c.block_tokens;
}
char *read_gzip(const char *path, size_t &length) {
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
char *field(char *&cursor) {
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
struct DictionaryEntry {
    char *word;
    uint32_t frequency;
    uint64_t offset;
};
struct Document {
    char *title;
    char *url;
    char *journal;
};
struct Index {
    Array<DictionaryEntry> dictionary;
    Array<Document> docs;
    FILE *postings;
    int mode;
    Index(const char *dir) : postings(0), mode(0) {
        FILE *m = open_file(dir, "meta.bin", "rb");
        check_header(m, "IRMETA01");
        mode = (int)get32(m);
        uint64_t count = get64(m);
        close_file(m);
        if (mode < 0 || mode > 2 || count > UINT32_MAX)
            stop_with_error("invalid index metadata");
        FILE *d = open_file(dir, "documents.bin", "rb");
        check_header(d, "IRDOCS01");
        if (get64(d) != count)
            stop_with_error("document count mismatch");
        for (uint64_t i = 0; i < count; ++i) {
            if (get32(d) != i)
                stop_with_error("non-contiguous document ids");
            Document doc;
            doc.title = get_string(d);
            doc.url = get_string(d);
            doc.journal = get_string(d);
            docs.add(doc);
        }
        close_file(d);
        FILE *t = open_file(dir, "dictionary.bin", "rb");
        check_header(t, "IRDICT01");
        uint64_t n = get64(t);
        for (uint64_t i = 0; i < n; ++i) {
            DictionaryEntry e;
            e.word = get_string(t);
            e.frequency = get32(t);
            e.offset = get64(t);
            if (i && strcmp(dictionary.items[i - 1].word, e.word) >= 0)
                stop_with_error("dictionary is not sorted");
            dictionary.add(e);
        }
        close_file(t);
        postings = open_file(dir, "postings.bin", "rb");
        check_header(postings, "IRPOST01");
    }
    ~Index() {
        for (size_t i = 0; i < dictionary.length; ++i)
            free(dictionary.items[i].word);
        for (size_t i = 0; i < docs.length; ++i) {
            free(docs.items[i].title);
            free(docs.items[i].url);
            free(docs.items[i].journal);
        }
        if (postings)
            close_file(postings);
    }
    Array<uint32_t> *find(const char *token) {
        char *word = normalize_word(token, strlen(token), mode);
        size_t lo = 0, hi = dictionary.length;
        while (lo < hi) {
            size_t mid = lo + (hi - lo) / 2;
            if (strcmp(dictionary.items[mid].word, word) < 0)
                lo = mid + 1;
            else
                hi = mid;
        }
        Array<uint32_t> *result = new Array<uint32_t>;
        if (lo < dictionary.length && !strcmp(dictionary.items[lo].word, word)) {
            DictionaryEntry &e = dictionary.items[lo];
            if (fseeko(postings, (off_t)e.offset, SEEK_SET))
                stop_with_error("postings seek failed");
            uint32_t previous = 0;
            for (uint32_t j = 0; j < e.frequency; ++j) {
                uint32_t id = get32(postings);
                if (id >= docs.length || (j && id <= previous))
                    stop_with_error("invalid postings");
                result->add(id);
                previous = id;
            }
        }
        free(word);
        return result;
    }
};
enum Kind { TERM, AND, OR, NOT, LEFT, RIGHT, END };
struct QueryWord {
    Kind kind;
    char *text;
    char bracket;
};
struct LexerContext {
    Array<QueryWord> *tokens;
};
void query_token(const char *t, size_t n, void *state) {
    LexerContext *c = (LexerContext *)state;
    QueryWord l = {TERM, duplicate(t, n), 0};
    c->tokens->add(l);
}
bool read_query_words(const char *s, Array<QueryWord> &tokens, const char *&error) {
    size_t i = 0;
    while (s[i]) {
        unsigned char c = (unsigned char)s[i];
        if (c <= 32) {
            ++i;
            continue;
        }
        QueryWord l = {END, 0, 0};
        if (c == '&' || c == '|') {
            if (s[i + 1] != (char)c) {
                error = "Use && for AND and || for OR";
                return false;
            }
            l.kind = c == '&' ? AND : OR;
            i += 2;
        } else if (c == '!') {
            l.kind = NOT;
            ++i;
        } else if (c == '(' || c == '[') {
            l.kind = LEFT;
            l.bracket = (char)c;
            ++i;
        } else if (c == ')' || c == ']') {
            l.kind = RIGHT;
            l.bracket = (char)c;
            ++i;
        } else {
            size_t start = i;
            while (s[i] && (unsigned char)s[i] > 32 && !strchr("&|!()[]", s[i]))
                ++i;
            LexerContext context = {&tokens};
            ir_tokenize(s + start, i - start, query_token, &context);
            continue;
        }
        tokens.add(l);
    }
    return true;
}
Array<uint32_t> *combine(Array<uint32_t> *a, Array<uint32_t> *b, Kind operation) {
    Array<uint32_t> *r = new Array<uint32_t>;
    size_t i = 0, j = 0;
    while (i < a->length && j < b->length) {
        uint32_t x = a->items[i], y = b->items[j];
        if (x == y) {
            if (operation != NOT)
                r->add(x);
            ++i;
            ++j;
        } else if (x < y) {
            if (operation == OR || operation == NOT)
                r->add(x);
            ++i;
        } else {
            if (operation == OR)
                r->add(y);
            ++j;
        }
    }
    if (operation == OR || operation == NOT)
        while (i < a->length)
            r->add(a->items[i++]);
    if (operation == OR)
        while (j < b->length)
            r->add(b->items[j++]);
    delete a;
    delete b;
    return r;
}
struct Parser {
    Index &index;
    Array<QueryWord> &tokens;
    size_t position;
    unsigned depth;
    const char *error;
    Parser(Index &i, Array<QueryWord> &t) : index(i), tokens(t), position(0), depth(0), error(0) {
    }
    Kind peek() {
        return position < tokens.length ? tokens.items[position].kind : END;
    }
    Array<uint32_t> *all() {
        Array<uint32_t> *r = new Array<uint32_t>;
        for (size_t i = 0; i < index.docs.length; ++i)
            r->add((uint32_t)i);
        return r;
    }
    Array<uint32_t> *atom() {
        if (++depth > 256) {
            error = "Query nesting exceeds 256";
            --depth;
            return new Array<uint32_t>;
        }
        Array<uint32_t> *r = 0;
        if (peek() == TERM) {
            r = index.find(tokens.items[position++].text);
        } else if (peek() == LEFT) {
            char open = tokens.items[position++].bracket;
            r = parse_or();
            if (!error &&
                (peek() != RIGHT || tokens.items[position].bracket != (open == '(' ? ')' : ']')))
                error = "Mismatched or unclosed bracket";
            if (!error)
                ++position;
        } else {
            error = "Expected term or opening bracket";
            r = new Array<uint32_t>;
        }
        --depth;
        return r;
    }
    Array<uint32_t> *parse_not() {
        if (peek() == NOT) {
            if (++depth > 256) {
                error = "Query nesting exceeds 256";
                --depth;
                return new Array<uint32_t>;
            }
            ++position;
            Array<uint32_t> *r = parse_not();
            --depth;
            return combine(all(), r, NOT);
        }
        return atom();
    }
    Array<uint32_t> *parse_and() {
        Array<uint32_t> *r = parse_not();
        while (!error && (peek() == AND || peek() == TERM || peek() == NOT || peek() == LEFT)) {
            if (peek() == AND)
                ++position;
            r = combine(r, parse_not(), AND);
        }
        return r;
    }
    Array<uint32_t> *parse_or() {
        Array<uint32_t> *r = parse_and();
        while (!error && peek() == OR) {
            ++position;
            r = combine(r, parse_and(), OR);
        }
        return r;
    }
    Array<uint32_t> *parse() {
        if (!tokens.length)
            return new Array<uint32_t>;
        Array<uint32_t> *r = parse_or();
        if (!error && peek() != END)
            error = "Unexpected closing bracket or operator";
        return r;
    }
};
}

int ir_index_main(int argc, char **argv) {
    if (argc < 3 || argc > 5) {
        fprintf(stderr, "usage: build DOCUMENTS_TSV INDEX_DIR [raw|stem|lemma] "
                        "[block_token_limit]\n");
        return 2;
    }
    int mode = 1;
    if (argc >= 4) {
        if (!strcmp(argv[3], "raw"))
            mode = 0;
        else if (!strcmp(argv[3], "stem"))
            mode = 1;
        else if (!strcmp(argv[3], "lemma"))
            mode = 2;
        else {
            fprintf(stderr, "unknown normalization mode\n");
            return 2;
        }
    }
    uint64_t limit = 500000;
    if (argc >= 5) {
        char *end = 0;
        limit = strtoull(argv[4], &end, 10);
        if (!*argv[4] || *end || limit == 0) {
            fprintf(stderr, "invalid block limit\n");
            return 2;
        }
    }
    if (mkdir(argv[2], 0755) && errno != EEXIST)
        stop_with_error("cannot create index directory");
    FILE *manifest = fopen(argv[1], "r");
    if (!manifest)
        stop_with_error("cannot open document manifest");
    FILE *docs = open_file(argv[2], "documents.bin", "wb");
    header(docs, "IRDOCS01");
    put64(docs, 0);
    char *line = 0;
    size_t capacity = 0;
    ssize_t n = getline(&line, &capacity, manifest);
    if (n < 0 || strcmp(line, "id\tpath\turl\ttitle\tjournal\n"))
        stop_with_error("invalid document manifest header");
    WordMap map;
    BuildContext context = {&map, 0, mode, 0, 0};
    unsigned blocks = 0;
    uint64_t input_bytes = 0;
    struct timespec started, finished;
    clock_gettime(CLOCK_MONOTONIC, &started);
    while ((n = getline(&line, &capacity, manifest)) >= 0) {
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = 0;
        char *cursor = line;
        char *id = field(cursor), *path = field(cursor), *url = field(cursor),
             *title = field(cursor), *journal = field(cursor);
        if (!id || !path || !url || !title || !journal || cursor)
            stop_with_error("invalid document row");
        char *end = 0;
        unsigned long value = strtoul(id, &end, 10);
        if (!*id || *end || value != context.doc)
            stop_with_error("document ids must be contiguous from zero");
        put32(docs, context.doc);
        put_string(docs, title);
        put_string(docs, url);
        put_string(docs, journal);
        size_t length = 0;
        char *text = read_gzip(path, length);
        input_bytes += length;
        ir_tokenize(text, length, add_token, &context);
        free(text);
        ++context.doc;
        if (context.block_tokens >= limit) {
            write_block(map, argv[2], blocks++);
            context.block_tokens = 0;
        }
        if (context.doc % 5000 == 0)
            fprintf(stderr, "indexed %u documents\n", context.doc);
    }
    if (ferror(manifest))
        stop_with_error("manifest read failed");
    free(line);
    close_file(manifest);
    if (map.entries.length || !blocks)
        write_block(map, argv[2], blocks++);
    if (fseeko(docs, 12, SEEK_SET))
        stop_with_error("seek failed");
    put64(docs, context.doc);
    close_file(docs);
    uint64_t terms = 0, postings = 0, characters = 0;
    merge_blocks(argv[2], blocks, terms, postings, characters);
    FILE *meta = open_file(argv[2], "meta.bin", "wb");
    header(meta, "IRMETA01");
    put32(meta, (uint32_t)mode);
    put64(meta, context.doc);
    put64(meta, context.tokens);
    put64(meta, terms);
    put64(meta, postings);
    close_file(meta);
    clock_gettime(CLOCK_MONOTONIC, &finished);
    double seconds =
        (finished.tv_sec - started.tv_sec) + (finished.tv_nsec - started.tv_nsec) / 1e9;
    FILE *stats = open_file(argv[2], "stats.json", "w");
    fprintf(stats,
            "{\n  \"documents\": %u,\n  \"tokens\": %llu,\n  \"terms\": %llu,\n  "
            "\"postings\": %llu,\n "
            " \"blocks\": %u,\n  \"block_token_limit\": %llu,\n  "
            "\"input_bytes\": %llu,\n  "
            "\"time_seconds\": %.6f,\n  \"seconds_per_document\": %.9f,\n  "
            "\"kilobytes_per_second\": "
            "%.3f,\n  \"average_term_length\": %.6f,\n  \"mode\": \"%s\"\n}\n",
            context.doc, (unsigned long long)context.tokens, (unsigned long long)terms,
            (unsigned long long)postings, blocks, (unsigned long long)limit,
            (unsigned long long)input_bytes, seconds, seconds / (context.doc ? context.doc : 1),
            input_bytes / 1024.0 / (seconds ? seconds : 1),
            characters / (double)(terms ? terms : 1),
            mode == 0   ? "raw"
            : mode == 1 ? "stem"
                        : "lemma");
    close_file(stats);
    fprintf(stderr, "built %u documents, %llu terms, %llu postings in %.3f seconds\n", context.doc,
            (unsigned long long)terms, (unsigned long long)postings, seconds);
    return 0;
}

int ir_search_main(int argc, char **argv) {
    if (argc < 2) {
        fprintf(stderr, "usage: search INDEX_DIR [--json] [--offset N] [--limit N]\n");
        return 2;
    }
    bool json = false;
    uint64_t offset = 0, limit = UINT64_MAX;
    for (int i = 2; i < argc; ++i) {
        if (!strcmp(argv[i], "--json"))
            json = true;
        else if ((!strcmp(argv[i], "--offset") || !strcmp(argv[i], "--limit")) && i + 1 < argc) {
            bool off = !strcmp(argv[i], "--offset");
            char *end = 0;
            const char *value = argv[++i];
            uint64_t number = strtoull(value, &end, 10);
            if (!*value || *value == '-' || *end) {
                fprintf(stderr, "invalid pagination value\n");
                return 2;
            }
            if (off)
                offset = number;
            else
                limit = number;
        } else {
            fprintf(stderr, "unknown search option\n");
            return 2;
        }
    }
    Index index(argv[1]);
    char *line = 0;
    size_t capacity = 0;
    ssize_t n;
    int status = 0;
    while ((n = getline(&line, &capacity, stdin)) >= 0) {
        timespec query_started, query_finished;
        clock_gettime(CLOCK_MONOTONIC, &query_started);
        while (n > 0 && (line[n - 1] == '\n' || line[n - 1] == '\r'))
            line[--n] = 0;
        Array<QueryWord> tokens;
        const char *error = 0;
        Array<uint32_t> *results = 0;
        if (read_query_words(line, tokens, error)) {
            Parser parser(index, tokens);
            results = parser.parse();
            error = parser.error;
        }
        if (error) {
            status = 1;
            if (json) {
                fputs("{\"query\":", stdout);
                json_string(stdout, line);
                fputs(",\"error\":", stdout);
                json_string(stdout, error);
                fputs("}\n", stdout);
            } else
                fprintf(stderr, "query error: %s\n", error);
        } else {
            clock_gettime(CLOCK_MONOTONIC, &query_finished);
            double elapsed_ms = (query_finished.tv_sec - query_started.tv_sec) * 1000.0 +
                                (query_finished.tv_nsec - query_started.tv_nsec) / 1000000.0;
            size_t start = offset < results->length ? (size_t)offset : results->length;
            size_t end = results->length;
            if (limit < (uint64_t)(end - start))
                end = start + (size_t)limit;
            if (json) {
                fputs("{\"query\":", stdout);
                json_string(stdout, line);
                fprintf(stdout,
                        ",\"count\":%zu,\"elapsed_ms\":%.6f,\"offset\":%llu,\"limit\":%"
                        "llu,\"ids\":[",
                        results->length, elapsed_ms, (unsigned long long)offset,
                        (unsigned long long)limit);
                for (size_t i = start; i < end; ++i) {
                    if (i > start)
                        fputc(',', stdout);
                    fprintf(stdout, "%u", results->items[i]);
                }
                fputs("],\"documents\":[", stdout);
                for (size_t i = start; i < end; ++i) {
                    if (i > start)
                        fputc(',', stdout);
                    uint32_t id = results->items[i];
                    Document &doc = index.docs.items[id];
                    fprintf(stdout, "{\"id\":%u,\"title\":", id);
                    json_string(stdout, doc.title);
                    fputs(",\"url\":", stdout);
                    json_string(stdout, doc.url);
                    fputs(",\"journal\":", stdout);
                    json_string(stdout, doc.journal);
                    fputc('}', stdout);
                }
                fputs("]}\n", stdout);
            } else {
                fprintf(stdout, "query: %s -> %zu documents\n", line, results->length);
                for (size_t i = start; i < end; ++i) {
                    uint32_t id = results->items[i];
                    Document &doc = index.docs.items[id];
                    fprintf(stdout, "%u\t%s\t%s\n", id, doc.title, doc.url);
                }
            }
        }
        delete results;
        for (size_t i = 0; i < tokens.length; ++i)
            free(tokens.items[i].text);
    }
    free(line);
    if (ferror(stdin) || ferror(stdout))
        stop_with_error("query input/output failed");
    return status;
}
