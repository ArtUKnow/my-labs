#include "../common/text.h"
#include "../common/text_support.h"

namespace {
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
bool has_vowel(const char *s, size_t n) {
    for (size_t i = 0; i < n; i++)
        if (strchr("aeiouy", s[i]))
            return true;
    return false;
}
void stem_english(char *s) {
    size_t n = strlen(s);
    if (characters(s) <= 3)
        return;
    if (has_ending(s, "sses"))
        s[n - 2] = 0;
    else if (has_ending(s, "ies"))
        s[n - 2] = 0;
    else if (!has_ending(s, "ss") && has_ending(s, "s"))
        s[n - 1] = 0;
    n = strlen(s);
    size_t count = characters(s);
    if (count > 5 && has_ending(s, "ing") && has_vowel(s, n - 3))
        s[n - 3] = 0;
    else if (count > 4 && has_ending(s, "ed") && has_vowel(s, n - 2))
        s[n - 2] = 0;
    n = strlen(s);
    if (characters(s) > 4 && has_ending(s, "ly"))
        s[n - 2] = 0;
    n = strlen(s);
    if (characters(s) > 6 && has_ending(s, "ness"))
        s[n - 4] = 0;
    n = strlen(s);
    if (characters(s) > 7 && has_ending(s, "ment"))
        s[n - 4] = 0;
    n = strlen(s);
    if (characters(s) > 6 && has_ending(s, "tion"))
        s[n - 3] = 0;
}
}
void ir_stem(char *token) {
    to_lowercase(token);
    bool ru = false;
    size_t n = strlen(token), w;
    for (size_t i = 0; i < n; i += w) {
        uint32_t c = read_utf8(token + i, n - i, &w);
        if (c >= 0x430 && c <= 0x44f) {
            ru = true;
            break;
        }
    }
    if (ru)
        stem_russian(token);
    else
        stem_english(token);
}
void ir_lemma(char *token) {
    to_lowercase(token);
    static const char *exceptions[][2] = {
        {"was", "be"},       {"were", "be"},       {"is", "be"},         {"are", "be"},
        {"been", "be"},      {"has", "have"},      {"had", "have"},      {"went", "go"},
        {"gone", "go"},      {"men", "man"},       {"people", "person"}, {"children", "child"},
        {"feet", "foot"},    {"mice", "mouse"},    {"люди", "человек"},  {"людей", "человек"},
        {"дети", "ребенок"}, {"детей", "ребенок"}, {"лет", "год"},       {NULL, NULL}};
    for (size_t i = 0; exceptions[i][0]; i++)
        if (!strcmp(token, exceptions[i][0])) {
            if (strlen(exceptions[i][1]) <= strlen(token))
                strcpy(token, exceptions[i][1]);
            return;
        }
    size_t n = strlen(token);
    if (n > 4 && has_ending(token, "ies")) {
        token[n - 3] = 'y';
        token[n - 2] = 0;
        return;
    }
    if (n > 5 && has_ending(token, "ing")) {
        token[n - 3] = 0;
        n -= 3;
        if (n > 2 && token[n - 1] == token[n - 2] && strchr("bdgmnprt", token[n - 1]))
            token[--n] = 0;
        return;
    }
    if (n > 4 && has_ending(token, "ed")) {
        token[n - 2] = 0;
        n -= 2;
        if (n > 2 && token[n - 1] == token[n - 2] && strchr("bdgmnprt", token[n - 1]))
            token[--n] = 0;
        return;
    }
    if (n > 3 && has_ending(token, "s") && !has_ending(token, "ss") && !has_ending(token, "us") &&
        !has_ending(token, "is") && !has_ending(token, "ics")) {
        if (has_ending(token, "ches") || has_ending(token, "shes") || has_ending(token, "xes"))
            token[n - 2] = 0;
        else
            token[n - 1] = 0;
        return;
    }
    bool ru = false;
    for (size_t i = 0; i < n; i++)
        if ((unsigned char)token[i] >= 0xd0) {
            ru = true;
            break;
        }
    if (ru)
        ir_stem(token);
}
bool ir_lemma_copy(const char *input, char *output, size_t capacity) {
    Buffer normalized;
    normalized.append(input, strlen(input));
    to_lowercase(normalized.data);
    static const char *expanding[][2] = {
        {"has", "have"},     {"had", "have"},      {"люди", "человек"}, {"людей", "человек"},
        {"дети", "ребенок"}, {"детей", "ребенок"}, {"лет", "год"},      {NULL, NULL}};
    const char *result = NULL;
    for (size_t i = 0; expanding[i][0]; i++)
        if (!strcmp(normalized.data, expanding[i][0])) {
            result = expanding[i][1];
            break;
        }
    if (!result) {
        ir_lemma(normalized.data);
        result = normalized.data;
    }
    size_t n = strlen(result);
    if (n >= capacity)
        return false;
    memcpy(output, result, n + 1);
    return true;
}
int ir_stem_main(int argc, char **argv) {
    if (argc < 1)
        return 2;
    if (!strcmp(argv[0], "stem") || !strcmp(argv[0], "lemma")) {
        char *line = NULL;
        size_t cap = 0;
        ssize_t n;
        while ((n = getline(&line, &cap, stdin)) >= 0) {
            while (n && strchr("\r\n \t", line[n - 1]))
                line[--n] = 0;
            char *start = line;
            while (*start == ' ' || *start == '\t')
                start++;
            if (!*start)
                continue;
            if (!strcmp(argv[0], "stem")) {
                ir_stem(start);
                puts(start);
            } else {
                Buffer result;
                result.reserve(strlen(start) + 64);
                if (!ir_lemma_copy(start, result.data, result.capacity)) {
                    fprintf(stderr, "lemma output buffer too small\n");
                    free(line);
                    return 1;
                }
                puts(result.data);
            }
        }
        free(line);
        return ferror(stdin) || ferror(stdout) ? 1 : 0;
    }
    fprintf(stderr, "usage: stem | lemma\n");
    return 2;
}
