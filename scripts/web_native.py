"""HTTP presentation only. Query parsing and retrieval run in compiled C++."""
import argparse
import html
import json
from http.server import BaseHTTPRequestHandler, HTTPServer
from pathlib import Path
import subprocess
import time
from urllib.parse import parse_qs, urlencode, urlsplit

ROOT = Path(__file__).resolve().parent.parent
PAGE_SIZE = 50


def search(binary, index, query, page=1):
    result = subprocess.run([str(binary), 'search', str(index), '--json', '--offset', str((page-1)*PAGE_SIZE), '--limit', str(PAGE_SIZE)],
                            input=query+'\n', text=True, capture_output=True, timeout=30)
    try:
        answer = json.loads(result.stdout)
    except json.JSONDecodeError as error:
        raise RuntimeError(result.stderr.strip() or 'Поисковая программа вернула некорректный ответ') from error
    if 'error' in answer:
        raise ValueError(answer['error'])
    if result.returncode:
        raise RuntimeError(result.stderr.strip() or 'Ошибка выполнения поиска')
    return answer


def page_html(query='', answer=None, page=1, error=None, elapsed=None):
    esc=html.escape
    chunks=['<!doctype html><html lang="ru"><meta charset="utf-8"><meta name="viewport" content="width=device-width"><title>Поиск научных статей</title>',
            '<style>body{font:17px/1.6 system-ui;background:#101827;color:#edf3ff;margin:0}main{max-width:1000px;margin:60px auto;padding:0 25px}h1{font-size:38px}input{width:65%;padding:14px;font:inherit}button{padding:14px;font:inherit}a{color:#9ac8ff}article{border-top:1px solid #3b4658;padding:18px 0}small{color:#b4c1d3}nav{display:flex;gap:30px;margin-top:30px}.error{color:#ffb8b8}</style><main>',
            '<small>МАИ · Информационный поиск</small><h1>Поиск научных статей</h1>',
            '<p>И: пробел или &amp;&amp; · ИЛИ: || · НЕ: ! · Скобки: ( )</p>',
            f'<form action="/search"><input name="q" aria-label="Поисковый запрос" value="{esc(query,quote=True)}" maxlength="500" required><button>Найти</button></form>']
    if error: chunks.append(f'<p class="error" role="alert">{esc(str(error))}</p>')
    if answer is not None:
        total=answer['count'];chunks.append(f'<p>Найдено: {total:,}. Время: {elapsed:.1f} мс.</p>')
        for position, document in enumerate(answer['documents'],(page-1)*PAGE_SIZE+1):
            url=document['url']
            if urlsplit(url).scheme not in ('https','http'):raise RuntimeError('Недопустимая ссылка в каталоге')
            chunks.append(f'<article><small>{position} · {esc(document["journal"])}</small><h2><a href="{esc(url,quote=True)}" rel="noopener noreferrer">{esc(document["title"])}</a></h2><small>{esc(url)}</small></article>')
        if not total: chunks.append('<p>Совпадений нет.</p>')
        chunks.append('<nav>')
        if page>1:chunks.append(f'<a href="/search?{esc(urlencode({"q":query,"page":page-1}),quote=True)}">Назад</a>')
        if page*PAGE_SIZE<total:chunks.append(f'<a href="/search?{esc(urlencode({"q":query,"page":page+1}),quote=True)}">Следующие 50</a>')
        chunks.append('</nav>')
    chunks.append('</main></html>')
    return ''.join(chunks)


def make_handler(binary,index):
    class Handler(BaseHTTPRequestHandler):
        def do_GET(self):
            parsed=urlsplit(self.path)
            if parsed.path not in ('/','/search'):self.send_error(404);return
            values=parse_qs(parsed.query);query=values.get('q',[''])[0].strip();status=200;answer=None;error=None;elapsed=None;page=1
            try:
                page=int(values.get('page',['1'])[0])
                if page<1:raise ValueError('Номер страницы должен быть положительным')
                if len(query)>500:raise ValueError('Запрос слишком длинный')
                if parsed.path=='/search' and query:
                    started=time.perf_counter();answer=search(binary,index,query,page);elapsed=(time.perf_counter()-started)*1000
                    if page>max(1,(answer['count']+PAGE_SIZE-1)//PAGE_SIZE):status=404;error='Такой страницы результатов нет'
            except ValueError as exception:status=400;error=str(exception)
            except (OSError,RuntimeError,subprocess.TimeoutExpired) as exception:status=500;error=str(exception)
            body=page_html(query,answer,page,error,elapsed).encode('utf-8');self.send_response(status);self.send_header('Content-Type','text/html; charset=utf-8');self.send_header('Content-Length',str(len(body)));self.end_headers();self.wfile.write(body)
    return Handler


def main():
    parser=argparse.ArgumentParser();parser.add_argument('--host',default='127.0.0.1');parser.add_argument('--port',type=int,default=8000)
    parser.add_argument('--binary',type=Path,default=ROOT/'build/ir');parser.add_argument('--index-dir',type=Path,default=ROOT/'lab1_corpus/data/native_index')
    args=parser.parse_args()
    if not args.binary.is_file():parser.error('Сначала выполните make')
    with HTTPServer((args.host,args.port),make_handler(args.binary,args.index_dir)) as server:
        print(f'http://{args.host}:{args.port}',flush=True)
        try:server.serve_forever()
        except KeyboardInterrupt:pass

if __name__=='__main__':main()
