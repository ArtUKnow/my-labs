#ifndef IR_LAB03_TOKENIZER_STATISTICS_H
#define IR_LAB03_TOKENIZER_STATISTICS_H
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
    Stats()
        : tokens(0), length(0), bytes(0), documents(0), words(0), numbers(0), mixed(0),
          points(NULL), cap(0), example_count(0) {
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
int write_stats(const char *output, const Stats &s, double elapsed) {
    FILE *f = fopen(output, "w");
    if (!f) {
        perror(output);
        return 1;
    }
    double token_time = 0;
    for (size_t i = 0; i < s.documents; i++)
        token_time += s.points[i].seconds;
    fprintf(f,
            "{\n  \"implementation\": \"C++ without STL\",\n  \"documents\": %llu,\n "
            " \"tokens\": %llu,\n  \"avg_token_length\": %.6f,\n  \"input_bytes\": "
            "%llu,\n  \"time_seconds\": %.6f,\n  \"wall_time_seconds\": %.6f,\n  "
            "\"kb_per_second\": %.6f,\n  \"kinds\": {\"word\": %llu, \"number\": "
            "%llu, \"mixed\": %llu},\n  \"timing_points\": [",
            (unsigned long long)s.documents, (unsigned long long)s.tokens,
            s.tokens ? (double)s.length / s.tokens : 0, (unsigned long long)s.bytes, token_time,
            elapsed, token_time ? s.bytes / 1024.0 / token_time : 0, (unsigned long long)s.words,
            (unsigned long long)s.numbers, (unsigned long long)s.mixed);
    for (size_t i = 0; i < s.documents; i++)
        fprintf(f, "%s{\"bytes\":%llu,\"tokens\":%llu,\"ms\":%.6f}", i ? "," : "",
                (unsigned long long)s.points[i].bytes, (unsigned long long)s.points[i].tokens,
                s.points[i].seconds * 1000);
    fputs("],\n  \"long_tokens\": [", f);
    for (size_t i = 0; i < s.example_count; i++) {
        if (i)
            fputc(',', f);
        json_string(f, s.examples[i]);
    }
    fputs("],\n  \"timing_scope\": \"time_seconds measures tokenization and "
          "callback statistics only; wall_time_seconds additionally includes "
          "manifest and gzip input\",\n  \"unicode\": \"UTF-8 decoding; Unicode "
          "16.0.0 alphabetic/digit tables and lowercase mappings; no locale "
          "dependency\"\n}\n",
          f);
    return fclose(f) ? 1 : 0;
}
#endif
