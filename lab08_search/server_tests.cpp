#include "server.h"
#include "server_parts.h"

using namespace web;

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
