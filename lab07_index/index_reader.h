#ifndef IR_LAB07_INDEX_INDEX_READER_H
#define IR_LAB07_INDEX_INDEX_READER_H
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

#endif
