#include "../common/text.h"
#include "../common/text_support.h"

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
