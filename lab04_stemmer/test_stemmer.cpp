#include "text.h"
#include <stdio.h>
#include <string.h>

namespace {
struct WordCase {
    const char *name;
    const char *input;
    const char *expected;
};
struct WordTests {
    int total = 0;
    int failed = 0;
    void result(const char *name, bool good, const char *expected, const char *actual) {
        total++;
        printf("  [%s] %s\n", good ? "OK" : "ОШИБКА", name);
        if (!good) {
            failed++;
            fprintf(stderr, "Ожидалось: <%s>; получено: <%s>\n", expected, actual);
        }
    }
    void stem(const WordCase &test) {
        struct ProtectedWord {
            unsigned char before[8];
            char word[256];
            unsigned char after[8];
        } value;
        memset(&value, 0xa5, sizeof(value));
        strcpy(value.word, test.input);
        ir_stem(value.word);
        bool good = !strcmp(value.word, test.expected);
        for (size_t i = 0; i < 8; i++)
            good = good && value.before[i] == 0xa5 && value.after[i] == 0xa5;
        result(test.name, good, test.expected, value.word);
    }
    void lemma(const WordCase &test) {
        char word[256];
        strcpy(word, test.input);
        ir_lemma(word);
        result(test.name, !strcmp(word, test.expected), test.expected, word);
    }
};
}

int ir_stemmer_tests() {
    puts("\nЮнит-тесты: стемминг");
    WordTests tests;
    const WordCase stems[] = {{"Пустое слово", "", ""},
                              {"Одна буква", "а", "а"},
                              {"Нет русских гласных", "ббб", "ббб"},
                              {"Русское множественное число", "ракеты", "ракет"},
                              {"Русское слово в верхнем регистре", "РАКЕТЫ", "ракет"},
                              {"Русский родительный падеж", "спутников", "спутник"},
                              {"Русское творительное окончание", "машинами", "машин"},
                              {"Русское прилагательное", "красивый", "красив"},
                              {"Нормализация ё", "ЁЛКИ", "елк"},
                              {"Регистр и ё вместе", "ПОЛЁТЫ", "полет"},
                              {"Короткое английское слово", "sky", "sky"},
                              {"Английское множественное число", "particles", "particle"},
                              {"Английское слово в верхнем регистре", "PARTICLES", "particle"},
                              {"Окончание ss не удаляется", "glass", "glass"},
                              {"Окончание sses", "classes", "class"},
                              {"Окончание ies", "studies", "studi"},
                              {"Окончание ing без сведения к лемме", "running", "runn"},
                              {"Окончание ing", "playing", "play"},
                              {"Окончание ed", "enjoyed", "enjoy"},
                              {"Окончание ly", "happily", "happi"},
                              {"Окончание ness", "kindness", "kind"},
                              {"Окончание ment", "agreement", "agree"},
                              {"Слово без подходящего окончания", "field", "field"},
                              {"Число не изменяется", "12345", "12345"}};
    for (const WordCase &test : stems)
        tests.stem(test);
    puts("\nДополнительные тесты: лемматизация");
    const WordCase lemmas[] = {{"Неправильный глагол", "went", "go"},
                               {"Неправильное множественное число", "children", "child"},
                               {"Восстановление y", "studies", "study"},
                               {"Лемма отличается от основы", "running", "run"},
                               {"Единственное число", "particles", "particle"},
                               {"Исключение physics", "physics", "physics"}};
    for (const WordCase &test : lemmas)
        tests.lemma(test);
    char expanded[64];
    bool good = ir_lemma_copy("has", expanded, sizeof(expanded));
    tests.result("Расширяющаяся английская лемма", good && !strcmp(expanded, "have"), "have",
                 good ? expanded : "ошибка");
    good = ir_lemma_copy("ДЕТИ", expanded, sizeof(expanded));
    tests.result("Расширяющаяся русская лемма", good && !strcmp(expanded, "ребенок"), "ребенок",
                 good ? expanded : "ошибка");
    char exact[5] = {};
    good = ir_lemma_copy("has", exact, sizeof(exact));
    tests.result("Буфер ровно по размеру результата", good && !strcmp(exact, "have"), "have",
                 good ? exact : "ошибка");
    char small[4] = {'x', 'x', 'x', 0};
    good = ir_lemma_copy("has", small, sizeof(small));
    tests.result("Маленький буфер: отказ без записи", !good && !strcmp(small, "xxx"), "xxx", small);
    printf("Итого: %d; успешно: %d; ошибок: %d\n", tests.total, tests.total - tests.failed,
           tests.failed);
    return tests.failed ? 1 : 0;
}
