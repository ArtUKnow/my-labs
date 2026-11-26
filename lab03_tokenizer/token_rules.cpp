#include "../common/text.h"
#include "../common/text_support.h"

void ir_tokenize(const char *text, size_t length, IrTokenCallback callback, void *context) {
    Buffer token;
    size_t width;
    uint32_t previous = 0;
    for (size_t i = 0; i < length; i += width) {
        uint32_t c = read_utf8(text + i, length - i, &width);
        bool keep = is_word(c);
        if (!keep && (c == '-' || c == '\'' || c == 0x2019 || c == '.' || c == ',') && token.size &&
            i + width < length) {
            size_t next_width;
            uint32_t next = read_utf8(text + i + width, length - i - width, &next_width);
            keep =
                is_word(next) && ((c != '.' && c != ',') || (is_digit(previous) && is_digit(next)));
        }
        if (keep) {
            uint32_t lc = lower_codepoint(c);
            char bytes[4];
            size_t size = write_utf8(lc, bytes);
            token.append(bytes, size);
            if (c == 0x130) {
                const char dot[] = {char(0xcc), char(0x87)};
                token.append(dot, 2);
            }
            previous = c;
        } else if (token.size) {
            callback(token.data, token.size, context);
            token.clear();
            previous = 0;
        }
    }
    if (token.size)
        callback(token.data, token.size, context);
}
