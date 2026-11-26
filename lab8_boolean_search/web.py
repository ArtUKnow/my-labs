"""Small UTF-8 web search service over the 30,000-document Boolean index."""

import argparse
import html
from http.server import BaseHTTPRequestHandler, HTTPServer
import time
from urllib.parse import parse_qs, urlencode, urlsplit

from boolean_search import BooleanSearch, QuerySyntaxError
from catalog import DATA_DIR, DocumentCatalog, SOURCE_LABELS


PAGE_SIZE = 50
EXAMPLES = [
    "(марс || mars) (ракета || rocket)",
    "(спутник || satellite) !луна",
    "(asteroid || астероид) (orbit || орбита)",
]

STYLE = """
:root{color-scheme:light;--ink:#142039;--muted:#607087;--line:#d9e1e8;--paper:#f6f8fa;--accent:#0d7168}
*{box-sizing:border-box}body{margin:0;background:var(--paper);color:var(--ink);font:16px/1.5 -apple-system,BlinkMacSystemFont,"Segoe UI",Arial,sans-serif}
a{color:inherit}a:hover{color:var(--accent)}.wrap{max-width:980px;margin:auto;padding:0 24px}
.top{background:#10243b;color:#f7fbff;padding:18px 0 24px;border-bottom:5px solid #31b5a5}
.topline{display:flex;align-items:center;justify-content:space-between;gap:16px}.brand{font-weight:800;letter-spacing:.02em;text-decoration:none;font-size:20px}
.count{color:#b8c9d6;font-size:14px}.hero{padding:56px 0 30px}.eyebrow{color:#73ddcf;text-transform:uppercase;letter-spacing:.14em;font-size:12px;font-weight:800}
h1{font-size:clamp(30px,5vw,52px);line-height:1.08;letter-spacing:-.035em;margin:12px 0 15px;max-width:700px}
.lead{color:#c9d8e4;max-width:670px;margin:0 0 28px;font-size:17px}.search{display:flex;gap:10px;max-width:850px}
.search input{min-width:0;flex:1;border:2px solid transparent;border-radius:10px;padding:15px 17px;font:inherit;background:white;color:var(--ink)}
.search input:focus{outline:none;border-color:#5ed8c8}.search button{border:0;border-radius:10px;background:#31b5a5;color:#082a2a;font:inherit;font-weight:800;padding:0 25px;cursor:pointer}
.sr-only{position:absolute;width:1px;height:1px;padding:0;margin:-1px;overflow:hidden;clip:rect(0,0,0,0);white-space:nowrap;border:0}
.search button:hover{background:#66d9cb}.examples{display:flex;gap:8px;flex-wrap:wrap;margin-top:16px}.examples a{border:1px solid #426077;border-radius:999px;color:#d7e8ed;padding:5px 11px;text-decoration:none;font-size:13px}
.examples a:hover{border-color:#31b5a5;color:white}.main{padding-top:32px;padding-bottom:60px}.summary{display:flex;justify-content:space-between;gap:20px;align-items:end;border-bottom:1px solid var(--line);padding-bottom:17px;margin-bottom:8px}
h2{font-size:22px;margin:0;letter-spacing:-.02em}.sub{color:var(--muted);font-size:14px;margin:6px 0 0}.error{background:#fff1ee;border:1px solid #e9a59b;color:#8b3026;border-radius:10px;padding:15px 18px;margin:20px 0}
.result{padding:23px 0;border-bottom:1px solid var(--line)}.result .meta{color:var(--accent);font-size:12px;font-weight:800;text-transform:uppercase;letter-spacing:.06em}
.result h3{font-size:20px;line-height:1.3;margin:6px 0}.result h3 a{text-decoration:none}.result h3 a:hover{text-decoration:underline}.result p{color:#42526a;margin:0 0 8px;max-width:800px}
.result .url{font-size:13px;color:#687b8d;overflow-wrap:anywhere}.result .author{font-size:13px;color:#718094;margin-top:8px}
.pagination{display:flex;gap:10px;align-items:center;margin-top:28px}.pagination a{border:1px solid var(--line);border-radius:9px;padding:10px 15px;background:white;text-decoration:none;font-weight:700}
.pagination a:hover{border-color:var(--accent)}.pagination span{color:var(--muted);font-size:14px}.intro{background:white;border:1px solid var(--line);border-radius:14px;padding:25px;margin-top:20px}
.intro p{color:#4f6073;max-width:740px}.sources{display:flex;gap:9px;flex-wrap:wrap;margin-top:18px}.sources span{background:#edf5f3;color:#236158;border-radius:999px;padding:7px 12px;font-size:13px;font-weight:700}
.footer{color:#77879a;font-size:13px;border-top:1px solid var(--line);padding:20px 0 35px}
@media(max-width:620px){.wrap{padding:0 17px}.hero{padding:38px 0 24px}.search{flex-direction:column}.search button{padding:13px}.count{display:none}.summary{display:block}.result{padding:19px 0}}
"""


def escape(value):
    return html.escape(str(value), quote=True)


def search_url(query, page=1):
    return "/search?" + urlencode({"q": query, "page": page})


def render_page(query, catalog, ids=None, elapsed_ms=None, page=1, error=None):
    count = len(catalog.records)
    title = "Поиск по корпусу" if not query else "Результаты поиска"
    examples = "".join('<a href="%s">%s</a>' % (escape(search_url(example)), escape(example)) for example in EXAMPLES)
    parts = [
        "<!doctype html><html lang=\"ru\"><head><meta charset=\"utf-8\"><meta name=\"viewport\" content=\"width=device-width, initial-scale=1\">",
        "<title>%s · MAI Search</title><style>%s</style></head><body>" % (escape(title), STYLE),
        '<header class="top"><div class="wrap"><div class="topline"><a class="brand" href="/">MAI / SEARCH</a><span class="count">%s документов · 5 коллекций</span></div>' % format(count, ",").replace(",", " "),
        '<div class="hero"><div class="eyebrow">Лабораторная работа 8 · Булев поиск</div><h1>Поиск по науке и космосу</h1>',
        '<p class="lead">Статьи, научные аннотации и вопросы сообщества в одном индексе. Сочетайте слова с И, ИЛИ, НЕ и скобками.</p>',
        '<form class="search" action="/search" method="get"><label for="q" class="sr-only">Поисковый запрос</label><input id="q" name="q" value="%s" placeholder="Например: (марс || mars) ракета" required autofocus><button type="submit">Найти →</button></form>' % escape(query),
        '<div class="examples">%s</div></div></div></header><main class="wrap main">' % examples,
    ]
    if error:
        parts.append('<div class="error" role="alert">%s</div>' % escape(error))
    elif ids is None:
        labels = "".join("<span>%s · %s</span>" % (escape(SOURCE_LABELS.get(source, source)), format(number, ",").replace(",", " ")) for source, number in sorted(catalog.source_counts.items()))
        parts.append('<section class="intro"><h2>Один запрос — пять коллекций</h2><p>Пробел и && означают И; || — ИЛИ; ! исключает документы. Скобки задают порядок. Результаты содержат заголовок и ссылку на оригинал.</p><div class="sources">%s</div></section>' % labels)
    else:
        total = len(ids)
        first = (page - 1) * PAGE_SIZE
        last = min(first + PAGE_SIZE, total)
        parts.append('<div class="summary"><div><h2>Найдено: %s</h2><p class="sub">Запрос: %s</p></div><p class="sub">%.2f мс · результаты %s–%s</p></div>' % (format(total, ",").replace(",", " "), escape(query), elapsed_ms, first + 1 if total else 0, last))
        for rank, doc_num in enumerate(ids[first:last], first + 1):
            record = catalog.get(doc_num)
            url = escape(record["url"])
            parts.append('<article class="result"><div class="meta">%s · %s</div><h3><a href="%s" target="_blank" rel="noopener noreferrer">%s</a></h3><p>%s</p><div class="url">%s</div>' % (rank, escape(record["source_label"]), url, escape(record["title"]), escape(catalog.snippet(doc_num)), url))
            if record["authors"]:
                parts.append('<div class="author">Автор: %s</div>' % escape(record["authors"][:150]))
            parts.append("</article>")
        if not total:
            parts.append('<section class="intro"><h2>Совпадений нет</h2><p>Попробуйте другое слово или объедините варианты через ||.</p></section>')
        if total > PAGE_SIZE:
            parts.append('<nav class="pagination" aria-label="Страницы результатов">')
            if page > 1:
                parts.append('<a href="%s">← Назад</a>' % escape(search_url(query, page - 1)))
            parts.append('<span>Страница %d из %d</span>' % (page, (total + PAGE_SIZE - 1) // PAGE_SIZE))
            if last < total:
                parts.append('<a href="%s">Следующие 50 →</a>' % escape(search_url(query, page + 1)))
            parts.append("</nav>")
    parts.append('</main><footer class="wrap footer">МАИ · Информационный поиск · Учебный проект</footer></body></html>')
    return "".join(parts)


def make_handler(engine, catalog):
    class SearchHandler(BaseHTTPRequestHandler):
        def do_GET(self):
            parsed = urlsplit(self.path)
            if parsed.path not in {"/", "/search"}:
                self.send_error(404)
                return
            values = parse_qs(parsed.query)
            query = values.get("q", [""])[0].strip()
            status = 200
            ids = None
            elapsed_ms = None
            error = None
            try:
                page = int(values.get("page", ["1"])[0])
                if page < 1:
                    raise ValueError
            except ValueError:
                page = 1
                error = "Номер страницы должен быть положительным целым числом"
                status = 400
            if len(query) > 500:
                error = "Запрос слишком длинный (не более 500 символов)"
                status = 400
            if parsed.path == "/search" and query and not error:
                started = time.perf_counter()
                try:
                    ids = engine.search(query)
                except QuerySyntaxError as exception:
                    error = str(exception)
                    status = 400
                elapsed_ms = (time.perf_counter() - started) * 1000
                if ids is not None and page > max(1, (len(ids) + PAGE_SIZE - 1) // PAGE_SIZE):
                    error = "Такой страницы результатов нет"
                    status = 404
            body = render_page(query, catalog, ids, elapsed_ms, page, error).encode("utf-8")
            self.send_response(status)
            self.send_header("Content-Type", "text/html; charset=utf-8")
            self.send_header("Content-Length", str(len(body)))
            self.send_header("Cache-Control", "no-store")
            self.end_headers()
            self.wfile.write(body)

    return SearchHandler


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--host", default="127.0.0.1")
    parser.add_argument("--port", type=int, default=8000)
    parser.add_argument("--index-dir", default=str(DATA_DIR / "index"))
    parser.add_argument("--data-dir", default=str(DATA_DIR))
    args = parser.parse_args()
    engine = BooleanSearch(args.index_dir)
    server = None
    try:
        catalog = DocumentCatalog(args.data_dir, engine.index.doc_names)
        server = HTTPServer((args.host, args.port), make_handler(engine, catalog))
        print("Поиск: http://%s:%d/" % server.server_address, flush=True)
        server.serve_forever()
    except KeyboardInterrupt:
        pass
    finally:
        if server is not None:
            server.server_close()
        engine.close()


if __name__ == "__main__":
    main()
