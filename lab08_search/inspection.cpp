#include "inspection.h"
#include <errno.h>
#include <signal.h>
#include <sys/wait.h>
#include <unistd.h>

namespace web {
namespace {
const char *kinds[] = {"index", "terms", "corpus", "memory", "crawler", "timing", "zipf"};
bool parameter(const char *input, const char *name, Text &value) {
    if (!input) return false;
    for (const char *part = input; *part;) {
        const char *end = strchr(part, '&');
        if (!end) end = part + strlen(part);
        const char *equal = (const char *)memchr(part, '=', end - part);
        if (equal && (size_t)(equal - part) == strlen(name) && !strncmp(part, name, equal - part))
            return decode_url(equal + 1, end - equal - 1, value);
        part = *end ? end + 1 : end;
    }
    return false;
}
void quote(Text &out, const char *text) {
    out.letter('"');
    for (; *text; ++text) {
        unsigned char c = (unsigned char)*text;
        if (c == '"' || c == '\\') { out.letter('\\'); out.letter(c); }
        else if (c < 32) out.format("\\u%04x", c);
        else out.letter(c);
    }
    out.letter('"');
}
int run_command(const char *command, Text &out) {
    FILE *capture = tmpfile();
    if (!capture) { out.add("Не удалось создать файл вывода\n"); return 500; }
    fflush(NULL);
    pid_t child = fork();
    if (child < 0) { fclose(capture); out.add("Не удалось создать процесс\n"); return 500; }
    if (!child) {
        alarm(30);
        dup2(fileno(capture), STDOUT_FILENO);
        dup2(fileno(capture), STDERR_FILENO);
        execl("/bin/sh", "sh", "-e", "-c", command, (char *)NULL);
        perror("/bin/sh"); _exit(127);
    }
    int result = 0; pid_t waited;
    do { waited = waitpid(child, &result, 0); } while (waited < 0 && errno == EINTR);
    rewind(capture); char buffer[4096]; size_t count;
    while ((count = fread(buffer, 1, sizeof buffer, capture))) out.add(buffer, count);
    bool read_ok = !ferror(capture); fclose(capture);
    bool ok = read_ok && waited == child && WIFEXITED(result) && WEXITSTATUS(result) == 0;
    if (!ok) out.add("\nКоманда завершилась с ошибкой или превысила лимит времени.\n");
    return ok ? 200 : 500;
}
}
bool inspection_command(const char *kind, Text &command) {
    if (!strcmp(kind, "index")) {
        command.add("wc -c \\\n  lab01_corpus/data/native_index/dictionary.bin \\\n  lab01_corpus/data/native_index/postings.bin \\\n  lab01_corpus/data/native_index/documents.bin \\\n  lab01_corpus/data/native_index/meta.bin \\\n  lab01_corpus/data/native_index/stats.json\n\nprintf '\\nАрхив индекса, байт:\\n'\nwc -c lab07_index/results/native_index.tar.gz");
    } else if (!strcmp(kind, "terms")) {
        command.add("cat lab01_corpus/data/native_index/stats.json");
    } else if (!strcmp(kind, "corpus")) {
        command.add("find lab01_corpus/data/text/scoap3 -type f -name '*.txt.gz' -print > build/corpus_files.txt\nwc -l build/corpus_files.txt\nfind lab01_corpus/data/text/scoap3 -type f -name '*.txt.gz' \\\n");
#ifdef __APPLE__
        command.add("  -exec stat -f '%z' {} + > build/corpus_sizes.txt\n");
#else
        command.add("  -exec stat -c '%s' {} + > build/corpus_sizes.txt\n");
#endif
        command.add("awk '{bytes += $1} END {printf \"Сжатые тексты: %.0f Б (%.2f МиБ)\\n\", bytes, bytes / 1048576}' build/corpus_sizes.txt\nprintf '\\nСохранённая статистика корпуса:\\n'\ncat lab01_corpus/results/stats.json");
    } else if (!strcmp(kind, "memory")) {
        command.format("printf 'RSS и VSZ указаны в КиБ; память текущего процесса сервера:\\n'\nps -o pid,rss,vsz,etime,command -p %ld", (long)getpid());
    } else if (!strcmp(kind, "crawler")) {
        command.add("cat lab02_crawler/results/raw_crawl_summary.json");
    } else if (!strcmp(kind, "timing")) {
        command.add("cat lab08_search/results/benchmark.json");
    } else if (!strcmp(kind, "zipf")) {
        command.add("cat lab05_zipf/results/zipf.json\nwc -c lab05_zipf/results/zipf.svg");
    } else return false;
    return true;
}
int inspection_execute(const char *kind, Text &out) {
    Text command;
    if (!inspection_command(kind, command)) { out.add("Неизвестный вид проверки\n"); return 400; }
    return run_command(command.data, out);
}
bool inspection_route(const char *path, const char *parameters, Text &body, const char *&type, int &status) {
    if (!strcmp(path, "/api/inspection-commands")) {
        type = "application/json; charset=utf-8"; body.letter('{');
        for (size_t i = 0; i < sizeof kinds / sizeof *kinds; ++i) {
            if (i) body.letter(','); Text command; inspection_command(kinds[i], command);
            quote(body, kinds[i]); body.letter(':'); quote(body, command.data);
        }
        body.letter('}'); return true;
    }
    if (!strcmp(path, "/api/tests")) {
        type = "application/json; charset=utf-8"; Text lab, out;
        if (!parameter(parameters, "lab", lab) || (strcmp(lab.data, "3") && strcmp(lab.data, "4"))) {
            status = 400; body.add("{\"ok\":false,\"output\":\"Выберите тесты лабы 3 или 4\"}"); return true;
        }
        status = run_command(!strcmp(lab.data, "3") ? "./build/ir test-tokenizer" : "./build/ir test-stemmer", out);
        body.format("{\"ok\":%s,\"output\":", status == 200 ? "true" : "false"); quote(body, out.data); body.letter('}'); return true;
    }
    if (strcmp(path, "/api/info")) return false;
    type = "text/plain; charset=utf-8"; Text kind;
    if (!parameter(parameters, "kind", kind)) { status = 400; body.add("Не указан вид проверки\n"); }
    else status = inspection_execute(kind.data, body);
    return true;
}
int ir_inspection_main(int argc, char **argv) {
    if (argc != 2) { fprintf(stderr, "usage: inspect corpus|index|terms|memory|crawler|timing|zipf\n"); return 2; }
    Text out; int status = inspection_execute(argv[1], out);
    fwrite(out.data, 1, out.size, stdout); return status == 200 ? 0 : 1;
}
int inspection_tests() {
    int errors = 0; Text command;
    if (inspection_command("index; echo injected", command)) ++errors;
    Text index; inspection_command("index", index);
    if (!strstr(index.data, "wc -c") || !strstr(index.data, "postings.bin")) ++errors;
    Text body; const char *type = "text/html"; int status = 200;
    if (!inspection_route("/api/info", "kind=unknown", body, type, status) || status != 400) ++errors;
    Text bad; status = 200;
    if (!inspection_route("/api/tests", "lab=6", bad, type, status) || status != 400) ++errors;
    fprintf(stderr, "inspection helpers: %s (%d failures)\n", errors ? "FAILED" : "OK", errors);
    return errors;
}
}
