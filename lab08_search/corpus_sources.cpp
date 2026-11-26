#include "corpus_sources.h"
#include <errno.h>

namespace web {
namespace {
struct Sample {
    Text title, url, doi;
};
struct Journal {
    Text name;
    unsigned count = 0;
    int year_min = 9999, year_max = 0;
    Sample samples[3];
};
void quote(Text &out, const char *text) {
    out.letter('"');
    for (; *text; ++text) {
        unsigned char c = (unsigned char)*text;
        if (c == '"' || c == '\\') { out.letter('\\'); out.letter(c); }
        else if (c < 32) out.format("\\u%04x", c);
        else out.letter(c);
    }
    out.letter('"');
}
int split(char *line, char **fields, int capacity) {
    size_t n = strlen(line);
    while (n && (line[n-1] == '\n' || line[n-1] == '\r')) line[--n] = 0;
    int count = 1; fields[0] = line;
    for (char *p = line; *p; ++p) if (*p == '\t') {
        if (count == capacity) return -1;
        *p = 0; fields[count++] = p + 1;
    }
    return count;
}
bool parse_year(const char *text, int &year) {
    char *end = NULL; errno = 0; long value = strtol(text, &end, 10);
    if (errno || end == text || *end || value < 1000 || value > 9999) return false;
    year = (int)value; return true;
}
bool safe_url(const char *url) {
    return !strncmp(url, "https://", 8) && url[8];
}
int catalog(Text &body) {
    const char *path = "lab01_corpus/data/provenance/scoap3.tsv";
    FILE *file = fopen(path, "r");
    if (!file) return 500;
    Journal journals[64]; int used = 0; unsigned total = 0, xml = 0, pdf = 0;
    char *line = NULL; size_t capacity = 0;
    bool valid = getline(&line, &capacity, file) > 0;
    const char *header = "file\trecord_id\tdoi\ttitle\tjournal\tyear\turl\txml_url\tpdf_url\tsource_format\tlicense\tsource_bytes\twords\tsha256\n";
    valid = valid && !strcmp(line, header);
    while (valid && getline(&line, &capacity, file) >= 0) {
        char *fields[14]; int year;
        if (split(line, fields, 14) != 14 || !*fields[4] || !parse_year(fields[5], year) || !safe_url(fields[6])) { valid = false; break; }
        int index = 0;
        while (index < used && strcmp(journals[index].name.data, fields[4])) ++index;
        if (index == used) {
            if (used == 64) { valid = false; break; }
            journals[used++].name.add(fields[4]);
        }
        Journal &journal = journals[index];
        if (journal.count < 3) {
            Sample &sample = journal.samples[journal.count];
            sample.title.add(fields[3]); sample.url.add(fields[6]); sample.doi.add(fields[2]);
        }
        ++journal.count; ++total;
        if (year < journal.year_min) journal.year_min = year;
        if (year > journal.year_max) journal.year_max = year;
        if (!strcmp(fields[9], "xml")) ++xml;
        else if (!strcmp(fields[9], "pdf")) ++pdf;
        else { valid = false; break; }
    }
    if (ferror(file)) valid = false;
    free(line); fclose(file);
    if (!valid || !total) return 500;
    body.format("{\"total\":%u,\"xml\":%u,\"pdf\":%u,\"repository\":\"https://repo.scoap3.org/\",\"manifest\":", total, xml, pdf);
    quote(body, path); body.add(",\"journals\":[");
    int order[64]; for (int i = 0; i < used; ++i) order[i] = i;
    for (int i = 1; i < used; ++i) {
        int value = order[i], j = i;
        while (j && journals[order[j-1]].count < journals[value].count) { order[j] = order[j-1]; --j; }
        order[j] = value;
    }
    for (int i = 0; i < used; ++i) {
        Journal &journal = journals[order[i]];
        if (i) body.letter(',');
        body.add("{\"name\":"); quote(body, journal.name.data);
        body.format(",\"count\":%u,\"share\":%.6f,\"year_min\":%d,\"year_max\":%d,\"samples\":[", journal.count, 100.0 * journal.count / total, journal.year_min, journal.year_max);
        for (unsigned j = 0; j < journal.count && j < 3; ++j) {
            if (j) body.letter(',');
            Sample &sample = journal.samples[j];
            body.add("{\"title\":"); quote(body, sample.title.data);
            body.add(",\"url\":"); quote(body, sample.url.data);
            body.add(",\"doi\":"); quote(body, sample.doi.data); body.letter('}');
        }
        body.add("]}");
    }
    body.add("]}"); return 200;
}
}
bool corpus_sources_route(const char *path, Text &body, const char *&type, int &status) {
    if (strcmp(path, "/api/sources")) return false;
    type = "application/json; charset=utf-8"; status = catalog(body);
    if (status != 200) body.add("{\"error\":\"Не удалось прочитать каталог источников\"}");
    return true;
}
int corpus_sources_tests() {
    char row[] = "one\ttwo\t\n"; char *fields[3]; int year = 0;
    int errors = split(row, fields, 3) != 3 || strcmp(fields[1], "two") || *fields[2];
    if (parse_year("2020bad", year) || !parse_year("2020", year) || year != 2020) ++errors;
    if (safe_url("javascript:alert(1)") || !safe_url("https://repo.scoap3.org/")) ++errors;
    fprintf(stderr, "corpus source helpers: %s (%d failures)\n", errors ? "FAILED" : "OK", errors);
    return errors;
}
}
