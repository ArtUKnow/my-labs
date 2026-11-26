#include "crawler.h"
#include <ctype.h>
#include <curl/curl.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <math.h>
#include <signal.h>
#include <sqlite3.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/file.h>
#include <sys/stat.h>
#include <time.h>
#include <unistd.h>
#include <zlib.h>
#ifdef __APPLE__
#include <CommonCrypto/CommonDigest.h>
#else
#include <openssl/sha.h>
#endif

namespace {
#include "crawler_database.h"

volatile sig_atomic_t stop_requested = 0;
void request_stop(int) {
    stop_requested = 1;
}
int run_queue(sqlite3 *database, const Sources &sources, const Config &config) {
    long processed = 0, errors = 0;
    bool good = true;
    while (!stop_requested && (!config.max_pages || processed < config.max_pages)) {
        sqlite3_stmt *statement = NULL;
        if (!prepare(database,
                     "SELECT url FROM raw_queue WHERE state='pending' AND next_attempt<=? ORDER BY "
                     "planned_at,url LIMIT 1",
                     &statement))
            return 1;
        sqlite3_bind_int64(statement, 1, time(NULL));
        int status = sqlite3_step(statement);
        char *url = status == SQLITE_ROW
                        ? copy_text((const char *)sqlite3_column_text(statement, 0))
                        : NULL;
        sqlite3_finalize(statement);
        if (status == SQLITE_DONE)
            break;
        if (status != SQLITE_ROW)
            return 1;
        Source *source = sources.find(url);
        if (!source) {
            fprintf(stderr, "crawler: queued URL is absent from current manifest: %s\n", url);
            free(url);
            return 1;
        }
        statement = NULL;
        good = prepare(database, "UPDATE raw_queue SET state='inflight' WHERE url=?", &statement);
        if (good) {
            bind_text(statement, 1, url);
            good = sqlite3_step(statement) == SQLITE_DONE;
        }
        sqlite3_finalize(statement);
        if (!good) {
            free(url);
            return 1;
        }
        Previous previous;
        Response response((size_t)config.max_source_bytes);
        char error[CURL_ERROR_SIZE] = "request failed";
        const char *result = "";
        good = previous_document(database, url, previous);
        if (good && fetch(*source, previous, config, response, error) &&
            save_response(database, *source, previous, response, config, result)) {
            printf("%s\t%s\n", result, url);
        } else {
            ++errors;
            fprintf(stderr, "crawler: %s: %s\n", url, error);
            if (!queue_error(database, url, error, config.retry_delay)) {
                free(url);
                return 1;
            }
        }
        free(url);
        ++processed;
        if (config.delay > 0) {
            struct timespec pause = {(time_t)config.delay,
                                     (long)((config.delay - (time_t)config.delay) * 1000000000)};
            nanosleep(&pause, NULL);
        }
    }
    sqlite3_stmt *statement = NULL;
    if (!prepare(database, "SELECT COUNT(*) FROM raw_queue WHERE state!='done'", &statement))
        return 1;
    int status = sqlite3_step(statement);
    sqlite3_int64 pending = status == SQLITE_ROW ? sqlite3_column_int64(statement, 0) : -1;
    sqlite3_finalize(statement);
    printf("processed: %ld\nerrors: %ld\npending: %lld\n", processed, errors, (long long)pending);
    return errors || pending < 0 || (!config.max_pages && pending > 0) ? 1 : 0;
}
}

int ir_crawler_main(int argc, char **argv) {
    if (argc != 2) {
        fprintf(stderr, "usage: crawl CONFIG.yaml\n");
        return 2;
    }
    Config config;
    if (!read_config(argv[1], config))
        return 2;
    if (curl_global_init(CURL_GLOBAL_DEFAULT) != CURLE_OK)
        return 1;
    Sources sources;
    if (!read_sources(config, sources)) {
        fprintf(stderr, "crawler: cannot read source manifest\n");
        curl_global_cleanup();
        return 1;
    }
    sqlite3 *database = NULL;
    int lock = -1;
    int result = 1;
    if (open_database(config, &database, lock) && plan_queue(database, sources, config)) {
        stop_requested = 0;
        void (*old_int)(int) = signal(SIGINT, request_stop);
        void (*old_term)(int) = signal(SIGTERM, request_stop);
        result = run_queue(database, sources, config);
        signal(SIGINT, old_int);
        signal(SIGTERM, old_term);
    }
    if (database)
        sqlite3_close(database);
    if (lock >= 0) {
        flock(lock, LOCK_UN);
        close(lock);
    }
    curl_global_cleanup();
    return result;
}
