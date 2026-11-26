import os
import sys
import json
import csv
import hashlib
import gzip
import shutil
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import extract_text
import corpus_stats
import download_samples


SAMPLE_HTML = """
<html><head><title>Тестовая статья — Википедия</title>
<style>p { color: red }</style>
<script>var x = 1;</script></head>
<body>
<div id="mw-content-text">
<p>Первый абзац про <b>космос</b> и ракеты.</p>
<table><tr><td>таблица не нужна</td></tr></table>
<h2>Заголовок</h2>
<ul><li>пункт один</li><li>пункт два<sup>[1]</sup></li></ul>
<p>Второй    абзац   с   пробелами.</p>
</div>
<div id="footer"><p>подвал</p></div>
</body></html>
"""


class TestExtractText(unittest.TestCase):
    def test_tags_removed(self):
        text = extract_text.extract_text_from_html(SAMPLE_HTML)
        self.assertNotIn("<", text)
        self.assertNotIn(">", text)

    def test_script_and_style_skipped(self):
        text = extract_text.extract_text_from_html(SAMPLE_HTML)
        self.assertNotIn("var x", text)
        self.assertNotIn("color", text)

    def test_table_skipped(self):
        text = extract_text.extract_text_from_html(SAMPLE_HTML)
        self.assertNotIn("таблица", text)

    def test_footnote_skipped(self):
        text = extract_text.extract_text_from_html(SAMPLE_HTML)
        self.assertNotIn("[1]", text)

    def test_paragraphs_kept(self):
        text = extract_text.extract_text_from_html(SAMPLE_HTML)
        self.assertIn("Первый абзац про космос и ракеты.", text)
        self.assertIn("пункт один", text)
        self.assertIn("Заголовок", text)

    def test_spaces_normalized(self):
        text = extract_text.extract_text_from_html(SAMPLE_HTML)
        self.assertIn("Второй абзац с пробелами.", text)

    def test_outside_content_skipped(self):
        text = extract_text.extract_text_from_html(SAMPLE_HTML)
        self.assertNotIn("подвал", text)

    def test_title(self):
        title = extract_text.extract_title(SAMPLE_HTML)
        self.assertEqual(title, "Тестовая статья")

    def test_empty_html(self):
        text = extract_text.extract_text_from_html("")
        self.assertEqual(text, "")


class TestLinks(unittest.TestCase):
    def test_article_link(self):
        self.assertTrue(download_samples.is_article_link("/wiki/Космонавтика"))

    def test_service_link(self):
        self.assertFalse(download_samples.is_article_link("/wiki/Категория:Космос"))
        self.assertFalse(download_samples.is_article_link("/wiki/Файл:Rocket.jpg"))

    def test_external_link(self):
        self.assertFalse(download_samples.is_article_link("https://example.com"))

    def test_file_name(self):
        name = download_samples.make_file_name("/wiki/%D0%9A%D0%BE%D1%81%D0%BC%D0%BE%D1%81")
        self.assertEqual(name, "Космос.html")

    def test_collect_links(self):
        html = '<a href="/wiki/A">a</a><a href="/wiki/A">dup</a><a href="/wiki/B">b</a>'
        collector = download_samples.LinkCollector()
        collector.feed(html)
        self.assertEqual(collector.links, ["/wiki/A", "/wiki/A", "/wiki/B"])


class TestStats(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.raw = os.path.join(self.tmp, "raw")
        self.text = os.path.join(self.tmp, "text")
        os.makedirs(os.path.join(self.raw, "src_a"))
        os.makedirs(os.path.join(self.raw, "src_b"))
        for i in range(3):
            f = open(os.path.join(self.raw, "src_a", "doc%d.html" % i), "w", encoding="utf-8")
            f.write(SAMPLE_HTML)
            f.close()
        f = open(os.path.join(self.raw, "src_b", "doc.html"), "w", encoding="utf-8")
        f.write(SAMPLE_HTML)
        f.close()

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_process_all(self):
        count = extract_text.process_all(self.raw, self.text)
        self.assertEqual(count, 4)
        self.assertTrue(os.path.exists(os.path.join(self.text, "src_a", "doc0.txt")))

    def test_stats_numbers(self):
        extract_text.process_all(self.raw, self.text)
        stats = corpus_stats.collect_stats(self.raw, self.text)
        self.assertEqual(len(stats["sources"]), 2)
        self.assertEqual(stats["total"]["documents"], 4)
        self.assertTrue(stats["total"]["text_bytes"] < stats["total"]["raw_bytes"])
        self.assertTrue(stats["total"]["words"] > 0)

    def test_stats_json_serializable(self):
        extract_text.process_all(self.raw, self.text)
        stats = corpus_stats.collect_stats(self.raw, self.text)
        json.dumps(stats)


class TestRealCorpus(unittest.TestCase):
    def test_two_raw_sources_exist(self):
        if not os.path.isdir(extract_text.RAW_DIR):
            self.skipTest("corpus not downloaded")
        sources = [d for d in os.listdir(extract_text.RAW_DIR) if os.path.isdir(os.path.join(extract_text.RAW_DIR, d))]
        self.assertTrue(len(sources) >= 2)

    def test_documents_not_empty(self):
        if not os.path.isdir(extract_text.TEXT_DIR):
            self.skipTest("corpus not extracted")
        for source in os.listdir(extract_text.TEXT_DIR):
            source_dir = os.path.join(extract_text.TEXT_DIR, source)
            for name in os.listdir(source_dir):
                size = os.path.getsize(os.path.join(source_dir, name))
                self.assertTrue(size > 20, name)

    def test_corpus_integrity_and_assignment_minimum(self):
        text_dir = os.path.join(os.path.dirname(extract_text.TEXT_DIR), "text", "scoap3")
        manifest_path = os.path.join(os.path.dirname(extract_text.TEXT_DIR), "provenance", "scoap3.tsv")
        files = {name for name in os.listdir(text_dir) if name.endswith(".txt.gz")}
        with open(manifest_path, encoding="utf-8") as manifest:
            rows = list(csv.DictReader(manifest, delimiter="\t"))
        self.assertGreaterEqual(len(rows), 30000)
        self.assertEqual({row["file"] for row in rows}, files)
        self.assertEqual(len(rows), len(files))
        self.assertEqual(len({row["doi"] for row in rows}), len(rows))
        for row in rows:
            self.assertGreaterEqual(int(row["words"]), 3000)
            self.assertTrue(row["journal"] and row["doi"])
            self.assertTrue(row["license"].upper().startswith("CC-BY"))
            self.assertGreater(int(row["source_bytes"]), 0)
            self.assertIn(row["source_format"], ("xml", "pdf"))
            self.assertTrue(row["url"].startswith("https://repo.scoap3.org/records/"))
            with gzip.open(os.path.join(text_dir, row["file"]), "rb") as document:
                self.assertEqual(row["sha256"], hashlib.sha256(document.read()).hexdigest())

    def test_corpus_reaches_requested_50000_target(self):
        manifest_path = os.path.join(os.path.dirname(extract_text.TEXT_DIR), "provenance", "scoap3.tsv")
        with open(manifest_path, encoding="utf-8") as manifest:
            rows = list(csv.DictReader(manifest, delimiter="\t"))
        self.assertGreaterEqual(len(rows), 50000)


if __name__ == "__main__":
    unittest.main(verbosity=2)
