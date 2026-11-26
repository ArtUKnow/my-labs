"""Historical Wikipedia baseline; excluded from the primary SCOAP3 evaluation."""
import os
import sys
import json
import time
import urllib.request
import urllib.parse


BASE_DIR = os.path.dirname(os.path.abspath(__file__))
TEXT_DIR = os.path.join(BASE_DIR, "..", "lab1_corpus", "data", "text")
QUERIES_PATH = os.path.join(BASE_DIR, "..", "lab1_corpus", "data", "queries", "legacy", "queries.txt")
OUT_PATH = os.path.join(BASE_DIR, "..", "lab1_corpus", "data", "queries", "legacy", "wikipedia_results.txt")

HOSTS = {"ru_wikipedia": "https://ru.wikipedia.org", "simple_wikipedia": "https://simple.wikipedia.org"}


def corpus_titles():
    titles = {}
    for source in os.listdir(TEXT_DIR):
        source_dir = os.path.join(TEXT_DIR, source)
        if not os.path.isdir(source_dir):
            continue
        for name in os.listdir(source_dir):
            if name.endswith(".txt"):
                first_line = open(os.path.join(source_dir, name), encoding="utf-8").readline().strip()
                titles[(source, first_line)] = source + "/" + name[:-4]
    return titles


def wikipedia_search(host, query, limit):
    params = urllib.parse.urlencode({"action": "query", "list": "search", "srsearch": query, "srlimit": limit, "format": "json"})
    url = host + "/w/api.php?" + params
    request = urllib.request.Request(url, headers={"User-Agent": "MAI-IR-student-bot/1.0"})
    data = json.loads(urllib.request.urlopen(request, timeout=30).read().decode("utf-8"))
    return [item["title"] for item in data["query"]["search"]]


def main():
    titles = corpus_titles()
    out = open(OUT_PATH, "w", encoding="utf-8")
    for line in open(QUERIES_PATH, encoding="utf-8"):
        line = line.strip()
        if line == "":
            continue
        query_id, text = line.split("\t")
        rank = 0
        for source in HOSTS:
            if source == "simple_wikipedia" and not text.isascii():
                continue
            if source == "ru_wikipedia" and text.isascii():
                continue
            found = None
            for attempt in range(3):
                try:
                    found = wikipedia_search(HOSTS[source], text, 50)
                    break
                except Exception as error:
                    print("error", query_id, error, "retry")
                    time.sleep(5)
            if found is None:
                continue
            for title in found:
                doc_id = titles.get((source, title))
                if doc_id is not None:
                    rank += 1
                    out.write("%s\t%s\t%d\n" % (query_id, doc_id, rank))
            time.sleep(2)
        print(query_id, text, "->", rank, "docs from corpus")
    out.close()


if __name__ == "__main__":
    main()
