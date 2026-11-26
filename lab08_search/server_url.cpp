#include "server_parts.h"
#include <errno.h>

namespace web {
int hex_digit(char value) {
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}
bool decode_url(const char *input, size_t length, Text &output) {
    for (size_t i = 0; i < length; ++i) {
        unsigned char value = (unsigned char)input[i];
        if (value == '+')
            value = ' ';
        else if (value == '%') {
            if (i + 2 >= length)
                return false;
            int first = hex_digit(input[i + 1]), second = hex_digit(input[i + 2]);
            if (first < 0 || second < 0)
                return false;
            value = (unsigned char)(first * 16 + second);
            i += 2;
        }
        if (!value || value == '\r' || value == '\n')
            return false;
        output.letter((char)value);
    }
    return true;
}
void encode_url(Text &output, const char *input) {
    const char *digits = "0123456789ABCDEF";
    for (; *input; ++input) {
        unsigned char value = (unsigned char)*input;
        if ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') || strchr("-_.~", value))
            output.letter((char)value);
        else {
            output.letter('%');
            output.letter(digits[value >> 4]);
            output.letter(digits[value & 15]);
        }
    }
}
bool read_parameters(const char *query_string, Text &query, uint64_t &page) {
    page = 1;
    const char *cursor = query_string;
    bool query_seen = false, page_seen = false;
    while (*cursor) {
        const char *end = strchr(cursor, '&');
        if (!end)
            end = cursor + strlen(cursor);
        const char *equal = (const char *)memchr(cursor, '=', (size_t)(end - cursor));
        const char *value = equal ? equal + 1 : end;
        Text key, text;
        if (!decode_url(cursor, (size_t)((equal ? equal : end) - cursor), key) ||
            !decode_url(value, (size_t)(end - value), text))
            return false;
        if (!strcmp(key.data, "q") && !query_seen) {
            query.add(text.data);
            query_seen = true;
        } else if (!strcmp(key.data, "page") && !page_seen) {
            if (!text.size || text.data[0] == '-')
                return false;
            char *number_end = NULL;
            errno = 0;
            unsigned long long number = strtoull(text.data, &number_end, 10);
            if (errno || number_end == text.data || *number_end || !number ||
                number > UINT64_MAX / 50)
                return false;
            page = number;
            page_seen = true;
        }
        cursor = *end ? end + 1 : end;
    }
    size_t characters = 0;
    for (size_t i = 0; i < query.size; ++i)
        if (((unsigned char)query.data[i] & 0xc0) != 0x80)
            ++characters;
    return characters <= 500;
}
}
