import os
import sys
import time
import signal
import ipaddress
import urllib.request
import urllib.parse
import urllib.error
from html.parser import HTMLParser

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from yaml_reader import read_yaml
from storage import Storage, make_hash


HEADERS = {"User-Agent": "MAI-IR-student-bot/1.0 (study project)"}

stop_flag = False


def handle_stop(signum, frame):
    global stop_flag
    stop_flag = True
    print("stop requested, finishing current page")


class LinkParser(HTMLParser):
    def __init__(self):
        HTMLParser.__init__(self)
        self.links = []

    def handle_starttag(self, tag, attrs):
        if tag != "a":
            return
        for name, value in attrs:
            if name == "href" and value:
                self.links.append(value)


def normalize_url(url):
    url = url.strip()
    if "#" in url:
        url = url[: url.index("#")]
    parts = urllib.parse.urlsplit(url)
    scheme = parts.scheme.lower()
    host = parts.netloc.lower()
    if host.endswith(":80") and scheme == "http":
        host = host[:-3]
    if host.endswith(":443") and scheme == "https":
        host = host[:-4]
    path = urllib.parse.unquote(parts.path)
    if path == "":
        path = "/"
    if path != "/" and path.endswith("/"):
        path = path[:-1]
    query = parts.query
    return urllib.parse.urlunsplit((scheme, host, path, query, ""))


def encode_url(url):
    return urllib.parse.quote(url, safe=":/?=&%")


def fetch(url, timeout):
    request = urllib.request.Request(encode_url(url), headers=HEADERS)
    host = urllib.parse.urlsplit(url).hostname or ""
    try:
        loopback = ipaddress.ip_address(host).is_loopback
    except ValueError:
        loopback = host == "localhost"
    opener = urllib.request.build_opener(urllib.request.ProxyHandler({})) if loopback else urllib.request.build_opener()
    with opener.open(request, timeout=timeout) as response:
        data = response.read()
    return data.decode("utf-8", "ignore")


def extract_links(html, base_url):
    parser = LinkParser()
    parser.feed(html)
    result = []
    for href in parser.links:
        full = urllib.parse.urljoin(base_url, href)
        result.append(normalize_url(full))
    return result


def link_allowed(url, source):
    if not url.startswith(source["prefix"]):
        return False
    for bad in source.get("skip", []):
        if bad in url:
            return False
    return True


class Crawler:
    def __init__(self, config):
        self.config = config
        self.delay = float(config["logic"].get("delay", 1))
        self.max_pages = int(config["logic"].get("max_pages", 100))
        self.timeout = int(config["logic"].get("timeout", 30))
        self.recrawl_after = int(config["logic"].get("recrawl_after", 86400))
        self.retry_delay = int(config["logic"].get("retry_delay", 60))
        self.sources = []
        for name in config:
            if name.startswith("source_"):
                source = dict(config[name])
                source["name"] = name.replace("source_", "")
                self.sources.append(source)
        self.storage = Storage(config["db"]["path"])
        self.downloaded = 0
        self.updated = 0
        self.unchanged = 0
        self.errors = 0

    def find_source(self, name):
        for source in self.sources:
            if source["name"] == name:
                return source
        return None

    def add_seeds(self):
        for source in self.sources:
            seeds = source.get("seeds", [])
            if not isinstance(seeds, list):
                seeds = [seeds]
            for seed in seeds:
                self.storage.add_to_queue(normalize_url(seed), source["name"], int(time.time()))

    def plan_recrawl(self):
        border = int(time.time()) - self.recrawl_after
        for url, source_name in self.storage.old_documents(border):
            added = self.storage.add_to_queue(url, source_name, int(time.time()))
            if not added:
                self.storage.requeue(url, int(time.time()))

    def process_page(self, url, source_name):
        source = self.find_source(source_name)
        try:
            html = fetch(url, self.timeout)
        except urllib.error.HTTPError as error:
            print("error", url, error)
            self.errors += 1
            permanent = error.code not in (408, 429) and error.code < 500
            error.close()
            return permanent
        except (urllib.error.URLError, TimeoutError) as error:
            print("error", url, error)
            self.errors += 1
            return False
        old = self.storage.get_document(url)
        now = int(time.time())
        if old is not None and old["content_hash"] == make_hash(html):
            self.unchanged += 1
            self.storage.save_document(url, old["html"], source_name, now)
            print("unchanged", url)
        else:
            if old is None:
                self.downloaded += 1
                print("new", url, len(html))
            else:
                self.updated += 1
                print("updated", url, len(html))
            self.storage.save_document(url, html, source_name, now)
        if source is None:
            return True
        for link in extract_links(html, url):
            if link_allowed(link, source) and not self.storage.has_document(link):
                self.storage.add_to_queue(link, source_name, now)
        return True

    def run(self):
        self.add_seeds()
        self.plan_recrawl()
        processed = 0
        while not stop_flag and processed < self.max_pages:
            row = self.storage.next_in_queue()
            if row is None:
                print("no pages ready" if self.storage.queue_size() else "queue is empty")
                break
            url, source_name = row
            if self.process_page(url, source_name):
                self.storage.mark_done(url)
            else:
                self.storage.requeue(url, int(time.time()) + self.retry_delay)
            processed += 1
            if self.delay > 0:
                time.sleep(self.delay)
        print("processed:", processed)
        print("new:", self.downloaded, "updated:", self.updated, "unchanged:", self.unchanged, "errors:", self.errors)
        print("documents in db:", self.storage.count_documents())
        print("left in queue:", self.storage.queue_size())
        return processed


def main():
    if len(sys.argv) > 1 and sys.argv[1] == "--scoap3":
        from scoap3 import main as collect_scoap3
        sys.argv = [sys.argv[0]] + sys.argv[2:]
        return collect_scoap3()
    if len(sys.argv) != 2:
        print("usage: python crawler.py config.yaml")
        sys.exit(1)
    config = read_yaml(sys.argv[1])
    if config.get("corpus", {}).get("kind") == "scoap3":
        from scoap3 import main as collect_scoap3
        options = config["corpus"]
        sys.argv = [sys.argv[0], "--collect-only", "--target", str(options["target"]),
                    "--min-words", str(options["min_words"]),
                    "--download-workers", str(options["download_workers"])]
        return collect_scoap3()
    signal.signal(signal.SIGINT, handle_stop)
    signal.signal(signal.SIGTERM, handle_stop)
    db_path = config["db"]["path"]
    if not os.path.isabs(db_path):
        config["db"]["path"] = os.path.join(os.path.dirname(os.path.abspath(sys.argv[1])), db_path)
    crawler = Crawler(config)
    crawler.run()
    crawler.storage.close()


if __name__ == "__main__":
    main()
