#include "server.h"
#include "index.h"
#include <arpa/inet.h>
#include <ctype.h>
#include <errno.h>
#include <limits.h>
#include <math.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>

namespace {
struct Text {
    char *data;
    size_t size, capacity;
    Text() : data(NULL), size(0), capacity(0) {
        add("");
    }
    ~Text() {
        free(data);
    }
    void reserve(size_t count) {
        if (count <= capacity)
            return;
        size_t next = capacity ? capacity : 128;
        while (next < count)
            next *= 2;
        char *memory = (char *)realloc(data, next);
        if (!memory) {
            perror("server memory");
            exit(1);
        }
        data = memory;
        capacity = next;
    }
    void add(const char *text) {
        add(text, strlen(text));
    }
    void add(const char *text, size_t count) {
        reserve(size + count + 1);
        memcpy(data + size, text, count);
        size += count;
        data[size] = 0;
    }
    void letter(char value) {
        add(&value, 1);
    }
    void format(const char *format, ...) {
        va_list args, copy;
        va_start(args, format);
        va_copy(copy, args);
        int count = vsnprintf(NULL, 0, format, copy);
        va_end(copy);
        if (count < 0) {
            va_end(args);
            return;
        }
        reserve(size + (size_t)count + 1);
        vsnprintf(data + size, (size_t)count + 1, format, args);
        va_end(args);
        size += count;
    }
};
void escape_html(Text &output, const char *input) {
    for (; *input; ++input) {
        switch (*input) {
        case '&':
            output.add("&amp;");
            break;
        case '<':
            output.add("&lt;");
            break;
        case '>':
            output.add("&gt;");
            break;
        case '"':
            output.add("&quot;");
            break;
        case '\'':
            output.add("&#39;");
            break;
        default:
            output.letter(*input);
        }
    }
}
int hex_digit(char value) {
    if (value >= '0' && value <= '9')
        return value - '0';
    if (value >= 'a' && value <= 'f')
        return value - 'a' + 10;
    if (value >= 'A' && value <= 'F')
        return value - 'A' + 10;
    return -1;
}
bool decode_url(const char *input, size_t length, Text &output) {
    for (size_t i = 0; i < length; ++i) {
        unsigned char value = (unsigned char)input[i];
        if (value == '+')
            value = ' ';
        else if (value == '%') {
            if (i + 2 >= length)
                return false;
            int first = hex_digit(input[i + 1]), second = hex_digit(input[i + 2]);
            if (first < 0 || second < 0)
                return false;
            value = (unsigned char)(first * 16 + second);
            i += 2;
        }
        if (!value || value == '\r' || value == '\n')
            return false;
        output.letter((char)value);
    }
    return true;
}
void encode_url(Text &output, const char *input) {
    const char *digits = "0123456789ABCDEF";
    for (; *input; ++input) {
        unsigned char value = (unsigned char)*input;
        if ((value >= 'a' && value <= 'z') || (value >= 'A' && value <= 'Z') ||
            (value >= '0' && value <= '9') || strchr("-_.~", value))
            output.letter((char)value);
        else {
            output.letter('%');
            output.letter(digits[value >> 4]);
            output.letter(digits[value & 15]);
        }
    }
}
void utf8(Text &output, uint32_t code) {
    if (code < 0x80)
        output.letter((char)code);
    else if (code < 0x800) {
        output.letter((char)(0xc0 | (code >> 6)));
        output.letter((char)(0x80 | (code & 63)));
    } else if (code < 0x10000) {
        output.letter((char)(0xe0 | (code >> 12)));
        output.letter((char)(0x80 | ((code >> 6) & 63)));
        output.letter((char)(0x80 | (code & 63)));
    } else {
        output.letter((char)(0xf0 | (code >> 18)));
        output.letter((char)(0x80 | ((code >> 12) & 63)));
        output.letter((char)(0x80 | ((code >> 6) & 63)));
        output.letter((char)(0x80 | (code & 63)));
    }
}
struct Json {
    const char *cursor;
    bool valid;
    Json(const char *input) : cursor(input), valid(true) {
    }
    void space() {
        while (*cursor && isspace((unsigned char)*cursor))
            ++cursor;
    }
    bool take(char value) {
        space();
        if (*cursor != value) {
            valid = false;
            return false;
        }
        ++cursor;
        return true;
    }
    bool next(char value) {
        space();
        if (*cursor != value)
            return false;
        ++cursor;
        return true;
    }
    uint32_t codepoint() {
        uint32_t value = 0;
        for (int i = 0; i < 4; ++i) {
            int digit = hex_digit(*cursor);
            if (digit < 0) {
                valid = false;
                return 0;
            }
            value = value * 16 + digit;
            ++cursor;
        }
        return value;
    }
    bool string(Text &output) {
        if (!take('"'))
            return false;
        while (valid && *cursor && *cursor != '"') {
            unsigned char value = (unsigned char)*cursor++;
            if (value < 32) {
                valid = false;
                break;
            }
            if (value != '\\') {
                output.letter((char)value);
                continue;
            }
            char escaped = *cursor++;
            if (!escaped) {
                valid = false;
                break;
            }
            switch (escaped) {
            case '"':
            case '\\':
            case '/':
                output.letter(escaped);
                break;
            case 'b':
                output.letter('\b');
                break;
            case 'f':
                output.letter('\f');
                break;
            case 'n':
                output.letter('\n');
                break;
            case 'r':
                output.letter('\r');
                break;
            case 't':
                output.letter('\t');
                break;
            case 'u': {
                uint32_t code = codepoint();
                if (code >= 0xd800 && code <= 0xdbff) {
                    if (cursor[0] != '\\' || cursor[1] != 'u') {
                        valid = false;
                        break;
                    }
                    cursor += 2;
                    uint32_t low = codepoint();
                    if (low < 0xdc00 || low > 0xdfff) {
                        valid = false;
                        break;
                    }
                    code = 0x10000 + ((code - 0xd800) << 10) + low - 0xdc00;
                } else if (code >= 0xdc00 && code <= 0xdfff) {
                    valid = false;
                    break;
                }
                if (!code) {
                    valid = false;
                    break;
                }
                if (valid)
                    utf8(output, code);
                break;
            }
            default:
                valid = false;
            }
        }
        return valid && take('"');
    }
    bool number(uint64_t &value) {
        space();
        if (!isdigit((unsigned char)*cursor)) {
            valid = false;
            return false;
        }
        errno = 0;
        char *end = NULL;
        unsigned long long parsed = strtoull(cursor, &end, 10);
        if (errno || end == cursor) {
            valid = false;
            return false;
        }
        cursor = end;
        value = parsed;
        return true;
    }
    bool skip(int depth = 0) {
        if (depth > 32) {
            valid = false;
            return false;
        }
        space();
        if (*cursor == '"') {
            Text value;
            return string(value);
        }
        if (next('{')) {
            if (next('}'))
                return true;
            do {
                Text key;
                if (!string(key) || !take(':') || !skip(depth + 1))
                    return false;
            } while (next(','));
            return take('}');
        }
        if (next('[')) {
            if (next(']'))
                return true;
            do {
                if (!skip(depth + 1))
                    return false;
            } while (next(','));
            return take(']');
        }
        if (!strncmp(cursor, "true", 4)) {
            cursor += 4;
            return true;
        }
        if (!strncmp(cursor, "false", 5)) {
            cursor += 5;
            return true;
        }
        if (!strncmp(cursor, "null", 4)) {
            cursor += 4;
            return true;
        }
        char *end = NULL;
        errno = 0;
        double value = strtod(cursor, &end);
        if (end == cursor || errno || !isfinite(value)) {
            valid = false;
            return false;
        }
        cursor = end;
        return true;
    }
};
struct Document {
    Text title, url, journal;
};
struct Answer {
    uint64_t count;
    Text error;
    Document documents[50];
    size_t size;
    Answer() : count(0), size(0) {
    }
};
bool read_document(Json &json, Document &document) {
    if (!json.take('{'))
        return false;
    if (json.next('}'))
        return true;
    do {
        Text key;
        if (!json.string(key) || !json.take(':'))
            return false;
        if (!strcmp(key.data, "title")) {
            if (!json.string(document.title))
                return false;
        } else if (!strcmp(key.data, "url")) {
            if (!json.string(document.url))
                return false;
        } else if (!strcmp(key.data, "journal")) {
            if (!json.string(document.journal))
                return false;
        } else if (!json.skip())
            return false;
    } while (json.next(','));
    return json.take('}');
}
bool read_answer(const char *text, Answer &answer) {
    Json json(text);
    if (!json.take('{'))
        return false;
    bool has_count = false;
    if (json.next('}'))
        return false;
    do {
        Text key;
        if (!json.string(key) || !json.take(':'))
            return false;
        if (!strcmp(key.data, "count")) {
            has_count = json.number(answer.count);
            if (!has_count)
                return false;
        } else if (!strcmp(key.data, "error")) {
            if (!json.string(answer.error))
                return false;
        } else if (!strcmp(key.data, "documents")) {
            if (!json.take('['))
                return false;
            if (!json.next(']')) {
                do {
                    if (answer.size == 50 || !read_document(json, answer.documents[answer.size]))
                        return false;
                    ++answer.size;
                } while (json.next(','));
                if (!json.take(']'))
                    return false;
            }
        } else if (!json.skip())
            return false;
    } while (json.next(','));
    if (!json.take('}'))
        return false;
    json.space();
    return json.valid && !*json.cursor && (has_count || answer.error.size);
}
int search(const char *index, const char *query, uint64_t page, Answer &answer) {
    FILE *input = tmpfile(), *output = tmpfile();
    if (!input || !output) {
        if (input)
            fclose(input);
        if (output)
            fclose(output);
        return 500;
    }
    fprintf(input, "%s\n", query);
    fflush(input);
    rewind(input);
    fflush(NULL);
    pid_t child = fork();
    if (child < 0) {
        fclose(input);
        fclose(output);
        return 500;
    }
    if (!child) {
        alarm(30);
        dup2(fileno(input), STDIN_FILENO);
        dup2(fileno(output), STDOUT_FILENO);
        clearerr(stdin);
        clearerr(stdout);
        char offset[32];
        snprintf(offset, sizeof offset, "%llu", (unsigned long long)((page - 1) * 50));
        char *args[] = {(char *)"search", (char *)index,     (char *)"--json", (char *)"--offset",
                        offset,           (char *)"--limit", (char *)"50"};
        int status = ir_search_main(7, args);
        fflush(stdout);
        _exit(status);
    }
    int status = 0;
    while (waitpid(child, &status, 0) < 0) {
        if (errno != EINTR) {
            fclose(input);
            fclose(output);
            return 500;
        }
    }
    rewind(output);
    Text body;
    char buffer[4096];
    size_t count;
    while ((count = fread(buffer, 1, sizeof buffer, output)) > 0) {
        body.add(buffer, count);
        if (body.size > 4 * 1024 * 1024)
            break;
    }
    fclose(input);
    fclose(output);
    if (!read_answer(body.data, answer))
        return 500;
    if (answer.error.size)
        return 400;
    if (!WIFEXITED(status) || WEXITSTATUS(status))
        return 500;
    return page > (answer.count ? (answer.count - 1) / 50 + 1 : 1) ? 404 : 200;
}
void page_link(Text &output, const char *query, uint64_t page, const char *title) {
    output.add("<a href=\"/search?q=");
    Text url;
    encode_url(url, query);
    escape_html(output, url.data);
    output.format("&amp;page=%llu\">", (unsigned long long)page);
    output.add(title);
    output.add("</a>");
}
void render(Text &output, const char *query, uint64_t page, const Answer *answer,
            const char *error) {
    output.add(
        "<!doctype html><html lang=\"ru\"><meta charset=\"utf-8\"><meta name=\"viewport\" "
        "content=\"width=device-width\"><title>Поиск статей</title><style>body{font:17px/1.6 "
        "system-ui;background:#111b2c;color:#edf2ff;margin:0}main{max-width:1000px;margin:50px "
        "auto;padding:20px}input{width:65%;padding:12px;font:inherit}button{padding:12px;font:"
        "inherit}a{color:#9ccaff}article{border-top:1px solid #40506a;padding:15px "
        "0}small{color:#bdc8da}nav{display:flex;gap:30px}.error{color:#ffb7b7}</"
        "style><main><small>МАИ · Информационный поиск</small><h1>Поиск научных статей</h1><p>И: "
        "пробел или &amp;&amp; · ИЛИ: || · НЕ: ! · Скобки: ( )</p><form action=\"/search\"><input "
        "name=\"q\" maxlength=\"500\" aria-label=\"Поисковый запрос\" value=\"");
    escape_html(output, query);
    output.add("\" required><button>Найти</button></form>");
    if (error && *error) {
        output.add("<p class=\"error\" role=\"alert\">");
        escape_html(output, error);
        output.add("</p>");
    }
    if (answer && (!error || !*error)) {
        output.format("<p>Найдено: %llu</p>", (unsigned long long)answer->count);
        for (size_t i = 0; i < answer->size; ++i) {
            const Document &document = answer->documents[i];
            if (strncmp(document.url.data, "https://", 8) &&
                strncmp(document.url.data, "http://", 7))
                continue;
            output.format("<article><small>%llu · ", (unsigned long long)((page - 1) * 50 + i + 1));
            escape_html(output, document.journal.data);
            output.add("</small><h2><a href=\"");
            escape_html(output, document.url.data);
            output.add("\" rel=\"noopener noreferrer\">");
            escape_html(output, document.title.data);
            output.add("</a></h2><small>");
            escape_html(output, document.url.data);
            output.add("</small></article>");
        }
        if (!answer->count)
            output.add("<p>Совпадений нет.</p>");
        output.add("<nav>");
        if (page > 1)
            page_link(output, query, page - 1, "Назад");
        if (page <= UINT64_MAX / 50 && page * 50 < answer->count)
            page_link(output, query, page + 1, "Следующие 50");
        output.add("</nav>");
    }
    output.add("</main></html>");
}
bool read_parameters(const char *query_string, Text &query, uint64_t &page) {
    page = 1;
    const char *cursor = query_string;
    bool query_seen = false, page_seen = false;
    while (*cursor) {
        const char *end = strchr(cursor, '&');
        if (!end)
            end = cursor + strlen(cursor);
        const char *equal = (const char *)memchr(cursor, '=', (size_t)(end - cursor));
        const char *value = equal ? equal + 1 : end;
        Text key, text;
        if (!decode_url(cursor, (size_t)((equal ? equal : end) - cursor), key) ||
            !decode_url(value, (size_t)(end - value), text))
            return false;
        if (!strcmp(key.data, "q") && !query_seen) {
            query.add(text.data);
            query_seen = true;
        } else if (!strcmp(key.data, "page") && !page_seen) {
            if (!text.size || text.data[0] == '-')
                return false;
            char *number_end = NULL;
            errno = 0;
            unsigned long long number = strtoull(text.data, &number_end, 10);
            if (errno || number_end == text.data || *number_end || !number ||
                number > UINT64_MAX / 50)
                return false;
            page = number;
            page_seen = true;
        }
        cursor = *end ? end + 1 : end;
    }
    size_t characters = 0;
    for (size_t i = 0; i < query.size; ++i)
        if (((unsigned char)query.data[i] & 0xc0) != 0x80)
            ++characters;
    return characters <= 500;
}
bool send_bytes(int socket, const char *data, size_t length) {
    while (length) {
        ssize_t sent = send(socket, data, length, 0);
        if (sent < 0 && errno == EINTR)
            continue;
        if (sent <= 0)
            return false;
        data += sent;
        length -= (size_t)sent;
    }
    return true;
}
void handle_client(int client, const char *index) {
    timeval timeout = {10, 0};
    setsockopt(client, SOL_SOCKET, SO_RCVTIMEO, &timeout, sizeof timeout);
    setsockopt(client, SOL_SOCKET, SO_SNDTIMEO, &timeout, sizeof timeout);
    Text request;
    char buffer[1024];
    bool complete = false;
    while (request.size < 16384) {
        ssize_t got = recv(client, buffer, sizeof buffer, 0);
        if (got < 0 && errno == EINTR)
            continue;
        if (got <= 0)
            break;
        request.add(buffer, (size_t)got);
        if (strstr(request.data, "\r\n\r\n")) {
            complete = true;
            break;
        }
    }
    int status = 200;
    Text query, body;
    uint64_t page = 1;
    Answer answer;
    bool searched = false;
    const char *error = NULL;
    char target[16384] = {0}, method[16] = {0}, version[16] = {0};
    if (!complete || sscanf(request.data, "%15s %16383s %15s", method, target, version) != 3) {
        status = 400;
        error = "Некорректный HTTP-запрос";
    } else if (strcmp(method, "GET")) {
        status = 405;
        error = "Допускается только GET";
    } else {
        char *parameters = strchr(target, '?');
        if (parameters)
            *parameters++ = 0;
        if (strcmp(target, "/") && strcmp(target, "/search")) {
            status = 404;
            error = "Страница не найдена";
        } else if (parameters && !read_parameters(parameters, query, page)) {
            status = 400;
            error = "Некорректный запрос или номер страницы";
        } else if (!strcmp(target, "/search") && query.size) {
            status = search(index, query.data, page, answer);
            searched = true;
            if (status == 400)
                error = answer.error.data;
            else if (status == 404)
                error = "Такой страницы результатов нет";
            else if (status != 200)
                error = "Не удалось прочитать поисковый индекс";
        }
    }
    render(body, query.data, page, searched ? &answer : NULL, error);
    const char *reason = status == 200   ? "OK"
                         : status == 400 ? "Bad Request"
                         : status == 404 ? "Not Found"
                         : status == 405 ? "Method Not Allowed"
                                         : "Internal Server Error";
    Text header;
    header.format("HTTP/1.1 %d %s\r\nContent-Type: text/html; charset=utf-8\r\nContent-Length: "
                  "%zu\r\nConnection: close\r\n\r\n",
                  status, reason, body.size);
    if (send_bytes(client, header.data, header.size))
        send_bytes(client, body.data, body.size);
}
volatile sig_atomic_t stopped = 0;
void stop_server(int) {
    stopped = 1;
}
}

int ir_server_main(int argc, char **argv) {
    const char *host = "127.0.0.1", *index = "lab01_corpus/data/native_index";
    int port = 8000;
    for (int i = 1; i < argc; ++i) {
        if (i + 1 >= argc) {
            fprintf(stderr, "serve: option needs value\n");
            return 2;
        }
        const char *option = argv[i], *value = argv[++i];
        if (!strcmp(option, "--host"))
            host = value;
        else if (!strcmp(option, "--index-dir"))
            index = value;
        else if (!strcmp(option, "--port")) {
            char *end = NULL;
            errno = 0;
            long number = strtol(value, &end, 10);
            if (errno || end == value || *end || number < 1 || number > 65535) {
                fprintf(stderr, "serve: invalid port\n");
                return 2;
            }
            port = (int)number;
        } else {
            fprintf(stderr, "serve: unknown option %s\n", option);
            return 2;
        }
    }
    int server = socket(AF_INET, SOCK_STREAM, 0);
    if (server < 0) {
        perror("server socket");
        return 1;
    }
    int yes = 1;
    setsockopt(server, SOL_SOCKET, SO_REUSEADDR, &yes, sizeof yes);
    sockaddr_in address;
    memset(&address, 0, sizeof address);
    address.sin_family = AF_INET;
    address.sin_port = htons((uint16_t)port);
    if (inet_pton(AF_INET, host, &address.sin_addr) != 1) {
        fprintf(stderr, "serve: use an IPv4 address\n");
        close(server);
        return 2;
    }
    if (bind(server, (sockaddr *)&address, sizeof address) || listen(server, 16)) {
        perror("server listen");
        close(server);
        return 1;
    }
    struct sigaction action;
    memset(&action, 0, sizeof action);
    action.sa_handler = stop_server;
    sigemptyset(&action.sa_mask);
    struct sigaction old_int, old_term;
    sigaction(SIGINT, &action, &old_int);
    sigaction(SIGTERM, &action, &old_term);
    signal(SIGPIPE, SIG_IGN);
    stopped = 0;
    printf("http://%s:%d\n", host, port);
    fflush(stdout);
    while (!stopped) {
        int client = accept(server, NULL, NULL);
        if (client < 0) {
            if (errno == EINTR)
                continue;
            perror("server accept");
            break;
        }
        handle_client(client, index);
        close(client);
    }
    close(server);
    sigaction(SIGINT, &old_int, NULL);
    sigaction(SIGTERM, &old_term, NULL);
    return 0;
}

int ir_server_tests() {
    int errors = 0;
    Text decoded;
    if (!decode_url("dark+matter%20%26%26%20%CE%B1", strlen("dark+matter%20%26%26%20%CE%B1"),
                    decoded) ||
        strcmp(decoded.data, "dark matter && α"))
        ++errors;
    Text bad;
    if (decode_url("%00", 3, bad))
        ++errors;
    Text escaped;
    escape_html(escaped, "<a x=\"&\">'");
    if (strcmp(escaped.data, "&lt;a x=&quot;&amp;&quot;&gt;&#39;"))
        ++errors;
    Text query;
    uint64_t page = 0;
    if (!read_parameters("q=dark+matter&page=2", query, page) ||
        strcmp(query.data, "dark matter") || page != 2)
        ++errors;
    Text invalid;
    if (read_parameters("q=x&page=5bad", invalid, page))
        ++errors;
    Answer answer;
    if (!read_answer(
            "{\"count\":51,\"ids\":[1],\"documents\":[{\"id\":1,\"title\":\"A \\\"title\\\" "
            "\\u03b1 "
            "\\ud83d\\ude00\",\"url\":\"https://example.org/a?x=1&y=2\",\"journal\":\"J\"}]}",
            answer) ||
        answer.count != 51 || answer.size != 1 || !strstr(answer.documents[0].title.data, "α 😀"))
        ++errors;
    Text rendered;
    render(rendered, "<script>", 1, &answer, NULL);
    if (strstr(rendered.data, "<script>") || !strstr(rendered.data, "&lt;script&gt;") ||
        !strstr(rendered.data, "Следующие 50") || !strstr(rendered.data, "x=1&amp;y=2"))
        ++errors;
    Answer malformed;
    if (read_answer("{\"count\":1,\"documents\":[", malformed))
        ++errors;
    fprintf(stderr, "native HTTP helpers: %s (%d failures)\n", errors ? "FAILED" : "OK", errors);
    return errors;
}
