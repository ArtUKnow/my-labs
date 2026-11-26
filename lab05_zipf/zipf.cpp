#include "../common/text.h"
#include "../common/text_support.h"

namespace {
struct Entry {
    char *key;
    uint64_t count;
};
struct Frequencies {
    Entry *slots;
    size_t capacity, used;
    Frequencies() : slots(NULL), capacity(0), used(0) {
        grow(2048);
    }
    ~Frequencies() {
        for (size_t i = 0; i < capacity; i++)
            free(slots[i].key);
        free(slots);
    }
    void grow(size_t n) {
        Entry *old = slots;
        size_t oldn = capacity;
        slots = (Entry *)checked(calloc(n, sizeof(Entry)));
        capacity = n;
        for (size_t i = 0; i < oldn; i++)
            if (old[i].key) {
                size_t at = hash(old[i].key) & (capacity - 1);
                while (slots[at].key)
                    at = (at + 1) & (capacity - 1);
                slots[at] = old[i];
            }
        free(old);
    }
    void add(const char *key) {
        if ((used + 1) * 10 >= capacity * 7)
            grow(capacity * 2);
        size_t at = hash(key) & (capacity - 1);
        while (slots[at].key && strcmp(slots[at].key, key))
            at = (at + 1) & (capacity - 1);
        if (!slots[at].key) {
            slots[at].key = clone(key);
            used++;
        }
        slots[at].count++;
    }
};
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
void merge_entries(Entry **a, Entry **temp, size_t start, size_t end) {
    if (end - start < 2)
        return;
    size_t middle = start + (end - start) / 2;
    merge_entries(a, temp, start, middle);
    merge_entries(a, temp, middle, end);
    size_t i = start, j = middle, k = start;
    while (i < middle && j < end) {
        bool left = a[i]->count > a[j]->count ||
                    (a[i]->count == a[j]->count && strcmp(a[i]->key, a[j]->key) < 0);
        temp[k++] = left ? a[i++] : a[j++];
    }
    while (i < middle)
        temp[k++] = a[i++];
    while (j < end)
        temp[k++] = a[j++];
    for (i = start; i < end; i++)
        a[i] = temp[i];
}
int zipf(const char *manifest, const char *output, const char *svg) {
    Stats stats;
    Frequencies freq;
    stats.frequencies = &freq;
    double begin = now();
    if (!corpus(manifest, stats))
        return 1;
    Entry **ranked = (Entry **)checked(malloc(freq.used * sizeof(Entry *)));
    Entry **temp = (Entry **)checked(malloc(freq.used * sizeof(Entry *)));
    size_t n = 0;
    uint64_t hapax = 0;
    for (size_t i = 0; i < freq.capacity; i++)
        if (freq.slots[i].key) {
            ranked[n++] = &freq.slots[i];
            if (freq.slots[i].count == 1)
                hapax++;
        }
    merge_entries(ranked, temp, 0, n);
    free(temp);
    double sx = 0, sy = 0, sxx = 0, sxy = 0;
    for (size_t i = 0; i < n; i++) {
        double x = log((double)i + 1), y = log((double)ranked[i]->count);
        sx += x;
        sy += y;
        sxx += x * x;
        sxy += x * y;
    }
    double denominator = n * sxx - sx * sx;
    double exponent = denominator ? -(n * sxy - sx * sy) / denominator : 1;
    double c = n ? exp((sy + exponent * sx) / n) : 0;
    double classical = n ? (double)ranked[0]->count : 0;
    double error = 0;
    for (size_t i = 0; i < n; i++) {
        double d = log((double)ranked[i]->count) - log(c) + exponent * log((double)i + 1);
        error += d * d;
    }
    if (n)
        error /= n;
    FILE *f = fopen(output, "w");
    if (!f) {
        perror(output);
        free(ranked);
        return 1;
    }
    fprintf(f,
            "{\n\"implementation\":\"C++ custom hash table and merge "
            "sort\",\n\"documents\":%llu,\n\"total_tokens\":%llu,\n\"unique_terms\":%"
            "zu,\n\"hapax_legomena\":%llu,\n\"hapax_share\":%.8f,\n\"time_seconds\":%"
            ".6f,\n\"zipf_classical\":{\"c\":%.8f,\"s\":1},\n\"zipf_fit\":{\"c\":%."
            "8f,\"s\":%.8f,\"log_mse\":%.8f},\n\"top_terms\":[",
            (unsigned long long)stats.documents, (unsigned long long)stats.tokens, n,
            (unsigned long long)hapax, n ? (double)hapax / n : 0, now() - begin, classical, c,
            exponent, error);
    for (size_t i = 0; i < n && i < 30; i++) {
        if (i)
            fputc(',', f);
        fputs("{\"term\":", f);
        json_string(f, ranked[i]->key);
        fprintf(f, ",\"freq\":%llu}", (unsigned long long)ranked[i]->count);
    }
    fputs("],\n\"points\":[", f);
    size_t last = (size_t)-1;
    for (size_t point = 0; point < 400 && n; point++) {
        size_t at = (size_t)floor(exp(log((double)n) * point / 399.0)) - 1;
        if (at >= n)
            at = n - 1;
        if (at == last)
            continue;
        if (last != (size_t)-1)
            fputc(',', f);
        fprintf(f, "{\"rank\":%zu,\"freq\":%llu,\"zipf\":%.8f,\"fit\":%.8f}", at + 1,
                (unsigned long long)ranked[at]->count, classical / (at + 1),
                c / pow(at + 1, exponent));
        last = at;
    }
    fputs("]\n}\n", f);
    if (fclose(f)) {
        free(ranked);
        return 1;
    }
    f = fopen(svg, "w");
    if (!f) {
        perror(svg);
        free(ranked);
        return 1;
    }
    fputs("<svg xmlns=\"http://www.w3.org/2000/svg\" width=\"900\" "
          "height=\"560\" viewBox=\"0 0 900 560\"><rect width=\"900\" "
          "height=\"560\" fill=\"white\"/><g font-family=\"sans-serif\" "
          "font-size=\"14\"><text x=\"65\" y=\"25\">Term frequencies: log rank / "
          "log frequency</text><text x=\"65\" y=\"545\">Blue: corpus; red: "
          "classical c/r; green: fitted c/r^s</text>",
          f);
    double xmax = log10((double)(n > 1 ? n : 2)), ymax = log10(classical > 1 ? classical : 2);
    for (int e = 0; e <= (int)ceil(xmax); e++) {
        double x = 65 + 760 * e / xmax;
        fprintf(f,
                "<path d=\"M %.2f 45 V 495\" stroke=\"#ddd\"/><text x=\"%.2f\" "
                "y=\"520\">10^%d</text>",
                x, x, e);
    }
    for (int e = 0; e <= (int)ceil(ymax); e++) {
        double y = 495 - 450 * e / ymax;
        fprintf(f,
                "<path d=\"M 65 %.2f H 825\" stroke=\"#ddd\"/><text x=\"10\" "
                "y=\"%.2f\">10^%d</text>",
                y, y, e);
    }
    const char *colors[] = {"#2563eb", "#dc2626", "#16a34a"};
    for (int curve = 0; curve < 3; curve++) {
        fprintf(f, "<polyline fill=\"none\" stroke=\"%s\" stroke-width=\"2\" points=\"",
                colors[curve]);
        last = (size_t)-1;
        for (size_t point = 0; point < 400 && n; point++) {
            size_t at = (size_t)floor(exp(log((double)n) * point / 399.0)) - 1;
            if (at >= n)
                at = n - 1;
            if (at == last)
                continue;
            double rank = at + 1, y = curve == 0   ? (double)ranked[at]->count
                                      : curve == 1 ? classical / rank
                                                   : c / pow(rank, exponent);
            fprintf(f, "%.2f,%.2f ", 65 + 760 * log10(rank) / xmax, 495 - 450 * log10(y) / ymax);
            last = at;
        }
        fputs("\"/>", f);
    }
    fputs("</g></svg>\n", f);
    int result = fclose(f) ? 1 : 0;
    free(ranked);
    return result;
}
}
int ir_zipf_main(int argc, char **argv) {
    if (argc == 4 && !strcmp(argv[0], "zipf"))
        return zipf(argv[1], argv[2], argv[3]);
    fprintf(stderr, "usage: zipf DOCUMENTS_TSV OUTPUT_JSON OUTPUT_SVG\n");
    return 2;
}
