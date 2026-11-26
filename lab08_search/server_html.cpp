#include "server_parts.h"

namespace web {
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
}
