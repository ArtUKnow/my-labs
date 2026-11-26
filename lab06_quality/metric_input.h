#ifndef IR_LAB06_QUALITY_METRIC_INPUT_H
#define IR_LAB06_QUALITY_METRIC_INPUT_H
static bool fields(char *line, char **a, char **b, char **c) {
    *a = line;
    *b = strchr(line, '\t');
    if (!*b)
        return false;
    *(*b)++ = 0;
    *c = strchr(*b, '\t');
    if (!*c)
        return false;
    *(*c)++ = 0;
    (*c)[strcspn(*c, "\r\n")] = 0;
    return **a && **b && **c && !strchr(*c, '\t');
}
static void print_score(Scores s) {
    printf("{\"p\":%.8f,\"r\":%.8f,\"ap\":%.8f,\"dcg\":%.8f,\"ndcg\":%.8f,"
           "\"err\":%.8f}",
           s.precision, s.recall, s.ap, s.dcg, s.ndcg, s.err);
}
static void quoted(const char *s) {
    putchar('"');
    for (; *s; ++s) {
        unsigned char c = (unsigned char)*s;
        if (c == '"' || c == '\\') {
            putchar('\\');
            putchar(c);
        } else if (c < 32)
            printf("\\u%04x", c);
        else
            putchar(c);
    }
    putchar('"');
}
static int read_judgments(FILE *file, Buffer<Judgment> &rows, Buffer<char *> &queries) {
    char *line = NULL;
    size_t capacity = 0;
    int error = 0;
    while (getline(&line, &capacity, file) >= 0) {
        if (!line[0] || line[0] == '\n')
            continue;
        char *a, *b, *c, *end;
        if (!fields(line, &a, &b, &c)) {
            error = 1;
            break;
        }
        long grade = strtol(c, &end, 10);
        if (*end || grade < 0 || grade > 3) {
            error = 1;
            break;
        }
        bool duplicate = false;
        for (size_t i = 0; i < rows.size; ++i)
            if (!strcmp(rows.data[i].query, a) && !strcmp(rows.data[i].document, b))
                duplicate = true;
        if (duplicate) {
            error = 1;
            break;
        }
        rows.add({copy_text(a), copy_text(b), (int)grade});
        bool exists = false;
        for (size_t i = 0; i < queries.size; ++i)
            if (!strcmp(queries.data[i], a))
                exists = true;
        if (!exists)
            queries.add(copy_text(a));
    }
    free(line);
    return error;
}

static int read_results(FILE *file, Buffer<RunItem> &run, Buffer<char *> &queries) {
    char *line = NULL;
    size_t capacity = 0;
    int error = 0;
    while (getline(&line, &capacity, file) >= 0) {
        if (!line[0] || line[0] == '\n')
            continue;
        char *a, *b, *c, *end;
        if (!fields(line, &a, &b, &c)) {
            error = 1;
            break;
        }
        errno = 0;
        long rank = strtol(c, &end, 10);
        if (errno || end == c || *end || rank <= 0 || rank > INT_MAX) {
            error = 1;
            break;
        }
        bool known = false;
        for (size_t i = 0; i < queries.size; ++i)
            if (!strcmp(queries.data[i], a))
                known = true;
        if (!known) {
            error = 1;
            break;
        }
        for (size_t i = 0; i < run.size; ++i)
            if (!strcmp(run.data[i].query, a) &&
                (run.data[i].rank == rank || !strcmp(run.data[i].document, b)))
                error = 1;
        if (error)
            break;
        run.add({copy_text(a), copy_text(b), (int)rank});
    }
    free(line);
    return error;
}


#endif
