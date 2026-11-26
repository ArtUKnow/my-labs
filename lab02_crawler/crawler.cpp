#include "crawler.h"
#include <ctype.h>
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <sqlite3.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/sha.h>
#endif

namespace {
struct Text {
    char *data;
    size_t size;
    size_t capacity;
    Text() : data(NULL), size(0), capacity(0) {
    }
    ~Text() {
        free(data);
    }
    bool add(const void *value, size_t length) {
        if (length > SIZE_MAX - size - 1)
            return false;
        if (size + length + 1 > capacity) {
            size_t next = capacity ? capacity : 256;
            while (next < size + length + 1) {
                if (next > SIZE_MAX / 2)
                    return false;
                next *= 2;
            }
            void *memory = realloc(data, next);
            if (!memory)
                return false;
            data = (char *)memory;
            capacity = next;
        }
        memcpy(data + size, value, length);
        size += length;
        data[size] = 0;
        return true;
    }
};
char *copy_text(const char *value) {
    char *result = strdup(value ? value : "");
    if (!result) {
        fprintf(stderr, "crawler: out of memory\n");
        exit(1);
    }
    return result;
}
void replace_text(char *&target, const char *value) {
    free(target);
    target = copy_text(value);
}
char *trim(char *value) {
    while (*value && isspace((unsigned char)*value))
        ++value;
    size_t length = strlen(value);
    while (length && isspace((unsigned char)value[length - 1]))
        value[--length] = 0;
    return value;
}
char *folder_of(const char *path) {
    char *result = copy_text(path);
    char *slash = strrchr(result, '/');
    if (slash)
        *slash = 0;
    else {
        free(result);
        result = copy_text(".");
    }
    return result;
}
char *join_path(const char *folder, const char *name) {
    if (name[0] == '/')
        return copy_text(name);
    Text result;
    result.add(folder, strlen(folder));
    result.add("/", 1);
    result.add(name, strlen(name));
    return copy_text(result.data);
}
bool make_folders(const char *path) {
    char *folder = folder_of(path);
    for (char *at = folder + 1; *at; ++at) {
        if (*at != '/')
            continue;
        *at = 0;
        if (mkdir(folder, 0755) && errno != EEXIST) {
            free(folder);
            return false;
        }
        *at = '/';
    }
    bool good = !mkdir(folder, 0755) || errno == EEXIST;
    free(folder);
    return good;
}
struct Config {
    char *database, *manifest, *schema;
    double delay;
    long timeout, recrawl_after, retry_delay, max_pages, max_source_bytes, workers;
    bool include_landing, samples_only;
    Config()
        : database(NULL), manifest(NULL), schema(NULL), delay(1), timeout(30), recrawl_after(86400),
          retry_delay(60), max_pages(0), max_source_bytes(67108864), workers(1),
          include_landing(true), samples_only(false) {
    }
    ~Config() {
        free(database);
        free(manifest);
        free(schema);
    }
};
bool integer_value(const char *text, long &value) {
    char *end;
    errno = 0;
    long parsed = strtol(text, &end, 10);
    if (errno || end == text || *end || parsed < 0)
        return false;
    value = parsed;
    return true;
}
bool read_config(const char *path, Config &config) {
    FILE *file = fopen(path, "r");
    if (!file) {
        perror(path);
        return false;
    }
    char *line = NULL;
    size_t capacity = 0;
    char section[64] = "";
    bool good = true;
    char *database = NULL, *manifest = NULL, *schema = NULL;
    while (getline(&line, &capacity, file) >= 0) {
        if (!line[0] || line[0] == '\n' || line[0] == '#')
            continue;
        bool indented = isspace((unsigned char)line[0]);
        char *key = trim(line);
        if (!*key || *key == '#')
            continue;
        char *colon = strchr(key, ':');
        if (!colon) {
            good = false;
            break;
        }
        *colon = 0;
        char *value = trim(colon + 1);
        size_t value_length = strlen(value);
        if (value_length >= 2 && ((value[0] == '"' && value[value_length - 1] == '"') ||
                                  (value[0] == '\'' && value[value_length - 1] == '\''))) {
            value[value_length - 1] = 0;
            ++value;
        }
        if (!indented) {
            if (*value || strlen(key) >= sizeof(section)) {
                good = false;
                break;
            }
            strcpy(section, key);
            continue;
        }
        trim(key);
        if (!strcmp(section, "db") && !strcmp(key, "path"))
            replace_text(database, value);
        else if (!strcmp(section, "db") && !strcmp(key, "schema"))
            replace_text(schema, value);
        else if (!strcmp(section, "corpus") && !strcmp(key, "manifest"))
            replace_text(manifest, value);
        else if (!strcmp(section, "corpus") && !strcmp(key, "include_landing")) {
            if (strcmp(value, "true") && strcmp(value, "false"))
                good = false;
            config.include_landing = !strcmp(value, "true");
        } else if (!strcmp(section, "corpus") && !strcmp(key, "samples_only")) {
            if (strcmp(value, "true") && strcmp(value, "false"))
                good = false;
            config.samples_only = !strcmp(value, "true");
        } else if (!strcmp(section, "logic") && !strcmp(key, "delay")) {
            char *end;
            errno = 0;
            config.delay = strtod(value, &end);
            if (errno || end == value || *end || !isfinite(config.delay) || config.delay < 0 ||
                config.delay > 86400)
                good = false;
        } else if (!strcmp(section, "logic")) {
            long *number = NULL;
            if (!strcmp(key, "workers"))
                number = &config.workers;
            else if (!strcmp(key, "timeout"))
                number = &config.timeout;
            else if (!strcmp(key, "recrawl_after"))
                number = &config.recrawl_after;
            else if (!strcmp(key, "retry_delay"))
                number = &config.retry_delay;
            else if (!strcmp(key, "max_pages"))
                number = &config.max_pages;
            else if (!strcmp(key, "max_source_bytes"))
                number = &config.max_source_bytes;
            if (number && !integer_value(value, *number))
                good = false;
        }
        if (!good)
            break;
    }
    free(line);
    fclose(file);
    if (!database || !manifest || !config.timeout || !config.max_source_bytes ||
        config.workers != 1 || config.max_source_bytes > INT_MAX)
        good = false;
    if (!good)
        fprintf(stderr, "crawler: invalid config; workers must be 1 for the sequential crawler\n");
    if (good) {
        char *folder = folder_of(path);
        config.database = join_path(folder, database);
        config.manifest = join_path(folder, manifest);
        config.schema = join_path(folder, schema ? schema : "sql/schema.sql");
        free(folder);
    }
    free(database);
    free(manifest);
    free(schema);
    return good;
}
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
bool execute(sqlite3 *database, const char *sql) {
    char *error = NULL;
    int status = sqlite3_exec(database, sql, NULL, NULL, &error);
    if (status != SQLITE_OK)
        fprintf(stderr, "crawler database: %s\n", error ? error : sqlite3_errmsg(database));
    sqlite3_free(error);
    return status == SQLITE_OK;
}
bool prepare(sqlite3 *database, const char *sql, sqlite3_stmt **statement) {
    if (sqlite3_prepare_v2(database, sql, -1, statement, NULL) != SQLITE_OK) {
        fprintf(stderr, "crawler database: %s\n", sqlite3_errmsg(database));
        return false;
    }
    return true;
}
void bind_text(sqlite3_stmt *statement, int column, const char *text) {
    sqlite3_bind_text(statement, column, text ? text : "", -1, SQLITE_TRANSIENT);
}
char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file)
        return NULL;
    Text content;
    char block[4096];
    size_t size;
    while ((size = fread(block, 1, sizeof(block), file)))
        if (!content.add(block, size)) {
            fclose(file);
            return NULL;
        }
    bool good = !ferror(file);
    fclose(file);
    return good ? copy_text(content.data ? content.data : "") : NULL;
}
char *read_sql(const Config &config, const char *name) {
    char *folder = folder_of(config.schema);
    char *path = join_path(folder, name);
    char *sql = read_file(path);
    if (!sql)
        perror(path);
    free(folder);
    free(path);
    return sql;
}
bool open_database(const Config &config, sqlite3 **database, int &lock) {
    if (!make_folders(config.database))
        return false;
    char *lock_path = (char *)malloc(strlen(config.database) + 6);
    if (!lock_path)
        return false;
    snprintf(lock_path, strlen(config.database) + 6, "%s.lock", config.database);
    lock = open(lock_path, O_RDWR | O_CREAT, 0600);
    free(lock_path);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB)) {
        fprintf(stderr, "crawler: another process owns the database\n");
        if (lock >= 0)
            close(lock);
        lock = -1;
        return false;
    }
    if (sqlite3_open(config.database, database) != SQLITE_OK) {
        fprintf(stderr, "crawler database: %s\n", sqlite3_errmsg(*database));
        return false;
    }
    sqlite3_busy_timeout(*database, 30000);
    char *schema = read_file(config.schema);
    if (!schema) {
        perror(config.schema);
        return false;
    }
    bool good = execute(*database, schema);
    free(schema);
    return good &&
           execute(*database, "UPDATE raw_queue SET state='pending' WHERE state='inflight'");
}
bool plan_queue(sqlite3 *database, const Sources &sources, const Config &config) {
    sqlite3_stmt *old = NULL, *insert = NULL;
    char *sql = read_sql(config, "plan_recrawl.sql");
    bool good = sql &&
                prepare(database, "SELECT checked_at FROM raw_documents WHERE url=?", &old) &&
                prepare(database, sql, &insert);
    free(sql);
    if (good)
        good = execute(database, "BEGIN IMMEDIATE");
    time_t now = time(NULL);
    for (size_t i = 0; good && i < sources.size; ++i) {
        Source *source = sources.items[i];
        sqlite3_reset(old);
        bind_text(old, 1, source->url);
        int status = sqlite3_step(old);
        if (status == SQLITE_ROW && sqlite3_column_int64(old, 0) > now - config.recrawl_after)
            continue;
        if (status != SQLITE_ROW && status != SQLITE_DONE) {
            good = false;
            break;
        }
        sqlite3_reset(insert);
        bind_text(insert, 1, source->url);
        bind_text(insert, 2, "{}");
        sqlite3_bind_int64(insert, 3, now);
        good = sqlite3_step(insert) == SQLITE_DONE;
    }
    if (good)
        good = execute(database, "COMMIT");
    else
        execute(database, "ROLLBACK");
    sqlite3_finalize(old);
    sqlite3_finalize(insert);
    return good;
}
struct Previous {
    bool exists;
    char *hash, *etag, *modified;
    Previous() : exists(false), hash(NULL), etag(NULL), modified(NULL) {
    }
    ~Previous() {
        free(hash);
        free(etag);
        free(modified);
    }
};
bool previous_document(sqlite3 *database, const char *url, Previous &previous) {
    sqlite3_stmt *statement = NULL;
    if (!prepare(database, "SELECT sha256,etag,last_modified FROM raw_documents WHERE url=?",
                 &statement))
        return false;
    bind_text(statement, 1, url);
    int status = sqlite3_step(statement);
    if (status == SQLITE_ROW) {
        previous.exists = true;
        previous.hash = copy_text((const char *)sqlite3_column_text(statement, 0));
        previous.etag = copy_text((const char *)sqlite3_column_text(statement, 1));
        previous.modified = copy_text((const char *)sqlite3_column_text(statement, 2));
    }
    sqlite3_finalize(statement);
    return status == SQLITE_ROW || status == SQLITE_DONE;
}
struct Response {
    Text body;
    char *type, *etag, *modified;
    size_t limit;
    long status;
    bool too_large;
    Response(size_t max)
        : type(NULL), etag(NULL), modified(NULL), limit(max), status(0), too_large(false) {
    }
    ~Response() {
        free(type);
        free(etag);
        free(modified);
    }
};
size_t receive_body(char *data, size_t size, size_t count, void *state) {
    Response *response = (Response *)state;
    if (size && count > SIZE_MAX / size)
        return 0;
    size_t length = size * count;
    if (length > response->limit - response->body.size) {
        response->too_large = true;
        return 0;
    }
    return response->body.add(data, length) ? length : 0;
}
size_t receive_header(char *data, size_t size, size_t count, void *state) {
    Response *response = (Response *)state;
    size_t length = size * count;
    char *line = (char *)malloc(length + 1);
    if (!line)
        return 0;
    memcpy(line, data, length);
    line[length] = 0;
    char *colon = strchr(line, ':');
    if (colon) {
        *colon = 0;
        char *value = trim(colon + 1);
        if (!strcasecmp(line, "ETag"))
            replace_text(response->etag, value);
        else if (!strcasecmp(line, "Last-Modified"))
            replace_text(response->modified, value);
        else if (!strcasecmp(line, "Content-Type"))
            replace_text(response->type, value);
    }
    free(line);
    return length;
}
bool fetch(const Source &source, const Previous &previous, const Config &config, Response &response,
           char *error) {
    CURL *curl = curl_easy_init();
    if (!curl)
        return false;
    struct curl_slist *headers = NULL;
    Text etag, modified;
    if (previous.etag && *previous.etag) {
        etag.add("If-None-Match: ", 15);
        etag.add(previous.etag, strlen(previous.etag));
        headers = curl_slist_append(headers, etag.data);
    }
    if (previous.modified && *previous.modified) {
        modified.add("If-Modified-Since: ", 19);
        modified.add(previous.modified, strlen(previous.modified));
        headers = curl_slist_append(headers, modified.data);
    }
    curl_easy_setopt(curl, CURLOPT_URL, source.url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "MAI-IR-coursework/3.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config.timeout);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, receive_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
    CURLcode status = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (status != CURLE_OK) {
        if (response.too_large)
            snprintf(error, CURL_ERROR_SIZE, "source exceeds max_source_bytes");
        return false;
    }
    if (response.status != 304 && (response.status < 200 || response.status >= 300)) {
        snprintf(error, CURL_ERROR_SIZE, "HTTP %ld", response.status);
        return false;
    }
    return true;
}
void sha256(const char *data, size_t size, char result[65]) {
    unsigned char digest[32];
#ifdef __APPLE__
    CC_SHA256(data, (CC_LONG)size, digest);
#else
    SHA256((const unsigned char *)data, size, digest);
#endif
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < 32; ++i) {
        result[2 * i] = digits[digest[i] >> 4];
        result[2 * i + 1] = digits[digest[i] & 15];
    }
    result[64] = 0;
}
bool gzip_body(const Text &body, Text &compressed) {
    z_stream stream = {};
    if (deflateInit2(&stream, 6, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return false;
    stream.next_in = (Bytef *)body.data;
    stream.avail_in = (uInt)body.size;
    char block[32768];
    int status;
    do {
        stream.next_out = (Bytef *)block;
        stream.avail_out = sizeof(block);
        status = deflate(&stream, Z_FINISH);
        if (status != Z_OK && status != Z_STREAM_END) {
            deflateEnd(&stream);
            return false;
        }
        if (!compressed.add(block, sizeof(block) - stream.avail_out)) {
            deflateEnd(&stream);
            return false;
        }
    } while (status != Z_STREAM_END);
    deflateEnd(&stream);
    return true;
}
bool save_response(sqlite3 *database, const Source &source, const Previous &previous,
                   const Response &response, const Config &config, const char *&result) {
    bool unchanged = response.status == 304;
    char hash[65] = "";
    if (unchanged && !previous.exists) {
        fprintf(stderr, "crawler: 304 without stored body\n");
        return false;
    }
    if (!unchanged) {
        sha256(response.body.data, response.body.size, hash);
        unchanged = previous.exists && !strcmp(hash, previous.hash);
    }
    sqlite3_stmt *statement = NULL;
    time_t now = time(NULL);
    bool good = true;
    if (unchanged) {
        good = prepare(database, "UPDATE raw_documents SET checked_at=? WHERE url=?", &statement);
        if (good) {
            sqlite3_bind_int64(statement, 1, now);
            bind_text(statement, 2, source.url);
            good = sqlite3_step(statement) == SQLITE_DONE;
        }
        result = "unchanged";
    } else {
        Text compressed;
        if (!gzip_body(response.body, compressed))
            return false;
        char *sql = read_sql(config, "save_document.sql");
        good = sql && prepare(database, sql, &statement);
        free(sql);
        if (good) {
            bind_text(statement, 1, source.url);
            bind_text(statement, 2, source.record_id);
            bind_text(statement, 3, source.record_url);
            bind_text(statement, 4, source.journal);
            bind_text(statement, 5, source.format);
            bind_text(statement, 6, response.type ? response.type : "application/octet-stream");
            sqlite3_bind_blob(statement, 7, compressed.data, (int)compressed.size,
                              SQLITE_TRANSIENT);
            sqlite3_bind_int64(statement, 8, (sqlite3_int64)response.body.size);
            bind_text(statement, 9, hash);
            sqlite3_bind_int64(statement, 10, now);
            sqlite3_bind_int64(statement, 11, now);
            bind_text(statement, 12, response.etag);
            bind_text(statement, 13, response.modified);
            good = sqlite3_step(statement) == SQLITE_DONE;
        }
        result = previous.exists ? "updated" : "downloaded";
    }
    sqlite3_finalize(statement);
    if (!good)
        return false;
    statement = NULL;
    good = prepare(database, "UPDATE raw_queue SET state='done',last_error=NULL WHERE url=?",
                   &statement);
    if (good) {
        bind_text(statement, 1, source.url);
        good = sqlite3_step(statement) == SQLITE_DONE;
    }
    sqlite3_finalize(statement);
    return good;
}
bool queue_error(sqlite3 *database, const char *url, const char *error, long delay) {
    sqlite3_stmt *statement = NULL;
    bool good =
        prepare(database,
                "UPDATE raw_queue SET "
                "state='pending',attempts=attempts+1,next_attempt=?,last_error=? WHERE url=?",
                &statement);
    if (good) {
        sqlite3_bind_int64(statement, 1, time(NULL) + delay);
        bind_text(statement, 2, error);
        bind_text(statement, 3, url);
        good = sqlite3_step(statement) == SQLITE_DONE;
    }
    sqlite3_finalize(statement);
    return good;
}
volatile sig_atomic_t stop_requested = 0;
void request_stop(int) {
    stop_requested = 1;
}
int run_queue(sqlite3 *database, const Sources &sources, const Config &config) {
    long processed = 0, errors = 0;
    bool good = true;
    while (!stop_requested && (!config.max_pages || processed < config.max_pages)) {
        sqlite3_stmt *statement = NULL;
        if (!prepare(database,
                     "SELECT url FROM raw_queue WHERE state='pending' AND next_attempt<=? ORDER BY "
                     "planned_at,url LIMIT 1",
                     &statement))
            return 1;
        sqlite3_bind_int64(statement, 1, time(NULL));
        int status = sqlite3_step(statement);
        char *url = status == SQLITE_ROW
                        ? copy_text((const char *)sqlite3_column_text(statement, 0))
                        : NULL;
        sqlite3_finalize(statement);
        if (status == SQLITE_DONE)
            break;
        if (status != SQLITE_ROW)
            return 1;
        Source *source = sources.find(url);
        if (!source) {
            fprintf(stderr, "crawler: queued URL is absent from current manifest: %s\n", url);
            free(url);
            return 1;
        }
        statement = NULL;
        good = prepare(database, "UPDATE raw_queue SET state='inflight' WHERE url=?", &statement);
        if (good) {
            bind_text(statement, 1, url);
            good = sqlite3_step(statement) == SQLITE_DONE;
        }
        sqlite3_finalize(statement);
        if (!good) {
            free(url);
            return 1;
        }
        Previous previous;
        Response response((size_t)config.max_source_bytes);
        char error[CURL_ERROR_SIZE] = "request failed";
        const char *result = "";
        good = previous_document(database, url, previous);
        if (good && fetch(*source, previous, config, response, error) &&
            save_response(database, *source, previous, response, config, result)) {
            printf("%s\t%s\n", result, url);
        } else {
            ++errors;
            fprintf(stderr, "crawler: %s: %s\n", url, error);
            if (!queue_error(database, url, error, config.retry_delay)) {
                free(url);
                return 1;
            }
        }
        free(url);
        ++processed;
        if (config.delay > 0) {
            struct timespec pause = {(time_t)config.delay,
                                     (long)((config.delay - (time_t)config.delay) * 1000000000)};
            nanosleep(&pause, NULL);
        }
    }
    sqlite3_stmt *statement = NULL;
    if (!prepare(database, "SELECT COUNT(*) FROM raw_queue WHERE state!='done'", &statement))
        return 1;
    int status = sqlite3_step(statement);
    sqlite3_int64 pending = status == SQLITE_ROW ? sqlite3_column_int64(statement, 0) : -1;
    sqlite3_finalize(statement);
    printf("processed: %ld\nerrors: %ld\npending: %lld\n", processed, errors, (long long)pending);
    return errors || pending < 0 || (!config.max_pages && pending > 0) ? 1 : 0;
}
}

int ir_crawler_main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: crawl CONFIG.yaml\n");
        return 2;
    }
    Config config;
    if (!read_config(argv[1], config))
        return 2;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        return 1;
    Sources sources;
    if (!read_sources(config, sources)) {
        fprintf(stderr, "crawler: cannot read source manifest\n");
        curl_global_cleanup();
        return 1;
    }
    sqlite3 *database = NULL;
    int lock = -1;
    int result = 1;
    if (open_database(config, &database, lock) && plan_queue(database, sources, config)) {
        stop_requested = 0;
        void (*old_int)(int) = signal(SIGINT, request_stop);
        void (*old_term)(int) = signal(SIGTERM, request_stop);
        result = run_queue(database, sources, config);
        signal(SIGINT, old_int);
        signal(SIGTERM, old_term);
    }
    if (database)
        sqlite3_close(database);
    if (lock >= 0) {
        flock(lock, LOCK_UN);
        close(lock);
    }
    curl_global_cleanup();
    return result;
}
