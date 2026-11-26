#ifndef IR_TEXT_SUPPORT_H
#define IR_TEXT_SUPPORT_H
#include <errno.h>
#include <math.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <zlib.h>
namespace {
inline void *checked(void *p) {
    if (!p) {
        fprintf(stderr, "memory allocation failed\n");
        exit(2);
    }
    return p;
}
struct Buffer {
    char *data;
    size_t size, capacity;
    Buffer() : data(NULL), size(0), capacity(0) {
    }
    ~Buffer() {
        free(data);
    }
    void reserve(size_t n) {
        if (n <= capacity)
            return;
        size_t c = capacity ? capacity : 256;
        while (c < n)
            c *= 2;
        data = (char *)checked(realloc(data, c));
        capacity = c;
    }
    void append(const char *p, size_t n) {
        reserve(size + n + 1);
        memcpy(data + size, p, n);
        size += n;
        data[size] = 0;
    }
    void clear() {
        size = 0;
        if (data)
            data[0] = 0;
    }
};
inline uint32_t read_utf8(const char *s, size_t n, size_t *width) {
    if (!n) {
        *width = 0;
        return 0;
    }
    unsigned char a = (unsigned char)s[0];
    if (a < 128) {
        *width = 1;
        return a;
    }
    unsigned count = a >= 0xf0 && a <= 0xf4   ? 4
                     : a >= 0xe0 && a <= 0xef ? 3
                     : a >= 0xc2 && a <= 0xdf ? 2
                                              : 1;
    if (count == 1 || count > n) {
        *width = 1;
        return 0xfffd;
    }
    uint32_t c = a & ((1u << (7 - count)) - 1);
    for (unsigned i = 1; i < count; i++) {
        unsigned char b = (unsigned char)s[i];
        if ((b & 0xc0) != 0x80) {
            *width = 1;
            return 0xfffd;
        }
        c = (c << 6) | (b & 63);
    }
    if ((count == 2 && c < 128) || (count == 3 && c < 2048) || (count == 4 && c < 65536) ||
        c > 0x10ffff || (c >= 0xd800 && c <= 0xdfff)) {
        *width = 1;
        return 0xfffd;
    }
    *width = count;
    return c;
}
inline size_t write_utf8(uint32_t c, char *out) {
    if (c < 128) {
        out[0] = (char)c;
        return 1;
    }
    if (c < 2048) {
        out[0] = (char)(0xc0 | (c >> 6));
        out[1] = (char)(0x80 | (c & 63));
        return 2;
    }
    if (c < 65536) {
        out[0] = (char)(0xe0 | (c >> 12));
        out[1] = (char)(0x80 | ((c >> 6) & 63));
        out[2] = (char)(0x80 | (c & 63));
        return 3;
    }
    out[0] = (char)(0xf0 | (c >> 18));
    out[1] = (char)(0x80 | ((c >> 12) & 63));
    out[2] = (char)(0x80 | ((c >> 6) & 63));
    out[3] = (char)(0x80 | (c & 63));
    return 4;
}
#include "unicode_data.h"
inline bool in_ranges(uint32_t c, const UnicodeRange *ranges, size_t n) {
    size_t lo = 0, hi = n;
    while (lo < hi) {
        size_t m = lo + (hi - lo) / 2;
        if (c < ranges[m].first)
            hi = m;
        else if (c > ranges[m].last)
            lo = m + 1;
        else
            return true;
    }
    return false;
}
inline uint32_t lower_codepoint(uint32_t c) {
    if (c < 128)
        return c >= 'A' && c <= 'Z' ? c + 32 : c;
    if (c == 0x130)
        return 'i';
    size_t lo = 0, hi = sizeof(lower_ranges) / sizeof(*lower_ranges);
    while (lo < hi) {
        size_t m = lo + (hi - lo) / 2;
        if (c < lower_ranges[m].first)
            hi = m;
        else if (c > lower_ranges[m].last)
            lo = m + 1;
        else
            return (uint32_t)((int64_t)c + lower_ranges[m].delta);
    }
    return c;
}
inline bool is_digit(uint32_t c) {
    if (c < 128)
        return c >= '0' && c <= '9';
    return in_ranges(c, digit_ranges, sizeof(digit_ranges) / sizeof(*digit_ranges));
}
inline bool is_letter(uint32_t c) {
    if (c < 128)
        return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
    return in_ranges(c, alpha_ranges, sizeof(alpha_ranges) / sizeof(*alpha_ranges));
}
inline bool is_word(uint32_t c) {
    return is_letter(c) || is_digit(c);
}
inline size_t characters(const char *s) {
    size_t count = 0, n = strlen(s), w;
    for (size_t i = 0; i < n; i += w) {
        read_utf8(s + i, n - i, &w);
        count++;
    }
    return count;
}
inline void to_lowercase(char *s) {
    size_t n = strlen(s), out = 0, w;
    for (size_t i = 0; i < n; i += w) {
        uint32_t c = read_utf8(s + i, n - i, &w);
        c = lower_codepoint(c);
        if (c == 0x451)
            c = 0x435;
        char b[4];
        size_t k = write_utf8(c, b);
        if (k > w) {
            memcpy(s + out, s + i, w);
            out += w;
        } else {
            memcpy(s + out, b, k);
            out += k;
        }
    }
    s[out] = 0;
}
inline bool has_ending(const char *s, const char *end) {
    size_t a = strlen(s), b = strlen(end);
    return a >= b && memcmp(s + a - b, end, b) == 0;
}
inline double now() {
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return t.tv_sec + t.tv_nsec / 1e9;
}
inline char *clone(const char *s) {
    size_t n = strlen(s) + 1;
    char *p = (char *)checked(malloc(n));
    memcpy(p, s, n);
    return p;
}
inline void json_string(FILE *f, const char *s) {
    fputc('"', f);
    for (const unsigned char *p = (const unsigned char *)s; *p; p++) {
        if (*p == '"' || *p == '\\') {
            fputc('\\', f);
            fputc(*p, f);
        } else if (*p == '\n')
            fputs("\\n", f);
        else if (*p == '\r')
            fputs("\\r", f);
        else if (*p == '\t')
            fputs("\\t", f);
        else if (*p < 32)
            fprintf(f, "\\u%04x", *p);
        else
            fputc(*p, f);
    }
    fputc('"', f);
}
inline uint64_t hash(const char *s) {
    uint64_t h = 1469598103934665603ULL;
    for (; *s; s++) {
        h ^= (unsigned char)*s;
        h *= 1099511628211ULL;
    }
    return h;
}
inline bool read_document(const char *path, Buffer &text) {
    text.clear();
    gzFile f = gzopen(path, "rb");
    if (!f) {
        fprintf(stderr, "cannot open document %s\n", path);
        return false;
    }
    char buffer[65536];
    int n;
    while ((n = gzread(f, buffer, sizeof(buffer))) > 0)
        text.append(buffer, (size_t)n);
    if (n < 0) {
        int error;
        fprintf(stderr, "cannot read document %s: %s\n", path, gzerror(f, &error));
        gzclose(f);
        return false;
    }
    if (gzclose(f) != Z_OK) {
        fprintf(stderr, "cannot close document %s\n", path);
        return false;
    }
    return true;
}
}
#endif
