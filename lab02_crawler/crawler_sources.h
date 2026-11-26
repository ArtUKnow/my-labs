#ifndef IR_CRAWLER_SOURCES_H
#define IR_CRAWLER_SOURCES_H
#include "crawler_config.h"
char *normalize_url(const char *value) {
    CURLU *url = curl_url();
    if (!url || curl_url_set(url, CURLUPART_URL, value, 0) != CURLUE_OK) {
        if (url)
            curl_url_cleanup(url);
        return NULL;
    }
    char *scheme = NULL, *host = NULL, *normal = NULL;
    if (curl_url_get(url, CURLUPART_SCHEME, &scheme, 0) != CURLUE_OK ||
        curl_url_get(url, CURLUPART_HOST, &host, 0) != CURLUE_OK) {
        curl_free(scheme);
        curl_free(host);
        curl_url_cleanup(url);
        return NULL;
    }
    for (char *at = scheme; *at; ++at)
        *at = (char)tolower((unsigned char)*at);
    for (char *at = host; *at; ++at)
        *at = (char)tolower((unsigned char)*at);
    bool allowed = !strcmp(scheme, "https") || !strcmp(scheme, "http");
    curl_url_set(url, CURLUPART_SCHEME, scheme, 0);
    curl_url_set(url, CURLUPART_HOST, host, 0);
    curl_url_set(url, CURLUPART_FRAGMENT, NULL, 0);
    char *result = NULL;
    if (allowed && curl_url_get(url, CURLUPART_URL, &normal, CURLU_NO_DEFAULT_PORT) == CURLUE_OK)
        result = copy_text(normal);
    curl_free(scheme);
    curl_free(host);
    curl_free(normal);
    curl_url_cleanup(url);
    return result;
}
struct Source {
    char *url, *record_id, *record_url, *journal, *format;
    Source() : url(NULL), record_id(NULL), record_url(NULL), journal(NULL), format(NULL) {
    }
    ~Source() {
        free(url);
        free(record_id);
        free(record_url);
        free(journal);
        free(format);
    }
};
struct Sources {
    Source **items;
    size_t size, capacity;
    size_t *slots, slot_count;
    Sources() : items(NULL), size(0), capacity(0), slots(NULL), slot_count(0) {
    }
    ~Sources() {
        for (size_t i = 0; i < size; ++i)
            delete items[i];
        free(items);
        free(slots);
    }
    bool add(Source *source) {
        if (size == capacity) {
            size_t next = capacity ? capacity * 2 : 256;
            void *memory = realloc(items, next * sizeof(Source *));
            if (!memory)
                return false;
            items = (Source **)memory;
            capacity = next;
        }
        items[size++] = source;
        return true;
    }
    uint64_t hash(const char *url) const {
        uint64_t value = 1469598103934665603ULL;
        for (; *url; ++url) {
            value ^= (unsigned char)*url;
            value *= 1099511628211ULL;
        }
        return value;
    }
    bool index() {
        slot_count = 16;
        while (slot_count < size * 2)
            slot_count *= 2;
        slots = (size_t *)calloc(slot_count, sizeof(size_t));
        if (!slots)
            return false;
        for (size_t i = 0; i < size; ++i) {
            size_t at = hash(items[i]->url) & (slot_count - 1);
            while (slots[at]) {
                if (!strcmp(items[slots[at] - 1]->url, items[i]->url)) {
                    fprintf(stderr, "crawler: duplicate URL in manifest\n");
                    return false;
                }
                at = (at + 1) & (slot_count - 1);
            }
            slots[at] = i + 1;
        }
        return true;
    }
    Source *find(const char *url) const {
        size_t at = hash(url) & (slot_count - 1);
        while (slots[at]) {
            Source *source = items[slots[at] - 1];
            if (!strcmp(source->url, url))
                return source;
            at = (at + 1) & (slot_count - 1);
        }
        return NULL;
    }
};
struct Fields {
    char **items;
    size_t size, capacity;
    Fields() : items(NULL), size(0), capacity(0) {
    }
    ~Fields() {
        clear();
        free(items);
    }
    void clear() {
        for (size_t i = 0; i < size; ++i)
            free(items[i]);
        size = 0;
    }
    bool add(const char *value) {
        if (size == capacity) {
            size_t next = capacity ? capacity * 2 : 16;
            void *memory = realloc(items, next * sizeof(char *));
            if (!memory)
                return false;
            items = (char **)memory;
            capacity = next;
        }
        items[size++] = copy_text(value);
        return true;
    }
};
bool read_tsv(FILE *file, Fields &fields) {
    fields.clear();
    errno = 0;
    Text field;
    bool quoted = false, started = false;
    for (;;) {
        int ch = fgetc(file);
        if (ch == EOF) {
            if (!started && !field.size && !fields.size)
                return false;
            if (quoted) {
                fprintf(stderr, "crawler: unclosed quoted field\n");
                errno = EINVAL;
                return false;
            }
            return fields.add(field.data ? field.data : "");
        }
        started = true;
        if (quoted) {
            if (ch == '"') {
                int next = fgetc(file);
                if (next == '"') {
                    if (!field.add("\"", 1))
                        return false;
                } else {
                    quoted = false;
                    if (next != EOF)
                        ungetc(next, file);
                }
            } else {
                char value = (char)ch;
                if (!field.add(&value, 1))
                    return false;
            }
            continue;
        }
        if (ch == '"' && !field.size) {
            quoted = true;
            continue;
        }
        if (ch == '\t' || ch == '\n' || ch == '\r') {
            if (!fields.add(field.data ? field.data : ""))
                return false;
            field.size = 0;
            if (field.data)
                field.data[0] = 0;
            if (ch == '\t')
                continue;
            if (ch == '\r') {
                int next = fgetc(file);
                if (next != '\n' && next != EOF)
                    ungetc(next, file);
            }
            return true;
        }
        char value = (char)ch;
        if (!field.add(&value, 1))
            return false;
    }
}
int column(const Fields &header, const char *name) {
    for (size_t i = 0; i < header.size; ++i)
        if (!strcmp(header.items[i], name))
            return (int)i;
    return -1;
}
bool add_source(Sources &sources, const Fields &row, int url, int record, int record_url,
                int journal, const char *format) {
    Source *source = new Source;
    source->url = normalize_url(row.items[url]);
    source->record_id = copy_text(row.items[record]);
    source->record_url = copy_text(row.items[record_url]);
    source->journal = copy_text(row.items[journal]);
    source->format = copy_text(format);
    if (!source->url || !sources.add(source)) {
        delete source;
        return false;
    }
    return true;
}
bool read_sources(const Config &config, Sources &sources) {
    FILE *file = fopen(config.manifest, "r");
    if (!file) {
        perror(config.manifest);
        return false;
    }
    Fields header, row;
    if (!read_tsv(file, header)) {
        fclose(file);
        return false;
    }
    int id = column(header, "record_id"), url = column(header, "url"),
        journal = column(header, "journal"), format = column(header, "source_format"),
        xml = column(header, "xml_url"), pdf = column(header, "pdf_url");
    bool good = id >= 0 && url >= 0 && journal >= 0 && format >= 0 && xml >= 0 && pdf >= 0;
    while (good && read_tsv(file, row)) {
        if (row.size != header.size) {
            good = false;
            break;
        }
        if (config.samples_only) {
            bool found = false;
            for (size_t i = 0; i < sources.size; ++i)
                if (!strcmp(sources.items[i]->journal, row.items[journal]))
                    found = true;
            if (found)
                continue;
        }
        int body = !strcmp(row.items[format], "xml")   ? xml
                   : !strcmp(row.items[format], "pdf") ? pdf
                                                       : -1;
        if (body < 0 || !*row.items[body]) {
            good = false;
            break;
        }
        good = add_source(sources, row, body, id, url, journal, row.items[format]);
        if (good && config.include_landing)
            good = add_source(sources, row, url, id, url, journal, "html");
    }
    if (ferror(file) || errno == EINVAL)
        good = false;
    fclose(file);
    return good && sources.size && sources.index();
}

#endif
