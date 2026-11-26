import os
import sys
import time
import shutil
import tempfile
import threading
import unittest
import http.server
import socketserver
import urllib.error
from unittest import mock

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import crawler
import storage
import yaml_reader


PAGES = {
    "index.html": '<html><body><a href="/a.html">a</a><a href="/b.html#top">b</a><a href="http://other.site/x">x</a><a href="/skip_me.html">s</a></body></html>',
    "a.html": '<html><body>page a <a href="/c.html">c</a><a href="/index.html">home</a></body></html>',
    "b.html": "<html><body>page b</body></html>",
    "c.html": '<html><body>page c <a href="/a.html">a</a></body></html>',
    "skip_me.html": "<html><body>should be skipped</body></html>",
}


class QuietHandler(http.server.SimpleHTTPRequestHandler):
    def log_message(self, format, *args):
        pass


class TestServer:
    def __init__(self, folder):
        self.folder = folder
        handler = lambda *args, **kwargs: QuietHandler(*args, directory=folder, **kwargs)
        socketserver.TCPServer.allow_reuse_address = True
        self.server = socketserver.TCPServer(("127.0.0.1", 0), handler)
        self.port = self.server.server_address[1]
        self.thread = threading.Thread(target=self.server.serve_forever)
        self.thread.daemon = True
        self.thread.start()

    def url(self, path):
        return "http://127.0.0.1:%d/%s" % (self.port, path)

    def stop(self):
        self.server.shutdown()
        self.server.server_close()


def write_pages(folder, pages):
    for name in pages:
        f = open(os.path.join(folder, name), "w", encoding="utf-8")
        f.write(pages[name])
        f.close()


class TestNormalize(unittest.TestCase):
    def test_fragment_removed(self):
        self.assertEqual(crawler.normalize_url("http://a.com/x#part"), "http://a.com/x")

    def test_host_lowercase(self):
        self.assertEqual(crawler.normalize_url("HTTP://A.COM/Path"), "http://a.com/Path")

    def test_default_port(self):
        self.assertEqual(crawler.normalize_url("http://a.com:80/"), "http://a.com/")
        self.assertEqual(crawler.normalize_url("https://a.com:443/x"), "https://a.com/x")

    def test_trailing_slash(self):
        self.assertEqual(crawler.normalize_url("http://a.com/x/"), "http://a.com/x")

    def test_percent_decoded(self):
        self.assertEqual(crawler.normalize_url("https://ru.wikipedia.org/wiki/%D0%9A"), "https://ru.wikipedia.org/wiki/К")

    def test_empty_path(self):
        self.assertEqual(crawler.normalize_url("http://a.com"), "http://a.com/")


class TestYaml(unittest.TestCase):
    def test_sections_and_types(self):
        lines = [
            "db:\n",
            "  path: test.db\n",
            "logic:\n",
            "  delay: 0.5  # comment\n",
            "  max_pages: 10\n",
            "  flag: true\n",
            "source_x:\n",
            "  prefix: http://x/\n",
            "  seeds:\n",
            "    - http://x/a\n",
            "    - http://x/b\n",
        ]
        data = yaml_reader.parse_yaml_lines(lines)
        self.assertEqual(data["db"]["path"], "test.db")
        self.assertEqual(data["logic"]["delay"], 0.5)
        self.assertEqual(data["logic"]["max_pages"], 10)
        self.assertEqual(data["logic"]["flag"], True)
        self.assertEqual(data["source_x"]["seeds"], ["http://x/a", "http://x/b"])

    def test_quoted_values(self):
        data = yaml_reader.parse_yaml_lines(["a:\n", '  skip:\n', '    - "Файл:"\n'])
        self.assertEqual(data["a"]["skip"], ["Файл:"])

    def test_real_config(self):
        path = os.path.join(os.path.dirname(os.path.abspath(__file__)), "config.yaml")
        data = yaml_reader.read_yaml(path)
        self.assertEqual(data["corpus"]["kind"], "scoap3")
        self.assertGreaterEqual(data["corpus"]["target"], 30000)
        self.assertGreaterEqual(data["corpus"]["min_words"], 3000)
        path = os.path.join(os.path.dirname(path), "config_html_example.yaml")
        data = yaml_reader.read_yaml(path)
        self.assertIn("db", data)
        self.assertIn("logic", data)
        self.assertIn("delay", data["logic"])


class TestStorage(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.db = storage.Storage(os.path.join(self.tmp, "sub", "test.db"))

    def tearDown(self):
        self.db.close()
        shutil.rmtree(self.tmp)

    def test_save_and_get(self):
        self.db.save_document("http://a/1", "<html>1</html>", "a", 100)
        doc = self.db.get_document("http://a/1")
        self.assertEqual(doc["html"], "<html>1</html>")
        self.assertEqual(doc["source"], "a")
        self.assertEqual(doc["crawled_at"], 100)
        self.assertEqual(doc["content_hash"], storage.make_hash("<html>1</html>"))

    def test_queue_order(self):
        self.db.add_to_queue("http://a/2", "a", 2)
        self.db.add_to_queue("http://a/1", "a", 1)
        self.assertEqual(self.db.next_in_queue()[0], "http://a/1")
        self.db.mark_done("http://a/1")
        self.assertEqual(self.db.next_in_queue()[0], "http://a/2")
        self.db.mark_done("http://a/2")
        self.assertIsNone(self.db.next_in_queue())

    def test_queue_no_duplicates(self):
        self.assertTrue(self.db.add_to_queue("http://a/1", "a", 1))
        self.assertFalse(self.db.add_to_queue("http://a/1", "a", 2))
        self.assertEqual(self.db.queue_size(), 1)

    def test_deferred_page_waits_until_due(self):
        url = "http://a/1"
        self.db.add_to_queue(url, "a", int(time.time()) + 60)
        self.assertIsNone(self.db.next_in_queue())
        self.assertEqual(self.db.queue_size(), 1)
        self.db.requeue(url, int(time.time()) - 1)
        self.assertEqual(self.db.next_in_queue()[0], url)

    def test_old_documents(self):
        self.db.save_document("http://a/1", "x", "a", 100)
        self.db.save_document("http://a/2", "y", "a", 200)
        old = self.db.old_documents(150)
        self.assertEqual(len(old), 1)
        self.assertEqual(old[0][0], "http://a/1")


class TestCrawl(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.site = os.path.join(self.tmp, "site")
        os.makedirs(self.site)
        write_pages(self.site, PAGES)
        self.server = TestServer(self.site)
        crawler.stop_flag = False

    def tearDown(self):
        self.server.stop()
        shutil.rmtree(self.tmp)

    def make_config(self, max_pages, recrawl_after=86400):
        return {
            "db": {"path": os.path.join(self.tmp, "crawl.db")},
            "logic": {"delay": 0, "max_pages": max_pages, "timeout": 5, "recrawl_after": recrawl_after},
            "source_test": {
                "prefix": self.server.url(""),
                "seeds": [self.server.url("index.html")],
                "skip": ["skip_me"],
            },
        }

    def test_full_crawl(self):
        bot = crawler.Crawler(self.make_config(100))
        bot.run()
        urls = bot.storage.all_urls()
        bot.storage.close()
        self.assertEqual(len(urls), 4)
        self.assertIn(self.server.url("a.html"), urls)
        self.assertIn(self.server.url("b.html"), urls)
        self.assertIn(self.server.url("c.html"), urls)
        self.assertNotIn(self.server.url("skip_me.html"), urls)

    def test_document_fields(self):
        bot = crawler.Crawler(self.make_config(100))
        bot.run()
        doc = bot.storage.get_document(self.server.url("b.html"))
        bot.storage.close()
        self.assertEqual(doc["html"], PAGES["b.html"])
        self.assertEqual(doc["source"], "test")
        self.assertTrue(abs(doc["crawled_at"] - int(time.time())) < 10)

    def test_resume_after_stop(self):
        bot = crawler.Crawler(self.make_config(2))
        first = bot.run()
        count_after_first = bot.storage.count_documents()
        bot.storage.close()
        self.assertEqual(first, 2)
        self.assertEqual(count_after_first, 2)
        bot = crawler.Crawler(self.make_config(100))
        bot.run()
        self.assertEqual(bot.storage.count_documents(), 4)
        self.assertEqual(bot.storage.queue_size(), 0)
        bot.storage.close()

    def test_recrawl_only_changed(self):
        bot = crawler.Crawler(self.make_config(100))
        bot.run()
        bot.storage.close()
        time.sleep(2.1)
        new_pages = dict(PAGES)
        new_pages["b.html"] = "<html><body>page b changed</body></html>"
        write_pages(self.site, new_pages)
        bot = crawler.Crawler(self.make_config(100, recrawl_after=1))
        bot.run()
        self.assertEqual(bot.updated, 1)
        self.assertEqual(bot.unchanged, 3)
        self.assertEqual(bot.downloaded, 0)
        doc = bot.storage.get_document(self.server.url("b.html"))
        self.assertIn("changed", doc["html"])
        bot.storage.close()

    def test_no_recrawl_when_fresh(self):
        bot = crawler.Crawler(self.make_config(100))
        bot.run()
        bot.storage.close()
        bot = crawler.Crawler(self.make_config(100))
        processed = bot.run()
        bot.storage.close()
        self.assertEqual(processed, 0)

    def test_error_page(self):
        config = self.make_config(100)
        config["source_test"]["seeds"] = [self.server.url("missing.html")]
        bot = crawler.Crawler(config)
        bot.run()
        self.assertEqual(bot.errors, 1)
        self.assertEqual(bot.storage.count_documents(), 0)
        self.assertEqual(bot.storage.queue_size(), 0)
        bot.storage.close()

    def test_transient_error_retried_after_restart(self):
        config = self.make_config(1)
        config["logic"]["retry_delay"] = 0
        url = self.server.url("index.html")
        unavailable = urllib.error.HTTPError(url, 503, "Service Unavailable", {}, None)
        with mock.patch.object(crawler, "fetch", side_effect=[unavailable, PAGES["index.html"]]) as fetch:
            bot = crawler.Crawler(config)
            bot.run()
            self.assertEqual(bot.errors, 1)
            self.assertEqual(bot.storage.queue_size(), 1)
            self.assertEqual(bot.storage.next_in_queue()[0], url)
            bot.storage.close()

            bot = crawler.Crawler(config)
            bot.run()
            self.assertEqual(bot.storage.get_document(url)["html"], PAGES["index.html"])
            bot.storage.close()
            self.assertEqual(fetch.call_count, 2)


class TestSharedCorpus(unittest.TestCase):
    def test_resume_uses_the_existing_complete_manifest(self):
        import scoap3
        from pathlib import Path
        self.assertEqual(scoap3.DATA.resolve(), (Path(__file__).resolve().parent.parent / "lab1_corpus" / "data").resolve())
        with tempfile.TemporaryDirectory() as folder:
            path = Path(folder)
            output = path / "text"
            manifest = path / "manifest.tsv"
            import csv
            with manifest.open("w") as file:
                writer = csv.DictWriter(file, scoap3.FIELDS, delimiter="\t")
                writer.writeheader()
                writer.writerow({"record_id": "1", "file": "scoap3_1.txt.gz"})
            with mock.patch.object(scoap3, "fetch_article") as fetch_article:
                scoap3.collect({"1": {"record_id": "1"}}, output, manifest, target=1)
                fetch_article.assert_not_called()


if __name__ == "__main__":
    unittest.main(verbosity=2)
