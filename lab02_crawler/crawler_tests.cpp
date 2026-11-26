#include "crawler.h"
#include <arpa/inet.h>
#include <errno.h>
#include <signal.h>
#include <sqlite3.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <unistd.h>
#include <zlib.h>

namespace {
bool write_text(const char *path, const char *text) {
    FILE *file = fopen(path, "wb");
    if (!file)
        return false;
    size_t length = strlen(text);
    bool good = fwrite(text, 1, length, file) == length;
    return fclose(file) == 0 && good;
}
bool copy_file(const char *source, const char *target) {
    FILE *input = fopen(source, "rb"), *output = fopen(target, "wb");
    if (!input || !output) {
        if (input)
            fclose(input);
        if (output)
            fclose(output);
        return false;
    }
    char block[4096];
    size_t size;
    bool good = true;
    while ((size = fread(block, 1, sizeof(block), input)))
        if (fwrite(block, 1, size, output) != size) {
            good = false;
            break;
        }
    good = good && !ferror(input);
    fclose(input);
    good = fclose(output) == 0 && good;
    return good;
}
void serve(int server) {
    for (int request = 0; request < 3; ++request) {
        int client = accept(server, NULL, NULL);
        if (client < 0)
            _exit(20);
        char data[16384];
        size_t size = 0;
        while (size + 1 < sizeof(data)) {
            ssize_t count = read(client, data + size, sizeof(data) - size - 1);
            if (count <= 0)
                _exit(21);
            size += (size_t)count;
            data[size] = 0;
            if (strstr(data, "\r\n\r\n"))
                break;
        }
        const char *response;
        if (request == 0)
            response = "HTTP/1.1 200 OK\r\nContent-Type: application/xml\r\nETag: "
                       "\"v1\"\r\nContent-Length: 3\r\nConnection: close\r\n\r\nabc";
        else if (request == 1) {
            if (!strstr(data, "If-None-Match: \"v1\""))
                _exit(22);
            response = "HTTP/1.1 304 Not Modified\r\nETag: \"v1\"\r\nContent-Length: "
                       "0\r\nConnection: close\r\n\r\n";
        } else
            response = "HTTP/1.1 200 OK\r\nContent-Type: application/xml\r\nETag: "
                       "\"v2\"\r\nContent-Length: 7\r\nConnection: close\r\n\r\nchanged";
        size_t length = strlen(response), sent = 0;
        while (sent < length) {
            ssize_t count = write(client, response + sent, length - sent);
            if (count <= 0)
                _exit(23);
            sent += (size_t)count;
        }
        close(client);
    }
    close(server);
    _exit(0);
}
bool check_body(const char *path, const char *expected, const char *etag) {
    sqlite3 *database = NULL;
    if (sqlite3_open(path, &database) != SQLITE_OK)
        return false;
    sqlite3_stmt *statement = NULL;
    bool good =
        sqlite3_prepare_v2(
            database,
            "SELECT raw_gzip,raw_bytes,sha256,etag,crawled_at,checked_at,source FROM raw_documents",
            -1, &statement, NULL) == SQLITE_OK;
    if (good)
        good = sqlite3_step(statement) == SQLITE_ROW;
    if (good) {
        const void *data = sqlite3_column_blob(statement, 0);
        int size = sqlite3_column_bytes(statement, 0);
        char body[128];
        z_stream stream = {};
        good = inflateInit2(&stream, 31) == Z_OK;
        if (good) {
            stream.next_in = (Bytef *)data;
            stream.avail_in = (uInt)size;
            stream.next_out = (Bytef *)body;
            stream.avail_out = sizeof(body) - 1;
            good = inflate(&stream, Z_FINISH) == Z_STREAM_END;
            if (good) {
                body[stream.total_out] = 0;
                good = !strcmp(body, expected) && stream.total_out == strlen(expected);
            }
            inflateEnd(&stream);
        }
        good = good && sqlite3_column_int64(statement, 1) == (sqlite3_int64)strlen(expected) &&
               !strcmp((const char *)sqlite3_column_text(statement, 3), etag) &&
               sqlite3_column_int64(statement, 4) > 0 && sqlite3_column_int64(statement, 5) > 0 &&
               !strcmp((const char *)sqlite3_column_text(statement, 6), "Test journal");
        if (!strcmp(expected, "abc"))
            good =
                good && !strcmp((const char *)sqlite3_column_text(statement, 2),
                                "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    }
    sqlite3_finalize(statement);
    sqlite3_close(database);
    return good;
}
}

int ir_crawler_tests() {
    char directory[] = "/tmp/mai-crawler-test-XXXXXX";
    if (!mkdtemp(directory))
        return 1;
    char sql_folder[1024], source_folder[1024];
    snprintf(sql_folder, sizeof(sql_folder), "%s/sql", directory);
    mkdir(sql_folder, 0700);
    snprintf(source_folder, sizeof(source_folder), "%s", __FILE__);
    char *slash = strrchr(source_folder, '/');
    if (slash)
        *slash = 0;
    else
        strcpy(source_folder, ".");
    const char *sql_names[] = {"schema.sql", "plan_recrawl.sql", "save_document.sql"};
    bool good = true;
    for (size_t i = 0; i < 3; ++i) {
        char source[2048], target[2048];
        snprintf(source, sizeof(source), "%s/sql/%s", source_folder, sql_names[i]);
        snprintf(target, sizeof(target), "%s/%s", sql_folder, sql_names[i]);
        good = good && copy_file(source, target);
    }
    int server = socket(AF_INET, SOCK_STREAM, 0);
    sockaddr_in address = {};
    address.sin_family = AF_INET;
    address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    good = good && server >= 0 && bind(server, (sockaddr *)&address, sizeof(address)) == 0 &&
           listen(server, 4) == 0;
    socklen_t address_size = sizeof(address);
    if (good)
        good = getsockname(server, (sockaddr *)&address, &address_size) == 0;
    pid_t child = -1;
    if (good) {
        child = fork();
        if (child == 0)
            serve(server);
        if (child < 0)
            good = false;
    }
    char manifest[1024], config[1024], database[1024], body[4096];
    snprintf(manifest, sizeof(manifest), "%s/manifest.tsv", directory);
    snprintf(config, sizeof(config), "%s/config.yaml", directory);
    snprintf(database, sizeof(database), "%s/raw.sqlite", directory);
    snprintf(body, sizeof(body),
             "record_id\turl\tjournal\tsource_format\txml_url\tpdf_url\n1\thttp://127.0.0.1:%u/"
             "record\tTest journal\txml\thttp://127.0.0.1:%u/article.xml#fragment\t\n",
             ntohs(address.sin_port), ntohs(address.sin_port));
    good = good && write_text(manifest, body);
    snprintf(body, sizeof(body),
             "db:\n  path: raw.sqlite\nlogic:\n  workers: 1\n  delay: 0\n  timeout: 3\n  "
             "max_pages: 1\n  recrawl_after: 0\n  retry_delay: 1\ncorpus:\n  manifest: "
             "manifest.tsv\n  include_landing: false\n");
    good = good && write_text(config, body);
    char command[] = "crawl";
    char *arguments[] = {command, config};
    if (good)
        good = ir_crawler_main(2, arguments) == 0 && check_body(database, "abc", "\"v1\"");
    if (good)
        good = ir_crawler_main(2, arguments) == 0 && check_body(database, "abc", "\"v1\"");
    if (good)
        good = ir_crawler_main(2, arguments) == 0 && check_body(database, "changed", "\"v2\"");
    if (server >= 0)
        close(server);
    if (child > 0) {
        if (!good)
            kill(child, SIGTERM);
        int status = 0;
        waitpid(child, &status, 0);
        good = good && WIFEXITED(status) && WEXITSTATUS(status) == 0;
    }
    const char *files[] = {"manifest.tsv",   "config.yaml",    "raw.sqlite",
                           "raw.sqlite-wal", "raw.sqlite-shm", "raw.sqlite.lock"};
    for (size_t i = 0; i < 6; ++i) {
        char path[2048];
        snprintf(path, sizeof(path), "%s/%s", directory, files[i]);
        unlink(path);
    }
    for (size_t i = 0; i < 3; ++i) {
        char path[2048];
        snprintf(path, sizeof(path), "%s/%s", sql_folder, sql_names[i]);
        unlink(path);
    }
    rmdir(sql_folder);
    rmdir(directory);
    fprintf(stderr, "native crawler HTTP200/304/changed, SQLite, gzip, SHA256: %s\n",
            good ? "OK" : "FAILED");
    return good ? 0 : 1;
}
