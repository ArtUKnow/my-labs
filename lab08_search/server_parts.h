#ifndef IR_SERVER_PARTS_H
#define IR_SERVER_PARTS_H
#include <stddef.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace web {
struct Text {
    char *data;
    size_t size, capacity;
    Text() : data(NULL), size(0), capacity(0) {
        add("");
    }
    ~Text() {
        free(data);
    }
    void reserve(size_t count) {
        if (count <= capacity)
            return;
        size_t next = capacity ? capacity : 128;
        while (next < count)
            next *= 2;
        char *memory = (char *)realloc(data, next);
        if (!memory) {
            perror("server memory");
            exit(1);
        }
        data = memory;
        capacity = next;
    }
    void add(const char *text) {
        add(text, strlen(text));
    }
    void add(const char *text, size_t count) {
        reserve(size + count + 1);
        memcpy(data + size, text, count);
        size += count;
        data[size] = 0;
    }
    void letter(char value) {
        add(&value, 1);
    }
    void format(const char *format, ...) {
        va_list args, copy;
        va_start(args, format);
        va_copy(copy, args);
        int count = vsnprintf(NULL, 0, format, copy);
        va_end(copy);
        if (count < 0) {
            va_end(args);
            return;
        }
        reserve(size + (size_t)count + 1);
        vsnprintf(data + size, (size_t)count + 1, format, args);
        va_end(args);
        size += count;
    }
};
struct Document {
    Text title, url, journal;
};
struct Answer {
    uint64_t count;
    Text error;
    Document documents[50];
    size_t size;
    Answer() : count(0), size(0) {
    }
};

int hex_digit(char value);
bool decode_url(const char *input, size_t length, Text &output);
void encode_url(Text &output, const char *input);
bool read_parameters(const char *query_string, Text &query, uint64_t &page);
void escape_html(Text &output, const char *input);
bool read_answer(const char *text, Answer &answer);
void render(Text &output, const char *query, uint64_t page, const Answer *answer, const char *error);
}
#endif
