#ifndef IR_CRAWLER_HTTP_H
#define IR_CRAWLER_HTTP_H
#include "crawler_sources.h"
struct Previous {
    bool exists;
    char *hash, *etag, *modified;
    Previous() : exists(false), hash(NULL), etag(NULL), modified(NULL) {
    }
    ~Previous() {
        free(hash);
        free(etag);
        free(modified);
    }
};
struct Response {
    Text body;
    char *type, *etag, *modified;
    size_t limit;
    long status;
    bool too_large;
    Response(size_t max)
        : type(NULL), etag(NULL), modified(NULL), limit(max), status(0), too_large(false) {
    }
    ~Response() {
        free(type);
        free(etag);
        free(modified);
    }
};
size_t receive_body(char *data, size_t size, size_t count, void *state) {
    Response *response = (Response *)state;
    if (size && count > SIZE_MAX / size)
        return 0;
    size_t length = size * count;
    if (length > response->limit - response->body.size) {
        response->too_large = true;
        return 0;
    }
    return response->body.add(data, length) ? length : 0;
}
size_t receive_header(char *data, size_t size, size_t count, void *state) {
    Response *response = (Response *)state;
    size_t length = size * count;
    char *line = (char *)malloc(length + 1);
    if (!line)
        return 0;
    memcpy(line, data, length);
    line[length] = 0;
    char *colon = strchr(line, ':');
    if (colon) {
        *colon = 0;
        char *value = trim(colon + 1);
        if (!strcasecmp(line, "ETag"))
            replace_text(response->etag, value);
        else if (!strcasecmp(line, "Last-Modified"))
            replace_text(response->modified, value);
        else if (!strcasecmp(line, "Content-Type"))
            replace_text(response->type, value);
    }
    free(line);
    return length;
}
bool fetch(const Source &source, const Previous &previous, const Config &config, Response &response,
           char *error) {
    CURL *curl = curl_easy_init();
    if (!curl)
        return false;
    struct curl_slist *headers = NULL;
    Text etag, modified;
    if (previous.etag && *previous.etag) {
        etag.add("If-None-Match: ", 15);
        etag.add(previous.etag, strlen(previous.etag));
        headers = curl_slist_append(headers, etag.data);
    }
    if (previous.modified && *previous.modified) {
        modified.add("If-Modified-Since: ", 19);
        modified.add(previous.modified, strlen(previous.modified));
        headers = curl_slist_append(headers, modified.data);
    }
    curl_easy_setopt(curl, CURLOPT_URL, source.url);
    curl_easy_setopt(curl, CURLOPT_HTTPHEADER, headers);
    curl_easy_setopt(curl, CURLOPT_USERAGENT, "MAI-IR-coursework/3.0");
    curl_easy_setopt(curl, CURLOPT_FOLLOWLOCATION, 1L);
    curl_easy_setopt(curl, CURLOPT_TIMEOUT, config.timeout);
    curl_easy_setopt(curl, CURLOPT_NOSIGNAL, 1L);
    curl_easy_setopt(curl, CURLOPT_WRITEFUNCTION, receive_body);
    curl_easy_setopt(curl, CURLOPT_WRITEDATA, &response);
    curl_easy_setopt(curl, CURLOPT_HEADERFUNCTION, receive_header);
    curl_easy_setopt(curl, CURLOPT_HEADERDATA, &response);
    curl_easy_setopt(curl, CURLOPT_ERRORBUFFER, error);
    CURLcode status = curl_easy_perform(curl);
    curl_easy_getinfo(curl, CURLINFO_RESPONSE_CODE, &response.status);
    curl_slist_free_all(headers);
    curl_easy_cleanup(curl);
    if (status != CURLE_OK) {
        if (response.too_large)
            snprintf(error, CURL_ERROR_SIZE, "source exceeds max_source_bytes");
        return false;
    }
    if (response.status != 304 && (response.status < 200 || response.status >= 300)) {
        snprintf(error, CURL_ERROR_SIZE, "HTTP %ld", response.status);
        return false;
    }
    return true;
}
void sha256(const char *data, size_t size, char result[65]) {
    unsigned char digest[32];
#ifdef __APPLE__
    CC_SHA256(data, (CC_LONG)size, digest);
#else
    SHA256((const unsigned char *)data, size, digest);
#endif
    static const char digits[] = "0123456789abcdef";
    for (size_t i = 0; i < 32; ++i) {
        result[2 * i] = digits[digest[i] >> 4];
        result[2 * i + 1] = digits[digest[i] & 15];
    }
    result[64] = 0;
}
bool gzip_body(const Text &body, Text &compressed) {
    z_stream stream = {};
    if (deflateInit2(&stream, 6, Z_DEFLATED, 31, 8, Z_DEFAULT_STRATEGY) != Z_OK)
        return false;
    stream.next_in = (Bytef *)body.data;
    stream.avail_in = (uInt)body.size;
    char block[32768];
    int status;
    do {
        stream.next_out = (Bytef *)block;
        stream.avail_out = sizeof(block);
        status = deflate(&stream, Z_FINISH);
        if (status != Z_OK && status != Z_STREAM_END) {
            deflateEnd(&stream);
            return false;
        }
        if (!compressed.add(block, sizeof(block) - stream.avail_out)) {
            deflateEnd(&stream);
            return false;
        }
    } while (status != Z_STREAM_END);
    deflateEnd(&stream);
    return true;
}

#endif
