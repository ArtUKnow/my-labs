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

#include "server_parts.h"
#include "dashboard.h"

namespace web {
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
    bool custom_response = false;
    const char *content_type = "text/html; charset=utf-8";
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
        if (dashboard_route(target, parameters, body, content_type, status)) {
            custom_response = true;
        } else if (strcmp(target, "/search")) {
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
    if (!custom_response)
        render(body, query.data, page, searched ? &answer : NULL, error);
    const char *reason = status == 200   ? "OK"
                         : status == 400 ? "Bad Request"
                         : status == 404 ? "Not Found"
                         : status == 405 ? "Method Not Allowed"
                                         : "Internal Server Error";
    Text header;
    header.format("HTTP/1.1 %d %s\r\nContent-Type: %s\r\nContent-Length: "
                  "%zu\r\nConnection: close\r\n\r\n",
                  status, reason, content_type, body.size);
    if (send_bytes(client, header.data, header.size))
        send_bytes(client, body.data, body.size);
}
volatile sig_atomic_t stopped = 0;
void stop_server(int) {
    stopped = 1;
}
}

using namespace web;

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
