#ifndef IR_TEXT_H
#define IR_TEXT_H
#include <stddef.h>
typedef void (*IrTokenCallback)(const char *, size_t, void *);
void ir_tokenize(const char *text, size_t length, IrTokenCallback callback, void *context);
void ir_stem(char *token);

void ir_lemma(char *token);

bool ir_lemma_copy(const char *input, char *output, size_t capacity);
int ir_tokenizer_main(int argc, char **argv);
int ir_stem_main(int argc, char **argv);
int ir_zipf_main(int argc, char **argv);
int ir_text_main(int argc, char **argv);
int ir_text_tests();
#endif
