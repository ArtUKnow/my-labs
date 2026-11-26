#ifndef IR_CRAWLER_DATABASE_H
#define IR_CRAWLER_DATABASE_H
#include "crawler_http.h"
bool execute(sqlite3 *database, const char *sql) {
    char *error = NULL;
    int status = sqlite3_exec(database, sql, NULL, NULL, &error);
    if (status != SQLITE_OK)
        fprintf(stderr, "crawler database: %s\n", error ? error : sqlite3_errmsg(database));
    sqlite3_free(error);
    return status == SQLITE_OK;
}
bool prepare(sqlite3 *database, const char *sql, sqlite3_stmt **statement) {
    if (sqlite3_prepare_v2(database, sql, -1, statement, NULL) != SQLITE_OK) {
        fprintf(stderr, "crawler database: %s\n", sqlite3_errmsg(database));
        return false;
    }
    return true;
}
void bind_text(sqlite3_stmt *statement, int column, const char *text) {
    sqlite3_bind_text(statement, column, text ? text : "", -1, SQLITE_TRANSIENT);
}
char *read_file(const char *path) {
    FILE *file = fopen(path, "rb");
    if (!file)
        return NULL;
    Text content;
    char block[4096];
    size_t size;
    while ((size = fread(block, 1, sizeof(block), file)))
        if (!content.add(block, size)) {
            fclose(file);
            return NULL;
        }
    bool good = !ferror(file);
    fclose(file);
    return good ? copy_text(content.data ? content.data : "") : NULL;
}
char *read_sql(const Config &config, const char *name) {
    char *folder = folder_of(config.schema);
    char *path = join_path(folder, name);
    char *sql = read_file(path);
    if (!sql)
        perror(path);
    free(folder);
    free(path);
    return sql;
}
bool open_database(const Config &config, sqlite3 **database, int &lock) {
    if (!make_folders(config.database))
        return false;
    char *lock_path = (char *)malloc(strlen(config.database) + 6);
    if (!lock_path)
        return false;
    snprintf(lock_path, strlen(config.database) + 6, "%s.lock", config.database);
    lock = open(lock_path, O_RDWR | O_CREAT, 0600);
    free(lock_path);
    if (lock < 0 || flock(lock, LOCK_EX | LOCK_NB)) {
        fprintf(stderr, "crawler: another process owns the database\n");
        if (lock >= 0)
            close(lock);
        lock = -1;
        return false;
    }
    if (sqlite3_open(config.database, database) != SQLITE_OK) {
        fprintf(stderr, "crawler database: %s\n", sqlite3_errmsg(*database));
        return false;
    }
    sqlite3_busy_timeout(*database, 30000);
    char *schema = read_file(config.schema);
    if (!schema) {
        perror(config.schema);
        return false;
    }
    bool good = execute(*database, schema);
    free(schema);
    return good &&
           execute(*database, "UPDATE raw_queue SET state='pending' WHERE state='inflight'");
}
bool plan_queue(sqlite3 *database, const Sources &sources, const Config &config) {
    sqlite3_stmt *old = NULL, *insert = NULL;
    char *sql = read_sql(config, "plan_recrawl.sql");
    bool good = sql &&
                prepare(database, "SELECT checked_at FROM raw_documents WHERE url=?", &old) &&
                prepare(database, sql, &insert);
    free(sql);
    if (good)
        good = execute(database, "BEGIN IMMEDIATE");
    time_t now = time(NULL);
    for (size_t i = 0; good && i < sources.size; ++i) {
        Source *source = sources.items[i];
        sqlite3_reset(old);
        bind_text(old, 1, source->url);
        int status = sqlite3_step(old);
        if (status == SQLITE_ROW && sqlite3_column_int64(old, 0) > now - config.recrawl_after)
            continue;
        if (status != SQLITE_ROW && status != SQLITE_DONE) {
            good = false;
            break;
        }
        sqlite3_reset(insert);
        bind_text(insert, 1, source->url);
        bind_text(insert, 2, "{}");
        sqlite3_bind_int64(insert, 3, now);
        good = sqlite3_step(insert) == SQLITE_DONE;
    }
    if (good)
        good = execute(database, "COMMIT");
    else
        execute(database, "ROLLBACK");
    sqlite3_finalize(old);
    sqlite3_finalize(insert);
    return good;
}
bool previous_document(sqlite3 *database, const char *url, Previous &previous) {
    sqlite3_stmt *statement = NULL;
    if (!prepare(database, "SELECT sha256,etag,last_modified FROM raw_documents WHERE url=?",
                 &statement))
        return false;
    bind_text(statement, 1, url);
    int status = sqlite3_step(statement);
    if (status == SQLITE_ROW) {
        previous.exists = true;
        previous.hash = copy_text((const char *)sqlite3_column_text(statement, 0));
        previous.etag = copy_text((const char *)sqlite3_column_text(statement, 1));
        previous.modified = copy_text((const char *)sqlite3_column_text(statement, 2));
    }
    sqlite3_finalize(statement);
    return status == SQLITE_ROW || status == SQLITE_DONE;
}
bool save_response(sqlite3 *database, const Source &source, const Previous &previous,
                   const Response &response, const Config &config, const char *&result) {
    bool unchanged = response.status == 304;
    char hash[65] = "";
    if (unchanged && !previous.exists) {
        fprintf(stderr, "crawler: 304 without stored body\n");
        return false;
    }
    if (!unchanged) {
        sha256(response.body.data, response.body.size, hash);
        unchanged = previous.exists && !strcmp(hash, previous.hash);
    }
    sqlite3_stmt *statement = NULL;
    time_t now = time(NULL);
    bool good = true;
    if (unchanged) {
        good = prepare(database, "UPDATE raw_documents SET checked_at=? WHERE url=?", &statement);
        if (good) {
            sqlite3_bind_int64(statement, 1, now);
            bind_text(statement, 2, source.url);
            good = sqlite3_step(statement) == SQLITE_DONE;
        }
        result = "unchanged";
    } else {
        Text compressed;
        if (!gzip_body(response.body, compressed))
            return false;
        char *sql = read_sql(config, "save_document.sql");
        good = sql && prepare(database, sql, &statement);
        free(sql);
        if (good) {
            bind_text(statement, 1, source.url);
            bind_text(statement, 2, source.record_id);
            bind_text(statement, 3, source.record_url);
            bind_text(statement, 4, source.journal);
            bind_text(statement, 5, source.format);
            bind_text(statement, 6, response.type ? response.type : "application/octet-stream");
            sqlite3_bind_blob(statement, 7, compressed.data, (int)compressed.size,
                              SQLITE_TRANSIENT);
            sqlite3_bind_int64(statement, 8, (sqlite3_int64)response.body.size);
            bind_text(statement, 9, hash);
            sqlite3_bind_int64(statement, 10, now);
            sqlite3_bind_int64(statement, 11, now);
            bind_text(statement, 12, response.etag);
            bind_text(statement, 13, response.modified);
            good = sqlite3_step(statement) == SQLITE_DONE;
        }
        result = previous.exists ? "updated" : "downloaded";
    }
    sqlite3_finalize(statement);
    if (!good)
        return false;
    statement = NULL;
    good = prepare(database, "UPDATE raw_queue SET state='done',last_error=NULL WHERE url=?",
                   &statement);
    if (good) {
        bind_text(statement, 1, source.url);
        good = sqlite3_step(statement) == SQLITE_DONE;
    }
    sqlite3_finalize(statement);
    return good;
}
bool queue_error(sqlite3 *database, const char *url, const char *error, long delay) {
    sqlite3_stmt *statement = NULL;
    bool good =
        prepare(database,
                "UPDATE raw_queue SET "
                "state='pending',attempts=attempts+1,next_attempt=?,last_error=? WHERE url=?",
                &statement);
    if (good) {
        sqlite3_bind_int64(statement, 1, time(NULL) + delay);
        bind_text(statement, 2, error);
        bind_text(statement, 3, url);
        good = sqlite3_step(statement) == SQLITE_DONE;
    }
    sqlite3_finalize(statement);
    return good;
}

#endif
