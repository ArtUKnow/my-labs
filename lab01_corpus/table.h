#ifndef IR_CORPUS_TABLE_H
#define IR_CORPUS_TABLE_H
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

namespace corpus_table {
inline char *copy_field(const char *start, size_t length) {
    char *result = (char *)malloc(length + 1);
    if (!result) {
        perror("memory");
        exit(1);
    }
    memcpy(result, start, length);
    result[length] = 0;
    return result;
}
struct Row {
    char *fields[32];
    int count;
    Row() : count(0) {
    }
    ~Row() {
        clear();
    }
    void clear() {
        for (int i = 0; i < count; i++)
            free(fields[i]);
        count = 0;
    }
    bool read(FILE *file) {
        clear();
        char *line = NULL;
        size_t capacity = 0;
        ssize_t length = getline(&line, &capacity, file);
        if (length < 0) {
            free(line);
            return false;
        }
        while (length > 0 && (line[length - 1] == '\n' || line[length - 1] == '\r'))
            line[--length] = 0;
        char *current = line;
        while (count < 32) {
            char *end = strchr(current, '\t');
            size_t size = end ? (size_t)(end - current) : strlen(current);
            fields[count++] = copy_field(current, size);
            if (!end)
                break;
            current = end + 1;
        }
        free(line);
        return true;
    }
    int column(const char *name) {
        for (int i = 0; i < count; i++)
            if (!strcmp(fields[i], name))
                return i;
        return -1;
    }
};
inline void clean_field(char *field) {
    size_t length = strlen(field);
    if (length >= 2 && field[0] == '"' && field[length - 1] == '"') {
        size_t output = 0;
        for (size_t i = 1; i < length - 1; i++) {
            if (field[i] == '"' && i + 1 < length - 1 && field[i + 1] == '"')
                i++;
            field[output++] = field[i];
        }
        field[output] = 0;
    }
}
}
#endif
