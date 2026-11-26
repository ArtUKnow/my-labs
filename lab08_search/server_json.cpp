#include "server_parts.h"
#include <ctype.h>
#include <errno.h>
#include <math.h>

namespace web {
void utf8(Text &output, uint32_t code) {
    if (code < 0x80)
        output.letter((char)code);
    else if (code < 0x800) {
        output.letter((char)(0xc0 | (code >> 6)));
        output.letter((char)(0x80 | (code & 63)));
    } else if (code < 0x10000) {
        output.letter((char)(0xe0 | (code >> 12)));
        output.letter((char)(0x80 | ((code >> 6) & 63)));
        output.letter((char)(0x80 | (code & 63)));
    } else {
        output.letter((char)(0xf0 | (code >> 18)));
        output.letter((char)(0x80 | ((code >> 12) & 63)));
        output.letter((char)(0x80 | ((code >> 6) & 63)));
        output.letter((char)(0x80 | (code & 63)));
    }
}
struct Json {
    const char *cursor;
    bool valid;
    Json(const char *input) : cursor(input), valid(true) {
    }
    void space() {
        while (*cursor && isspace((unsigned char)*cursor))
            ++cursor;
    }
    bool take(char value) {
        space();
        if (*cursor != value) {
            valid = false;
            return false;
        }
        ++cursor;
        return true;
    }
    bool next(char value) {
        space();
        if (*cursor != value)
            return false;
        ++cursor;
        return true;
    }
    uint32_t codepoint() {
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            int digit = hex_digit(*cursor);
            if (digit < 0) {
                valid = false;
                return 0;
            }
            value = value * 16 + digit;
            ++cursor;
        }
        return value;
    }
    bool string(Text &output) {
        if (!take('"'))
            return false;
        while (valid && *cursor && *cursor != '"') {
            unsigned char value = (unsigned char)*cursor++;
            if (value < 32) {
                valid = false;
                break;
            }
            if (value != '\\') {
                output.letter((char)value);
                continue;
            }
            char escaped = *cursor++;
            if (!escaped) {
                valid = false;
                break;
            }
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                output.letter(escaped);
                break;
            case 'b':
                output.letter('\b');
                break;
            case 'f':
                output.letter('\f');
                break;
            case 'n':
                output.letter('\n');
                break;
            case 'r':
                output.letter('\r');
                break;
            case 't':
                output.letter('\t');
                break;
            case 'u': {
                uint32_t code = codepoint();
                if (code >= 0xd800 && code <= 0xdbff) {
                    if (cursor[0] != '\\' || cursor[1] != 'u') {
                        valid = false;
                        break;
                    }
                    cursor += 2;
                    uint32_t low = codepoint();
                    if (low < 0xdc00 || low > 0xdfff) {
                        valid = false;
                        break;
                    }
                    code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
                } else if (code >= 0xdc00 && code <= 0xdfff) {
                    valid = false;
                    break;
                }
                if (!code) {
                    valid = false;
                    break;
                }
                if (valid)
                    utf8(output, code);
                break;
            }
            default:
                valid = false;
            }
        }
        return valid && take('"');
    }
    bool number(uint64_t &value) {
        space();
        if (!isdigit((unsigned char)*cursor)) {
            valid = false;
            return false;
        }
        errno = 0;
        char *end = NULL;
        unsigned long long parsed = strtoull(cursor, &end, 10);
        if (errno || end == cursor) {
            valid = false;
            return false;
        }
        cursor = end;
        value = parsed;
        return true;
    }
    bool skip(int depth = 0) {
        if (depth > 32) {
            valid = false;
            return false;
        }
        space();
        if (*cursor == '"') {
            Text value;
            return string(value);
        }
        if (next('{')) {
            if (next('}'))
                return true;
            do {
                Text key;
                if (!string(key) || !take(':') || !skip(depth + 1))
                    return false;
            } while (next(','));
            return take('}');
        }
        if (next('[')) {
            if (next(']'))
                return true;
            do {
                if (!skip(depth + 1))
                    return false;
            } while (next(','));
            return take(']');
        }
        if (!strncmp(cursor, "true", 4)) {
            cursor += 4;
            return true;
        }
        if (!strncmp(cursor, "false", 5)) {
            cursor += 5;
            return true;
        }
        if (!strncmp(cursor, "null", 4)) {
            cursor += 4;
            return true;
        }
        char *end = NULL;
        errno = 0;
        double value = strtod(cursor, &end);
        if (end == cursor || errno || !isfinite(value)) {
            valid = false;
            return false;
        }
        cursor = end;
        return true;
    }
};
bool read_document(Json &json, Document &document) {
    if (!json.take('{'))
        return false;
    if (json.next('}'))
        return true;
    do {
        Text key;
        if (!json.string(key) || !json.take(':'))
            return false;
        if (!strcmp(key.data, "title")) {
            if (!json.string(document.title))
                return false;
        } else if (!strcmp(key.data, "url")) {
            if (!json.string(document.url))
                return false;
        } else if (!strcmp(key.data, "journal")) {
            if (!json.string(document.journal))
                return false;
        } else if (!json.skip())
            return false;
    } while (json.next(','));
    return json.take('}');
}
bool read_answer(const char *text, Answer &answer) {
    Json json(text);
    if (!json.take('{'))
        return false;
    bool has_count = false;
    if (json.next('}'))
        return false;
    do {
        Text key;
        if (!json.string(key) || !json.take(':'))
            return false;
        if (!strcmp(key.data, "count")) {
            has_count = json.number(answer.count);
            if (!has_count)
                return false;
        } else if (!strcmp(key.data, "error")) {
            if (!json.string(answer.error))
                return false;
        } else if (!strcmp(key.data, "documents")) {
            if (!json.take('['))
                return false;
            if (!json.next(']')) {
                do {
                    if (answer.size == 50 || !read_document(json, answer.documents[answer.size]))
                        return false;
                    ++answer.size;
                } while (json.next(','));
                if (!json.take(']'))
                    return false;
            }
        } else if (!json.skip())
            return false;
    } while (json.next(','));
    if (!json.take('}'))
        return false;
    json.space();
    return json.valid && !*json.cursor && (has_count || answer.error.size);
}
}
