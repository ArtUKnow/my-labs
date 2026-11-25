import os
import sys
import time
import urllib.request
import urllib.parse
from html.parser import HTMLParser


BASE_DIR = os.path.dirname(os.path.abspath(__file__))
RAW_DIR = os.path.join(BASE_DIR, "data", "raw")

SOURCES = [
    {
        "name": "ru_wikipedia",
        "host": "https://ru.wikipedia.org",
        "category": "/wiki/Категория:Космонавтика",
        "limit": 40,
    },
    {
        "name": "simple_wikipedia",
        "host": "https://simple.wikipedia.org",
        "category": "/wiki/Category:Spaceflight",
        "limit": 20,
    },
]

HEADERS = {"User-Agent": "MAI-IR-student-bot/1.0 (study project)"}


class LinkCollector(HTMLParser):
    def __init__(self):
        HTMLParser.__init__(self)
        self.links = []

    def handle_starttag(self, tag, attrs):
        if tag != "a":
            return
        for name, value in attrs:
            if name == "href" and value is not None:
                self.links.append(value)


def encode_url(url):
    return urllib.parse.quote(url, safe=":/?=&%")


def download_page(url):
    request = urllib.request.Request(encode_url(url), headers=HEADERS)
    response = urllib.request.urlopen(request, timeout=30)
    data = response.read()
    return data.decode("utf-8", "ignore")


def is_article_link(href):
    if not href.startswith("/wiki/"):
        return False
    if ":" in href:
        return False
    if "#" in href:
        return False
    if href == "/wiki/Main_Page" or href == "/wiki/Заглавная_страница":
        return False
    return True


def collect_article_links(host, category_path, limit):
    html = download_page(host + category_path)
    collector = LinkCollector()
    collector.feed(html)
    result = []
    seen = set()
    for href in collector.links:
        if not is_article_link(href):
            continue
        if href in seen:
            continue
        seen.add(href)
        result.append(href)
        if len(result) >= limit:
            break
    return result


def make_file_name(href):
    title = href.replace("/wiki/", "")
    title = urllib.parse.unquote(title)
    safe = ""
    for ch in title:
        if ch.isalnum() or ch in "_-":
            safe += ch
        else:
            safe += "_"
    return safe[:80] + ".html"


def download_source(source, delay):
    out_dir = os.path.join(RAW_DIR, source["name"])
    os.makedirs(out_dir, exist_ok=True)
    links = collect_article_links(source["host"], source["category"], source["limit"])
    print("source", source["name"], "found links:", len(links))
    url_list_path = os.path.join(out_dir, "urls.txt")
    url_file = open(url_list_path, "w", encoding="utf-8")
    for href in links:
        url = source["host"] + href
        file_name = make_file_name(href)
        file_path = os.path.join(out_dir, file_name)
        if os.path.exists(file_path):
            print("skip", file_name)
            url_file.write(url + "\t" + file_name + "\n")
            continue
        try:
            html = download_page(url)
        except Exception as error:
            print("error", url, error)
            continue
        f = open(file_path, "w", encoding="utf-8")
        f.write(html)
        f.close()
        url_file.write(url + "\t" + file_name + "\n")
        print("saved", file_name, len(html), "bytes")
        time.sleep(delay)
    url_file.close()


def main():
    delay = 0.5
    if len(sys.argv) > 1:
        delay = float(sys.argv[1])
    for source in SOURCES:
        download_source(source, delay)


if __name__ == "__main__":
    main()
