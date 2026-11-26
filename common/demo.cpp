#include "demo.h"
#include "crawler.h"
#include "index.h"
#include "text.h"
#include <ctype.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>


namespace {
struct FileText {
    char *data;
    FileText(const char *path) : data(NULL) {
        FILE *file = fopen(path, "rb");
        if (!file) return;
        if (fseek(file, 0, SEEK_END)) { fclose(file); return; }
        long size = ftell(file);
        if (size < 0 || size > 32 * 1024 * 1024 || fseek(file, 0, SEEK_SET)) { fclose(file); return; }
        data = (char *)malloc((size_t)size + 1);
        if (!data) { fclose(file); return; }
        size_t count = fread(data, 1, (size_t)size, file);
        fclose(file);
        if (count != (size_t)size) { free(data); data = NULL; return; }
        data[count] = 0;
    }
    ~FileText() { free(data); }
};
const char *field(const char *data, const char *key) {
    if (!data) return NULL;
    char quoted[128];
    if (snprintf(quoted, sizeof quoted, "\"%s\"", key) >= (int)sizeof quoted) return NULL;
    const char *value = strstr(data, quoted);
    if (!value) return NULL;
    value += strlen(quoted);
    while (*value && isspace((unsigned char)*value)) ++value;
    if (*value++ != ':') return NULL;
    while (*value && isspace((unsigned char)*value)) ++value;
    return value;
}
bool number(const char *data, const char *key, double &result) {
    const char *value = field(data, key);
    if (!value) return false;
    char *end = NULL; errno = 0;
    result = strtod(value, &end);
    return !errno && end != value;
}
size_t text_width(const char *text) {
    size_t width=0;
    for (;*text;++text) if (((unsigned char)*text & 0xc0)!=0x80) ++width;
    return width;
}
void print_cell(const char *text, size_t width) {
    fputs(text,stdout);
    size_t used=text_width(text);
    while (used++<width) putchar(' ');
}
void print_value(double value, int decimals) {
    char text[128]; snprintf(text,sizeof text,"%.*f",decimals,value);
    if (decimals) { fputs(text,stdout); return; }
    size_t length=strlen(text);
    for (size_t i=0;i<length;++i) {
        if (i && (length-i)%3==0 && text[i-1]!='-') putchar(' ');
        putchar(text[i]);
    }
}
bool show_number(const char *data, const char *key, const char *title, int decimals = 0) {
    double value = 0;
    if (!number(data, key, value)) { fprintf(stderr, "Не найдено поле %s\n", key); return false; }
    fputs("  ",stdout); print_cell(title,32); print_value(value,decimals); putchar('\n');
    return true;
}
void saved() { puts("\nСохранённый результат полного прогона:"); }
bool saved_file(const FileText &file, const char *path) {
    if (file.data) return true;
    fprintf(stderr, "Не удалось прочитать результат: %s\n", path); return false;
}
int corpus_demo() {
    puts("Смотрим состав общего корпуса и сохранённую статистику.");
    FILE *file = fopen("lab01_corpus/data/native_documents.tsv", "r");
    if (!file) { perror("native_documents.tsv"); return 1; }
    char *line = NULL; size_t capacity = 0; size_t documents = 0, source_count = 0;
    char **sources = NULL; getline(&line, &capacity, file);
    while (getline(&line, &capacity, file) >= 0) {
        char *journal = strrchr(line, '\t');
        if (!journal) { fclose(file); free(line); return 1; }
        ++journal; journal[strcspn(journal, "\r\n")] = 0;
        bool known = false;
        for (size_t i = 0; i < source_count; ++i) if (!strcmp(sources[i], journal)) known = true;
        if (!known) {
            char **next = (char **)realloc(sources, (source_count + 1) * sizeof(char *));
            if (!next) { fclose(file); free(line); return 1; }
            sources = next; sources[source_count] = strdup(journal);
            if (!sources[source_count]) { fclose(file); free(line); return 1; }
            ++source_count;
        }
        ++documents;
    }
    fclose(file); free(line);
    printf("  Документов в текущем каталоге:  %zu\n", documents);
    printf("  Журналов-источников:            %zu\n", source_count);
    for (size_t i = 0; i < source_count; ++i) free(sources[i]); free(sources);
    const char *path = "lab01_corpus/results/stats.json"; FileText stats(path);
    if (!saved_file(stats, path)) return 1;
    saved();
    if (!show_number(stats.data, "text_bytes", "Байт выделенного текста:")) return 1;
    puts("  Каталог: lab01_corpus/data/native_documents.tsv");
    puts("  Происхождение: lab01_corpus/data/provenance/scoap3.tsv");
    return documents ? 0 : 1;
}
int crawler_demo() {
    puts("Проверяем сохранённый итог робота и маленькие локальные тесты.");
    const char *path = "lab02_crawler/results/raw_crawl_summary.json"; FileText stats(path);
    if (!saved_file(stats, path)) return 1;
    saved();
    const char *state=field(stats.data,"status");
    if (!state || strncmp(state,"\"complete\"",10)) {
        fprintf(stderr,"Сохранённая база робота ещё не завершена\n"); return 1;
    }
    bool ok = show_number(stats.data, "canonical_articles", "Статей:") &&
              show_number(stats.data, "raw_original_documents", "Оригиналов XML/PDF:") &&
              show_number(stats.data, "raw_html_documents", "HTML-страниц:") &&
              show_number(stats.data, "missing_resources", "Недостающих ресурсов:");
    if (!ok) return 1;
    puts("\nСейчас выполняются локальные тесты базы, очереди и переобкачки.");
    puts("Полный корпус повторно не скачивается."); fflush(stdout);
    return ir_crawler_tests() ? 1 : 0;
}
struct TokenCounter { size_t count; };
void print_token(const char *text, size_t length, void *state) {
    TokenCounter *counter = (TokenCounter *)state;
    printf("  %zu. %.*s\n", ++counter->count, (int)length, text);
}
int tokenizer_demo() {
    const char *example = "Dark matter, частицы и Союз-2: 3.14 GeV!";
    printf("Разбиваем настоящий пример на токены:\n  %s\n\n", example);
    TokenCounter counter = {0}; ir_tokenize(example, strlen(example), print_token, &counter);
    printf("\n  Получено токенов: %zu\n", counter.count);
    const char *path = "lab03_tokenizer/results/stats.json"; FileText stats(path);
    if (!saved_file(stats, path)) return 1;
    saved();
    return show_number(stats.data, "documents", "Документов:") &&
           show_number(stats.data, "tokens", "Токенов:") &&
           show_number(stats.data, "avg_token_length", "Средняя длина токена:", 3) ? 0 : 1;
}
int stemmer_demo() {
    puts("Сравниваем формы слова, основу и результат правил лемматизации.\n");
    const char *words[] = {"particles", "particle", "running", "ракеты", "ракетами", "дети", "has"};
    fputs("  ",stdout); print_cell("Слово",18); print_cell("Стемминг",18); puts("Лемматизация");
    for (size_t i = 0; i < sizeof(words) / sizeof(words[0]); ++i) {
        char stem[128], lemma[128]; snprintf(stem, sizeof stem, "%s", words[i]); ir_stem(stem);
        if (!ir_lemma_copy(words[i], lemma, sizeof lemma)) return 1;
        fputs("  ",stdout); print_cell(words[i],18); print_cell(stem,18); puts(lemma);
    }
    puts("\nЛемматизация ограничена правилами и исключениями, полного словаря нет.");
    return 0;
}
int zipf_demo() {
    puts("Смотрим распределение частот и параметры модели без повторного подсчёта.");
    const char *path = "lab05_zipf/results/zipf.json"; FileText stats(path);
    if (!saved_file(stats, path)) return 1;
    saved();
    bool ok = show_number(stats.data, "documents", "Документов:") &&
              show_number(stats.data, "total_tokens", "Всего токенов:") &&
              show_number(stats.data, "unique_terms", "Разных токенов:") &&
              show_number(stats.data, "hapax_legomena", "Встретились один раз:");
    const char *fit = field(stats.data, "zipf_fit");
    if (!ok || !show_number(fit, "s", "Показатель Ципфа:", 4)) return 1;
    puts("\nГрафик: lab05_zipf/results/zipf.svg"); return 0;
}
void remove_index_demo(const char *folder, const char *manifest) {
    const char *names[] = {"dictionary.bin", "postings.bin", "documents.bin", "meta.bin", "stats.json"};
    char path[1024];
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) {
        snprintf(path, sizeof path, "%s/%s", folder, names[i]); unlink(path);
    }
    unlink(manifest); rmdir(folder);
}
int index_demo() {
    puts("Строим отдельный маленький индекс по первым 100 статьям.");
    puts("Основной индекс и документы остаются без изменений.");
    char folder[] = "/tmp/mai_lab7_demo_XXXXXX";
    if (!mkdtemp(folder)) { perror("index demo"); return 1; }
    char manifest[1024]; snprintf(manifest, sizeof manifest, "%s/sample.tsv", folder);
    FILE *source = fopen("lab01_corpus/data/native_documents.tsv", "r"), *sample = fopen(manifest, "w");
    if (!source || !sample) { if (source) fclose(source); if (sample) fclose(sample); remove_index_demo(folder, manifest); return 1; }
    char *line = NULL; size_t capacity = 0; size_t count = 0;
    while (count <= 100 && getline(&line, &capacity, source) >= 0) { fputs(line, sample); ++count; }
    free(line); fclose(source); fclose(sample);
    if (count < 2) { remove_index_demo(folder, manifest); return 1; }
    char *args[] = {(char *)"build", manifest, folder, (char *)"stem", (char *)"10000"}; fflush(stdout);
    int status = ir_index_main(5, args);
    char stats_path[1024]; snprintf(stats_path, sizeof stats_path, "%s/stats.json", folder); FileText result(stats_path);
    if (!status && result.data) {
        puts("\nИндекс, построенный сейчас:");
        if (!show_number(result.data, "documents", "Документов:") || !show_number(result.data, "terms", "Термов:") ||
            !show_number(result.data, "postings", "Постингов:") || !show_number(result.data, "time_seconds", "Время построения, с:", 3)) status = 1;
    } else status = 1;
    const char *saved_path = "lab07_index/results/stats.json"; FileText saved_stats(saved_path);
    if (!status && saved_stats.data) { saved(); show_number(saved_stats.data, "documents", "Документов:"); show_number(saved_stats.data, "terms", "Термов:"); }
    remove_index_demo(folder, manifest); return status;
}
int search_demo(const char *query) {
    printf("Выполняем настоящий поиск по готовому полному индексу.\nЗапрос: %s\nПоказываем первые 5 результатов.\n\n", query);
    FILE *input = tmpfile(); if (!input) return 1;
    fprintf(input, "%s\n", query); fflush(input); rewind(input);
    int previous = dup(STDIN_FILENO);
    if (previous < 0 || dup2(fileno(input), STDIN_FILENO) < 0) { if (previous >= 0) close(previous); fclose(input); return 1; }
    clearerr(stdin);
    char *args[] = {(char *)"search", (char *)"lab01_corpus/data/native_index", (char *)"--limit", (char *)"5"}; fflush(stdout);
    int status = ir_search_main(4, args);
    dup2(previous, STDIN_FILENO); close(previous); fclose(input); clearerr(stdin);
    puts("\nВеб-интерфейс: ./run.sh web"); return status;
}
}

int ir_demo_main(int argc, char **argv) {
    char *end = NULL; errno = 0;
    long lab = argc > 1 ? strtol(argv[1], &end, 10) : 0;
    if (argc < 2 || errno || end == argv[1] || *end || lab < 1 || lab > 8 || lab == 6) {
        fprintf(stderr, "Использование: demo НОМЕР_ЛАБЫ [запрос]\n"); return 2;
    }
    const char *names[] = {"Корпус документов", "Поисковый робот", "Токенизация", "Стемминг и лемматизация", "Закон Ципфа", "", "Булев индекс", "Булев поиск"};
    printf("\n============================================================\nЛабораторная %ld. %s\n============================================================\n\n", lab, names[lab - 1]); fflush(stdout);
    timespec started, finished; clock_gettime(CLOCK_MONOTONIC, &started); int status = 0;
    switch (lab) {
    case 1: status = corpus_demo(); break;
    case 2: status = crawler_demo(); break;
    case 3: status = tokenizer_demo(); break;
    case 4: status = stemmer_demo(); break;
    case 5: status = zipf_demo(); break;
    case 7: status = index_demo(); break;
    case 8: status = search_demo(argc > 2 ? argv[2] : "dark matter"); break;
    }
    clock_gettime(CLOCK_MONOTONIC, &finished);
    double seconds = finished.tv_sec - started.tv_sec + (finished.tv_nsec - started.tv_nsec) / 1000000000.;
    printf("\n------------------------------------------------------------\nСтатус: %s\nВремя демонстрации: %.3f с\n", status ? "ОШИБКА" : "ГОТОВО", seconds); fflush(stdout);
    return status;
}
