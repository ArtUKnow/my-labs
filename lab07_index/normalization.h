#ifndef IR_LAB07_INDEX_NORMALIZATION_H
#define IR_LAB07_INDEX_NORMALIZATION_H
inline char *normalize_word(const char *token, size_t length, int mode) {
    if (mode != 2) {
        char *word = duplicate(token, length);
        if (mode == 1)
            ir_stem(word);
        return word;
    }
    if (length > SIZE_MAX - 128)
        stop_with_error("token length overflow");
    char *input = duplicate(token, length);
    char *word = (char *)allocate(length + 128);
    if (!ir_lemma_copy(input, word, length + 128)) {
        free(input);
        free(word);
        stop_with_error("lemma output exceeds allocated capacity");
    }
    free(input);
    return word;
}

#endif
