#ifndef IR_CRAWLER_CONFIG_H
#define IR_CRAWLER_CONFIG_H
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

#endif
