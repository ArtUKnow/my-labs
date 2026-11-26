#include "dashboard.h"
#include "demo.h"
#include "inspection.h"
#include "examples.h"
#include "corpus_sources.h"
#include "dashboard_script.h"
#include <dirent.h>
#include <errno.h>
#include <limits.h>
#include <signal.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

namespace web {
namespace {
const int lab_numbers[] = {1, 2, 3, 4, 5, 7, 8};
const char *lab_folders[] = {"lab01_corpus", "lab02_crawler", "lab03_tokenizer", "lab04_stemmer", "lab05_zipf", "lab07_index", "lab08_search"};
const char *lab_names[] = {"Корпус документов", "Поисковый робот", "Токенизация", "Стемминг", "Закон Ципфа", "Булев индекс", "Булев поиск"};
const char *lab_descriptions[] = {"Состав коллекции, источники и объём текстов", "Исходные документы, очередь и обновления", "Как текст превращается в последовательность токенов", "Как разные формы слова получают общую основу", "Частоты слов и распределение по рангам", "Словарь и списки документов для быстрого поиска", "Запросы с И, ИЛИ, НЕ и скобками"};
int lab_index(int lab) {
    for (int i = 0; i < 7; ++i) if (lab_numbers[i] == lab) return i;
    return -1;
}
void json_string(Text &output, const char *text) {
    output.letter('"');
    for (; *text; ++text) {
        unsigned char value = (unsigned char)*text;
        if (value == '"' || value == '\\') { output.letter('\\'); output.letter((char)value); }
        else if (value < 32) output.format("\\u%04x", value);
        else output.letter((char)value);
    }
    output.letter('"');
}
bool parameter(const char *parameters, const char *wanted, Text &result) {
    if (!parameters) return false;
    const char *cursor = parameters;
    while (*cursor) {
        const char *end = strchr(cursor, '&'); if (!end) end = cursor + strlen(cursor);
        const char *equal = (const char *)memchr(cursor, '=', (size_t)(end - cursor));
        Text key;
        if (!decode_url(cursor, (size_t)((equal ? equal : end) - cursor), key)) return false;
        if (!strcmp(key.data, wanted)) return equal && decode_url(equal + 1, (size_t)(end - equal - 1), result);
        cursor = *end ? end + 1 : end;
    }
    return false;
}
int read_lab(const char *parameters) {
    Text value;
    if (!parameter(parameters, "lab", value) || !value.size) return -1;
    errno = 0; char *end = NULL; long number = strtol(value.data, &end, 10);
    if (errno || end == value.data || *end || number < 1 || number > 8 || lab_index((int)number) < 0) return -1;
    return (int)number;
}
bool source_extension(const char *name) {
    const char *dot = strrchr(name, '.');
    return dot && (!strcmp(dot, ".cpp") || !strcmp(dot, ".h") || !strcmp(dot, ".sql"));
}
bool allowed_source(const char *file) {
    if (!file || !*file || file[0] == '/' || strstr(file, "..") || strchr(file, '\\')) return false;
    if (!strcmp(file, "main.cpp") || !strcmp(file, "Makefile") || !strcmp(file, "run.sh")) return true;
    const char *slash = strchr(file, '/'); if (!slash || !source_extension(file)) return false;
    size_t length = (size_t)(slash - file); bool folder = length == 6 && !strncmp(file, "common", 6);
    for (int i = 0; i < 7; ++i) if (strlen(lab_folders[i]) == length && !strncmp(file, lab_folders[i], length)) folder = true;
    if (!folder) return false;
    const char *tail = slash + 1;
    if (!*tail || *tail == '.') return false;
    const char *second = strchr(tail, '/');
    if (second && (strncmp(tail, "sql/", 4) || strchr(second + 1, '/') || second[1] == '.')) return false;
    return true;
}
bool read_file(const char *file, Text &body, size_t limit) {
    char root[PATH_MAX], full[PATH_MAX];
    if (!realpath(".", root) || !realpath(file, full)) return false;
    size_t root_length = strlen(root);
    if (strncmp(root, full, root_length) || full[root_length] != '/') return false;
    struct stat info;
    if (stat(full, &info) || !S_ISREG(info.st_mode) || info.st_size < 0 || (uint64_t)info.st_size > limit) return false;
    FILE *input = fopen(full, "rb"); if (!input) return false;
    char buffer[8192]; size_t count;
    while ((count = fread(buffer, 1, sizeof buffer, input)) > 0) body.add(buffer, count);
    bool valid = !ferror(input); fclose(input); return valid;
}
void add_file(Text &output, const char *file, int lab, bool &first) {
    if (!allowed_source(file)) return;
    struct stat info; if (stat(file, &info) || !S_ISREG(info.st_mode)) return;
    if (!first) output.letter(','); first = false;
    output.add("{\"path\":"); json_string(output, file); output.format(",\"lab\":%d,\"bytes\":%llu}", lab, (unsigned long long)info.st_size);
}
void scan_folder(Text &output, const char *folder, int lab, bool &first, bool nested = false) {
    DIR *directory = opendir(folder); if (!directory) return;
    dirent *entry;
    while ((entry = readdir(directory))) {
        if (entry->d_name[0] == '.') continue;
        char path[PATH_MAX]; int count = snprintf(path, sizeof path, "%s/%s", folder, entry->d_name);
        if (count < 0 || (size_t)count >= sizeof path) continue;
        if (!nested && !strcmp(entry->d_name, "sql")) scan_folder(output, path, lab, first, true);
        else add_file(output, path, lab, first);
    }
    closedir(directory);
}
void list_files(Text &body) {
    body.letter('['); bool first = true;
    for (int i = 0; i < 7; ++i) scan_folder(body, lab_folders[i], lab_numbers[i], first);
    scan_folder(body, "common", 0, first);
    add_file(body, "main.cpp", 0, first); add_file(body, "Makefile", 0, first); add_file(body, "run.sh", 0, first);
    body.letter(']');
}
void run_demo(int lab, Text &body, int &status) {
    FILE *output = tmpfile();
    if (!output) { status = 500; body.add("{\"ok\":false,\"output\":\"Не удалось создать файл вывода\"}"); return; }
    fflush(NULL); timespec start, finish; clock_gettime(CLOCK_MONOTONIC, &start);
    pid_t child = fork();
    if (child < 0) { fclose(output); status = 500; body.add("{\"ok\":false,\"output\":\"Не удалось запустить демонстрацию\"}"); return; }
    if (!child) {
        alarm(15); dup2(fileno(output), STDOUT_FILENO); dup2(fileno(output), STDERR_FILENO); clearerr(stdout); clearerr(stderr);
        char number[8]; snprintf(number, sizeof number, "%d", lab);
        char *arguments[] = {(char *)"demo", number};
        int result = ir_demo_main(2, arguments); fflush(stdout); fflush(stderr); _exit(result);
    }
    int result = 0; bool waited = false;
    while (true) { if (waitpid(child, &result, 0) >= 0) { waited = true; break; } if (errno != EINTR) break; }
    clock_gettime(CLOCK_MONOTONIC, &finish); rewind(output); Text text; char buffer[4096]; size_t count;
    while (text.size < 256 * 1024 && (count = fread(buffer, 1, sizeof buffer, output)) > 0) text.add(buffer, count);
    fclose(output);
    bool ok = waited && WIFEXITED(result) && WEXITSTATUS(result) == 0;
    if (!ok) { status = 500; if (!text.size) text.add("Демонстрация остановлена или завершилась ошибкой."); }
    double elapsed = (finish.tv_sec - start.tv_sec) * 1000. + (finish.tv_nsec - start.tv_nsec) / 1000000.;
    body.format("{\"ok\":%s,\"elapsed_ms\":%.3f,\"output\":", ok ? "true" : "false", elapsed); json_string(body, text.data); body.add("}");
}
void dashboard(Text &body) {
    body.add(R"HTML(<!doctype html><html lang="ru"><head><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Лабораторные · Информационный поиск</title><style>
:root{font-family:system-ui,-apple-system,sans-serif;color:#24334a;background:#eef2f7}*{box-sizing:border-box}body{margin:0}a{color:inherit}.layout{display:grid;grid-template-columns:285px 1fr;min-height:100vh}aside{background:#152337;color:#dce7f7;padding:35px 24px;position:sticky;top:0;height:100vh;overflow:auto}.brand{font-size:23px;font-weight:750;color:white}.course{font-size:14px;color:#91a8c6;margin:8px 0 30px}.navlab{display:block;width:100%;text-align:left;border:0;background:transparent;color:#c5d4e9;padding:13px 12px;border-radius:9px;font:inherit;font-size:14px;cursor:pointer;margin:4px 0}.navlab.active{background:#314865;color:white}.navlab span{display:inline-block;width:28px;color:#89afd9}.sidefoot{font-size:12px;color:#91a8c6;line-height:1.7;margin-top:35px}.sidefoot a{display:block;margin-top:8px}main{padding:48px min(6vw,75px);max-width:1450px;width:100%}.eyebrow{font-size:13px;color:#60728b;margin-bottom:10px}h1{font-size:38px;line-height:1.2;margin:0 0 15px;letter-spacing:-1px}.lead{color:#61718a;line-height:1.7;max-width:800px}.topline{display:flex;align-items:start;justify-content:space-between;gap:20px}.badge{white-space:nowrap;background:#e1eaf5;color:#31587e;border-radius:30px;padding:8px 14px;font-size:12px}.tabs{display:flex;border-bottom:1px solid #d5deeb;gap:26px;margin:30px 0}.tab{font:inherit;background:none;border:0;padding:15px 0;color:#75849b;cursor:pointer;border-bottom:3px solid transparent}.tab.active{border-color:#2e629c;color:#234d7d}.card{background:white;border:1px solid #dce3ed;border-radius:16px;padding:28px;box-shadow:0 10px 35px #18345906}.summary{display:grid;grid-template-columns:repeat(3,1fr);gap:14px;margin-bottom:20px}.stat{background:white;border:1px solid #dce3ed;border-radius:12px;padding:18px}.stat strong{display:block;font-size:23px;margin-bottom:4px}.stat span{color:#748198;font-size:13px}h2{font-size:21px;margin:0 0 10px}p{line-height:1.7}.muted{color:#6e7d93;font-size:14px}.primary{background:#2e629c;color:white;border:0;border-radius:9px;padding:13px 22px;font:inherit;font-weight:600;cursor:pointer}.primary:disabled{background:#7b91ad;cursor:wait}.actions{display:flex;align-items:center;gap:18px;flex-wrap:wrap;margin:22px 0}.status{font-size:13px;color:#6b7a90}.output{white-space:pre-wrap;overflow:auto;background:#122034;color:#d6e6fb;border-radius:12px;padding:24px;font:14px/1.65 ui-monospace,SFMono-Regular,monospace;min-height:240px;margin:18px 0 0}.files{display:grid;grid-template-columns:245px minmax(0,1fr);gap:20px}.filelist{display:flex;flex-direction:column;gap:5px;max-height:620px;overflow:auto}.file{border:0;background:#f0f4f9;padding:10px;border-radius:7px;text-align:left;cursor:pointer;font:12px/1.5 ui-monospace,monospace;overflow-wrap:anywhere}.file.active{background:#dce9f8;color:#1c5085}.code{margin:0;max-height:680px;overflow:auto;background:#152337;color:#e0e9f6;font:13px/1.7 ui-monospace,monospace;border-radius:10px;padding:22px;white-space:pre;tab-size:4}.hidden{display:none}.reportlinks{display:flex;gap:15px;margin:20px 0}.reportlinks a{display:inline-block;background:#edf3fa;border-radius:8px;padding:13px 20px;color:#285682;text-decoration:none}.reportframe{width:100%;height:600px;border:1px solid #dce3ed;border-radius:10px;background:#fafbfe}.error{color:#b74141}@media(max-width:850px){.layout{display:block}aside{position:static;height:auto;padding:20px}.course,.sidefoot{display:none}#nav{display:flex;overflow:auto;margin-top:15px}.navlab{min-width:155px;width:auto}.summary{grid-template-columns:1fr}main{padding:28px 20px}.files{grid-template-columns:1fr}.filelist{max-height:180px}.topline{display:block}.badge{display:inline-block;margin:8px 0}h1{font-size:30px}.reportframe{height:450px}}
.examples,.tools{display:flex;gap:8px;flex-wrap:wrap;margin:15px 0}.example,.tools button{font:inherit;font-size:13px;cursor:pointer;border:1px solid #d5deeb;background:#f3f6fb;border-radius:8px;padding:10px 13px}.example.active{background:#dce9f8;border-color:#6292c5;color:#234d7d}.command{white-space:pre-wrap;overflow-wrap:anywhere;background:#f1f5fa;padding:18px;border-radius:9px;font:13px/1.6 ui-monospace,monospace}.primary{white-space:pre-wrap;text-align:left;overflow-wrap:anywhere;max-width:100%;font:13px/1.6 ui-monospace,monospace}.sourceTable{overflow:auto}table{border-collapse:collapse;width:100%;font-size:14px}th,td{padding:16px 12px;text-align:left;border-bottom:1px solid #e1e7ef;vertical-align:top}td:first-child{min-width:200px}td:last-child{min-width:220px}details p{font-size:13px}summary{cursor:pointer;color:#285682}#sourceFilter{font:inherit;padding:12px 15px;border:1px solid #cad6e7;border-radius:8px;width:min(100%,400px);margin:15px 0}.tabs{flex-wrap:wrap;gap:20px}
</style></head><body><div class="layout"><aside><div class="brand">Информационный<br>поиск</div><div class="course"></div><nav id="nav">)HTML");
    for (int i = 0; i < 7; ++i) { body.format("<button class=\"navlab%s\" data-lab=\"%d\"><span>%02d</span>", i ? "" : " active", lab_numbers[i], lab_numbers[i]); escape_html(body, lab_names[i]); body.add("</button>"); }
    body.add(R"HTML(</nav></aside><main><div class="topline"><div><h1 id="labTitle">Корпус документов</h1></div></div><p class="lead" id="labDescription">Состав коллекции, источники и объём текстов</p><div class="tabs"><button class="tab active" data-tab="run">Команды и примеры</button><button class="tab" data-tab="code">Исходники</button><button class="tab" data-tab="sources">Источники корпуса</button><button class="tab" data-tab="report">Отчёт</button></div><section id="panel-run"><div class="card" id="consoleCard"><h2>Команды и примеры</h2><div id="exampleList" class="examples"></div><p class="muted" id="demoNote"></p><pre class="command" id="command"></pre><div class="actions"><button class="primary" id="runButton" disabled>Загрузка команд…</button><span class="status" id="runStatus">Готово к запуску</span></div><div class="tools"><button data-info="index">Размер файлов индекса</button><button data-info="terms">Термины и постинги</button><button data-info="corpus">Размер корпуса</button><button data-info="memory">Память приложения</button><button data-info="crawler">Итог загрузки</button><button data-info="timing">Измерения поиска</button></div><div class="tools"><button id="clearConsole">Очистить вывод</button><button id="copyConsole">Копировать вывод</button><button id="saveConsole">Скачать вывод</button></div><pre class="output" id="output"></pre><p class="muted" id="consoleInfo"></p></div></section><section id="panel-sources" class="hidden"><div class="card"><h2>Откуда взяты статьи</h2><p>Научные статьи по физике высоких энергий. Ниже — журналы, количество документов и примеры публикаций.</p><p id="sourceTotals">Чтение каталога…</p><p class="muted">Количество статей, годы и примеры считаются по каталогу корпуса: <code id="sourceManifest"></code>. HTML и XML/PDF одной статьи не увеличивают число статей.</p><input id="sourceFilter" type="search" placeholder="Найти журнал" aria-label="Найти журнал"><div class="sourceTable"><table><thead><tr><th>Журнал</th><th>Статей</th><th>Доля</th><th>Годы в корпусе</th><th>Примеры</th></tr></thead><tbody id="sourceRows"></tbody></table></div></div></section><section id="panel-code" class="hidden"><div class="card"><h2>Исходный код</h2><p class="muted">Файлы выбранной лабораторной и общие компоненты. Выберите файл слева.</p><div class="files"><div class="filelist" id="fileList"></div><pre class="code" id="source">Загрузка списка файлов…</pre></div></div></section><section id="panel-report" class="hidden"><div class="card"><h2>Отчёт по лабораторной</h2><p class="muted">PDF для просмотра и редактируемый документ для работы над отчётом.</p><div class="reportlinks"><a id="pdfLink" target="_blank">Открыть PDF ↗</a><a id="docxLink">Скачать DOCX</a></div><iframe class="reportframe" id="reportFrame" title="Отчёт по лабораторной работе"></iframe></div></section></main></div><script>
const labs=)HTML");
    body.letter('[');
    for (int i = 0; i < 7; ++i) { if (i) body.letter(','); body.format("{id:%d,title:", lab_numbers[i]); json_string(body, lab_names[i]); body.add(",description:"); json_string(body, lab_descriptions[i]); body.add("}"); }
    body.add("];");body.add(dashboard_script);body.add("</script></body></html>");
}
}
bool dashboard_route(const char *path, const char *parameters, Text &body, const char *&content_type, int &status) {
    if (inspection_route(path, parameters, body, content_type, status) || examples_route(path, parameters, body, content_type, status) || corpus_sources_route(path, body, content_type, status)) return true;
    if (!strcmp(path, "/api/health")) { content_type = "application/json; charset=utf-8"; body.add("{\"app\":\"mai-labs\",\"ok\":true}"); return true; }
    if (!strcmp(path, "/")) { dashboard(body); return true; }
    if (!strcmp(path, "/api/files")) { content_type = "application/json; charset=utf-8"; list_files(body); return true; }
    if (!strcmp(path, "/api/run")) {
        content_type = "application/json; charset=utf-8"; int lab = read_lab(parameters);
        if (lab < 0) { status = 400; body.add("{\"ok\":false,\"output\":\"Неизвестная лабораторная\",\"elapsed_ms\":0}"); }
        else run_demo(lab, body, status);
        return true;
    }
    if (!strcmp(path, "/api/code")) {
        content_type = "text/plain; charset=utf-8"; Text file;
        if (!parameter(parameters, "file", file) || !allowed_source(file.data)) { status = 400; body.add("Недопустимый путь к исходнику"); }
        else if (!read_file(file.data, body, 2 * 1024 * 1024)) { status = 404; body.add("Исходник не найден"); }
        return true;
    }
    if (!strcmp(path, "/report") || !strcmp(path, "/guide")) {
        char file[PATH_MAX]; bool docx = false;
        if (!strcmp(path, "/guide")) snprintf(file, sizeof file, "reports/Защита_за_час.pdf");
        else {
            int lab = read_lab(parameters); Text format;
            if (lab < 0 || !parameter(parameters, "format", format) || (strcmp(format.data, "pdf") && strcmp(format.data, "docx"))) { status = 400; body.add("Некорректный отчёт"); return true; }
            docx = !strcmp(format.data, "docx"); snprintf(file, sizeof file, "reports/lab%02d.%s", lab, format.data);
        }
        content_type = docx ? "application/vnd.openxmlformats-officedocument.wordprocessingml.document" : "application/pdf";
        if (!read_file(file, body, 20 * 1024 * 1024)) { status = 404; content_type = "text/plain; charset=utf-8"; body.add("Документ не найден"); }
        return true;
    }
    return false;
}
int dashboard_tests() {
    int errors = 0;
    if (lab_index(6) >= 0 || lab_index(8) < 0 || read_lab("lab=6") >= 0 || read_lab("lab=3") != 3) ++errors;
    if (!allowed_source("lab03_tokenizer/tokenizer.cpp") || !allowed_source("lab02_crawler/sql/schema.sql") || allowed_source("../main.cpp") || allowed_source("/etc/passwd") || allowed_source("lab01_corpus/data/file.h") || allowed_source("lab06_quality/metrics.cpp")) ++errors;
    Text page; const char *type = "text/html; charset=utf-8"; int status = 200;
    if (!dashboard_route("/", NULL, page, type, status) || status != 200 || !strstr(page.data, "data-lab=\"3\"") || strstr(page.data, "data-lab=\"6\"")) ++errors;
    Text bad; status = 200; if (!dashboard_route("/api/code", "file=..%2Fmain.cpp", bad, type, status) || status != 400) ++errors;
    fprintf(stderr, "dashboard helpers: %s (%d failures)\n", errors ? "FAILED" : "OK", errors); return errors;
}
}
