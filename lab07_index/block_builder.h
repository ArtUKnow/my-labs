#ifndef IR_LAB07_INDEX_BLOCK_BUILDER_H
#define IR_LAB07_INDEX_BLOCK_BUILDER_H
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
inline void merge_entries(Entry **a, Entry **tmp, size_t lo, size_t hi) {
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
inline void write_block(WordMap &map, const char *dir, unsigned number) {
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
inline void advance(Run &r) {
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
inline void merge_blocks(const char *dir, unsigned count, uint64_t &terms, uint64_t &postings,
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

#endif
