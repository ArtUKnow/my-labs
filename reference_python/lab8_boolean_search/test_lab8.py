import csv
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import threading
import unittest
from http.server import HTTPServer
from urllib.error import HTTPError
from urllib.parse import urlencode
from urllib.request import ProxyHandler, build_opener

sys.path.insert(0, str(Path(__file__).resolve().parent))
sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "lab7_boolean_index"))

from build_index import build_index
from boolean_search import BooleanSearch, QuerySyntaxError, difference, parse_query, union
from catalog import DocumentCatalog
from web import make_handler


class TestBooleanSearch(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        cls.tmp = tempfile.TemporaryDirectory()
        cls.data = Path(cls.tmp.name) / "data"
        text_dir = cls.data / "text" / "sample"
        text_dir.mkdir(parents=True)
        provenance_dir = cls.data / "provenance"
        provenance_dir.mkdir()
        bodies = {
            "a": "Mars rover on the red planet",
            "b": "Rocket launch to the Moon",
            "c": "Mars rocket rover mission",
            "d": "Astronomy telescope observations",
            "e": "Красная ракета летит к Марсу",
        }
        for number in range(55):
            bodies["filler_%02d" % number] = "Filler document about the universe"
        with (provenance_dir / "sample.tsv").open("w", encoding="utf-8", newline="") as file:
            writer = csv.writer(file, delimiter="\t", lineterminator="\n")
            writer.writerow(["file", "title", "url", "author"])
            for name, body in bodies.items():
                (text_dir / (name + ".txt")).write_text(name + "\n" + body + "\n", encoding="utf-8")
                writer.writerow([name + ".txt", "Title " + name, "https://example.org/" + name, "Tester"])
        cls.index_dir = cls.data / "index"
        build_index(str(cls.data / "text"), str(cls.index_dir), "stem", 20)
        cls.engine = BooleanSearch(str(cls.index_dir))
        cls.catalog = DocumentCatalog(cls.data, cls.engine.index.doc_names)

    @classmethod
    def tearDownClass(cls):
        cls.engine.close()
        cls.tmp.cleanup()

    def names(self, query):
        return set(self.engine.index.names(self.engine.search(query)))

    def test_implicit_and(self):
        self.assertEqual(self.names("mars rover"), {"sample/a", "sample/c"})
        self.assertEqual(self.names("  mars   rover  "), {"sample/a", "sample/c"})

    def test_explicit_and(self):
        self.assertEqual(self.names("mars&&rocket"), {"sample/c"})

    def test_or_and_deduplication(self):
        self.assertEqual(self.names("mars || rocket"), {"sample/a", "sample/b", "sample/c"})

    def test_not_with_positive_term(self):
        self.assertEqual(self.names("rocket !moon"), {"sample/c"})
        self.assertEqual(self.names("mars && !rocket"), {"sample/a"})

    def test_only_not_and_double_not(self):
        self.assertEqual(len(self.engine.search("!mars")), 58)
        self.assertEqual(self.names("!!mars"), {"sample/a", "sample/c"})

    def test_precedence(self):
        self.assertEqual(self.names("mars || rocket telescope"), {"sample/a", "sample/c"})
        self.assertEqual(self.names("(mars || rocket) telescope"), set())

    def test_parentheses_and_brackets(self):
        self.assertEqual(self.names("[mars || rocket] !moon"), {"sample/a", "sample/c"})
        self.assertEqual(self.names("(mars || rocket) && !moon"), {"sample/a", "sample/c"})

    def test_unknown_and_empty_query(self):
        self.assertEqual(self.engine.search("unfindableword"), [])
        self.assertEqual(self.engine.search("  "), [])
        self.assertEqual(len(self.engine.search("!unfindableword")), 60)

    def test_cyrillic_stemming(self):
        self.assertEqual(self.names("ракеты"), {"sample/e"})

    def test_sorted_set_operations(self):
        self.assertEqual(union([1, 3, 5], [2, 3, 4]), [1, 2, 3, 4, 5])
        self.assertEqual(difference([1, 2, 3, 4], [2, 4, 8]), [1, 3])

    def test_malformed_queries(self):
        for query in ("mars ||", "(mars", "mars)", "mars & rover", "[]", "(mars]", "&& mars"):
            with self.subTest(query=query):
                with self.assertRaises(QuerySyntaxError):
                    parse_query(query)

    def test_catalog_links_and_snippet(self):
        number = self.engine.search("mars rover")[0]
        record = self.catalog.get(number)
        self.assertTrue(record["url"].startswith("https://example.org/"))
        self.assertIn("Mars rover", self.catalog.snippet(number))

    def test_cli_jsonl_multiple_queries(self):
        script = Path(__file__).with_name("cli.py")
        result = subprocess.run(
            [sys.executable, str(script), "--index-dir", str(self.index_dir), "--data-dir", str(self.data), "--jsonl", "--limit", "2"],
            input="mars rover\nrocket !moon\n",
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        self.assertEqual(result.returncode, 0, result.stderr)
        rows = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertEqual([row["count"] for row in rows], [2, 1])
        self.assertEqual(rows[0]["documents"][0]["title"], "Title a")

    def test_cli_reports_invalid_query(self):
        script = Path(__file__).with_name("cli.py")
        result = subprocess.run(
            [sys.executable, str(script), "--index-dir", str(self.index_dir), "--data-dir", str(self.data), "--jsonl"],
            input="mars ||\n",
            text=True, stdout=subprocess.PIPE, stderr=subprocess.PIPE,
        )
        self.assertEqual(result.returncode, 1)
        self.assertIn("error", json.loads(result.stdout))

    def test_web_form_results_and_pagination(self):
        server = HTTPServer(("127.0.0.1", 0), make_handler(self.engine, self.catalog))
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        opener = build_opener(ProxyHandler({}))
        base = "http://127.0.0.1:%d" % server.server_port
        try:
            home = opener.open(base + "/", timeout=5).read().decode("utf-8")
            self.assertIn("<form", home)
            self.assertIn("60 документов", home)
            search = opener.open(base + "/search?" + urlencode({"q": "filler"}), timeout=5).read().decode("utf-8")
            self.assertIn("Найдено: 55", search)
            self.assertIn("Следующие 50", search)
            self.assertEqual(search.count('<article class="result">'), 50)
            next_page = opener.open(base + "/search?" + urlencode({"q": "filler", "page": 2}), timeout=5).read().decode("utf-8")
            self.assertEqual(next_page.count('<article class="result">'), 5)
            self.assertIn("← Назад", next_page)
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=5)

    def test_web_escapes_query_and_rejects_syntax_error(self):
        server = HTTPServer(("127.0.0.1", 0), make_handler(self.engine, self.catalog))
        thread = threading.Thread(target=server.serve_forever, daemon=True)
        thread.start()
        opener = build_opener(ProxyHandler({}))
        base = "http://127.0.0.1:%d/search?" % server.server_port
        try:
            response = opener.open(base + urlencode({"q": "<script>alert(1)</script>"}), timeout=5).read().decode("utf-8")
            self.assertIn("&lt;script&gt;", response)
            self.assertNotIn("<script>alert(1)</script>", response)
            with self.assertRaises(HTTPError) as caught:
                opener.open(base + urlencode({"q": "mars ||"}), timeout=5)
            self.assertEqual(caught.exception.code, 400)
            caught.exception.close()
        finally:
            server.shutdown()
            server.server_close()
            thread.join(timeout=5)


if __name__ == "__main__":
    unittest.main(verbosity=2)
