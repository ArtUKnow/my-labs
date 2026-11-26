#ifndef IR_LAB05_ZIPF_CORPUS_COUNT_H
#define IR_LAB05_ZIPF_CORPUS_COUNT_H
struct Point {
    uint64_t bytes, tokens;
    double seconds;
};
struct Stats {
    uint64_t tokens, length, bytes, documents, words, numbers, mixed;
    Point *points;
    size_t cap;
    char *examples[15];
    size_t example_count;
    Frequencies *frequencies;
    Stats()
        : tokens(0), length(0), bytes(0), documents(0), words(0), numbers(0), mixed(0),
          points(NULL), cap(0), example_count(0), frequencies(NULL) {
        memset(examples, 0, sizeof(examples));
    }
    ~Stats() {
        free(points);
        for (size_t i = 0; i < example_count; i++)
            free(examples[i]);
    }
};
void stats_token(const char *t, size_t, void *user) {
    Stats *s = (Stats *)user;
    s->tokens++;
    size_t n = strlen(t), w, count = 0;
    bool letter = false, number = false;
    for (size_t i = 0; i < n; i += w) {
        uint32_t c = read_utf8(t + i, n - i, &w);
        count++;
        letter |= is_letter(c);
        number |= is_digit(c);
    }
    s->length += count;
    if (letter && number)
        s->mixed++;
    else if (number)
        s->numbers++;
    else
        s->words++;
    if (s->frequencies)
        s->frequencies->add(t);
    if (count >= 25 && s->example_count < 15) {
        bool exists = false;
        for (size_t i = 0; i < s->example_count; i++)
            if (!strcmp(s->examples[i], t))
                exists = true;
        if (!exists)
            s->examples[s->example_count++] = clone(t);
    }
}
bool corpus(const char *manifest, Stats &stats) {
    FILE *f = fopen(manifest, "r");
    if (!f) {
        perror(manifest);
        return false;
    }
    char *line = NULL;
    size_t length = 0;
    ssize_t n;
    if ((n = getline(&line, &length, f)) < 0 || strncmp(line, "id\tpath\t", 8)) {
        fprintf(stderr, "documents TSV must have id,path,url,title,journal header\n");
        free(line);
        fclose(f);
        return false;
    }
    Buffer text;
    while ((n = getline(&line, &length, f)) >= 0) {
        char *first = strchr(line, '\t');
        char *second = first ? strchr(first + 1, '\t') : NULL;
        if (!first || !second) {
            fprintf(stderr, "invalid documents TSV row\n");
            free(line);
            fclose(f);
            return false;
        }
        *second = 0;
        if (!read_document(first + 1, text)) {
            free(line);
            fclose(f);
            return false;
        }
        uint64_t before = stats.tokens;
        double begin = now();
        ir_tokenize(text.data ? text.data : "", text.size, stats_token, &stats);
        double seconds = now() - begin;
        if (stats.documents == stats.cap) {
            stats.cap = stats.cap ? stats.cap * 2 : 128;
            stats.points = (Point *)checked(realloc(stats.points, stats.cap * sizeof(Point)));
        }
        stats.points[stats.documents++] =
            Point{(uint64_t)text.size, stats.tokens - before, seconds};
        stats.bytes += text.size;
        if (stats.documents % 5000 == 0)
            fprintf(stderr, "text documents: %llu\n", (unsigned long long)stats.documents);
    }
    bool ok = !ferror(f);
    if (!ok)
        perror(manifest);
    free(line);
    fclose(f);
    return ok;
}
#endif
