#include "text.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

namespace {
struct Tokens {
    char *items[64];
    size_t count;
    Tokens() : count(0) {
        memset(items, 0, sizeof(items));
    }
    ~Tokens() {
        for (size_t i = 0; i < count; i++)
            free(items[i]);
    }
};
void save_token(const char *text, size_t size, void *state) {
    Tokens *tokens = (Tokens *)state;
    if (tokens->count == 64)
        abort();
    char *copy = (char *)malloc(size + 1);
    if (!copy)
        abort();
    memcpy(copy, text, size);
    copy[size] = 0;
    tokens->items[tokens->count++] = copy;
}
struct TokenTests {
    int total = 0;
    int failed = 0;
    void check(const char *name, const char *input, size_t length, const char *expected) {
        total++;
        Tokens tokens;
        ir_tokenize(input, length, save_token, &tokens);
        const char *word = expected;
        size_t index = 0;
        bool good = true;
        while (*word) {
            const char *end = strchr(word, ' ');
            size_t size = end ? (size_t)(end - word) : strlen(word);
            if (index >= tokens.count || strlen(tokens.items[index]) != size ||
                memcmp(tokens.items[index], word, size)) {
                good = false;
                break;
            }
            index++;
            word += size;
            if (*word)
                word++;
        }
        if (index != tokens.count)
            good = false;
        printf("  [%s] %s\n", good ? "OK" : "ОШИБКА", name);
        if (!good) {
            failed++;
            fprintf(stderr, "Ожидалось: %s\nПолучено:", expected);
            for (size_t i = 0; i < tokens.count; i++)
                fprintf(stderr, " <%s>", tokens.items[i]);
            fprintf(stderr, "\n");
        }
    }
    void check(const char *name, const char *input, const char *expected) {
        check(name, input, strlen(input), expected);
    }
};
}

int ir_tokenizer_tests() {
    puts("\nЮнит-тесты: токенизация");
    TokenTests tests;
    tests.check("Пустой текст", "", "");
    tests.check("Только пробелы и переводы строк", " \t\r\n ", "");
    tests.check("Только пунктуация", ".,!?:;()[]{}---''", "");
    tests.check("Пунктуация и нижний регистр", "Hello, WORLD!", "hello world");
    tests.check("Русские буквы и ё", "РАКЕТА ЁЖ ёлка", "ракета ёж ёлка");
    tests.check("Смешанные алфавиты и десятичные числа",
                "Dark matter! СОЮЗ-2 O’Neill 3.14 1,25 x.y ΑΒ ЁЖ",
                "dark matter союз-2 o’neill 3.14 1,25 x y αβ ёж");
    tests.check("Внутренний дефис", "foo-bar Союз-2 -start end-", "foo-bar союз-2 start end");
    tests.check("Два дефиса разделяют слова", "one--two", "one two");
    tests.check("Кавычки и внутренний апостроф", "'quoted' a'b O’Neill", "quoted a'b o’neill");
    tests.check("Числа, десятичные разделители", "42 3.14 1,25 .5 6.", "42 3.14 1,25 5 6");
    tests.check("Точка и запятая между буквами", "a.b c,d", "a b c d");
    tests.check("Пробел, табуляция и новая строка", "one\ttwo\nthree\r\nfour",
                "one two three four");
    tests.check("Греческий регистр", "ΑΒ Ω", "αβ ω");
    tests.check("Китайские символы и арабские цифры", "中文 ١٢٣٫٤ e\xcc\x81", "中文 ١٢٣ ٤ e");
    tests.check("Степени и обозначения", "42 m² x2", "42 m² x2");
    tests.check("Unicode-преобразование с расширением", "İ", "i\xcc\x87");
    const char invalid[] = {'a', (char)0xff, 'b'};
    tests.check("Повреждённый UTF-8 разделяет слова", invalid, sizeof(invalid), "a b");
    const char incomplete[] = {'a', (char)0xd0};
    tests.check("Незавершённый UTF-8 в конце", incomplete, sizeof(incomplete), "a");
    const char bounded[] = {'A', 'B', ' ', 'C', 'D'};
    tests.check("Соблюдается длина буфера", bounded, 2, "ab");
    const char zero[] = {'A', 0, 'B'};
    tests.check("Нулевой байт внутри буфера", zero, sizeof(zero), "a b");
    const char unchanged[] = "MiXeD ЁЖ";
    tests.check("Входной текст сохраняется", unchanged, "mixed ёж");
    if (strcmp(unchanged, "MiXeD ЁЖ"))
        tests.failed++;
    char long_word[4097];
    memset(long_word, 'a', 4096);
    long_word[4096] = 0;
    tests.check("Длинный токен не обрезается", long_word, long_word);
    printf("Итого: %d; успешно: %d; ошибок: %d\n", tests.total, tests.total - tests.failed,
           tests.failed);
    return tests.failed ? 1 : 0;
}

int ir_text_tests() {
    int tokenizer = ir_tokenizer_tests();
    int stemmer = ir_stemmer_tests();
    return tokenizer || stemmer ? 1 : 0;
}
