#ifndef IR_LAB04_STEMMER_ENGLISH_STEM_H
#define IR_LAB04_STEMMER_ENGLISH_STEM_H
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
#endif
