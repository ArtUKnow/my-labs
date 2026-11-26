#ifndef IR_LAB04_STEMMER_RUSSIAN_STEM_H
#define IR_LAB04_STEMMER_RUSSIAN_STEM_H
bool vowel(uint32_t c) {
    return c == 0x430 || c == 0x435 || c == 0x438 || c == 0x43e || c == 0x443 || c == 0x44b ||
           c == 0x44d || c == 0x44e || c == 0x44f;
}
size_t region(const char *s, size_t start) {
    size_t n = strlen(s), w;
    bool previous = false;
    for (size_t i = start; i < n; i += w) {
        uint32_t c = read_utf8(s + i, n - i, &w);
        if (previous && !vowel(c))
            return i + w;
        previous = vowel(c);
    }
    return n;
}
bool remove_ending(char *s, const char *const *endings, size_t boundary, bool previous = false) {
    size_t n = strlen(s);
    for (size_t i = 0; endings[i]; i++) {
        size_t k = strlen(endings[i]);
        if (n < k || n - k < boundary || memcmp(s + n - k, endings[i], k))
            continue;
        if (previous &&
            (n - k < 2 || (memcmp(s + n - k - 2, "а", 2) && memcmp(s + n - k - 2, "я", 2))))
            continue;
        s[n - k] = 0;
        return true;
    }
    return false;
}
#include "stem_rules.h"
void stem_russian(char *s) {
    size_t n = strlen(s), w, rv = n;
    for (size_t i = 0; i < n; i += w) {
        uint32_t c = read_utf8(s + i, n - i, &w);
        if (vowel(c)) {
            rv = i + w;
            break;
        }
    }
    size_t r1 = region(s, 0), r2 = region(s, r1);
    if (rv >= n)
        return;
    bool done = remove_ending(s, deyeprichastie_2, rv);
    if (!done)
        done = remove_ending(s, deyeprichastie_1, rv, true);
    if (!done) {
        remove_ending(s, vozvratnoe_okonchanie, rv);
        done = remove_ending(s, prilagatelnoe, rv);
        if (done && !remove_ending(s, prichastie_2, rv))
            remove_ending(s, prichastie_1, rv, true);
        if (!done) {
            done = remove_ending(s, glagol_2, rv);
            if (!done)
                done = remove_ending(s, glagol_1, rv, true);
        }
        if (!done)
            remove_ending(s, sushchestvitelnoe, rv);
    }
    const char *const I[] = {"и", NULL};
    remove_ending(s, I, rv);
    remove_ending(s, slovoobrazovanie, r2);
    const char *const NN[] = {"н", NULL};
    if (has_ending(s, "нн"))
        remove_ending(s, NN, rv);
    else {
        done = remove_ending(s, prevoshodnaya_stepen, rv);
        if (done && has_ending(s, "нн"))
            remove_ending(s, NN, rv);
        else {
            const char *const SOFT[] = {"ь", NULL};
            remove_ending(s, SOFT, rv);
        }
    }
}
#endif
