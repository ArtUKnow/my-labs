
import gzip
import importlib.util
import json
import math
from pathlib import Path
import subprocess
import tempfile
import threading
import unittest
from urllib.error import HTTPError
from urllib.parse import urlencode
from urllib.request import ProxyHandler, build_opener
from http.server import HTTPServer

ROOT = Path(__file__).resolve().parent.parent
BINARY = ROOT / 'build' / 'ir'
spec = importlib.util.spec_from_file_location('web_native', ROOT / 'scripts' / 'web_native.py')
web_native = importlib.util.module_from_spec(spec)
spec.loader.exec_module(web_native)


class NativePipelineTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        if not BINARY.is_file():
            raise RuntimeError('Compiled executable is missing; run make before integration tests')
        cls.temporary = tempfile.TemporaryDirectory(prefix='mai-ir-native-integration-')
        cls.folder = Path(cls.temporary.name)
        cls.index = cls.folder / 'index'
        cls.documents = cls.folder / 'documents.tsv'
        with cls.documents.open('w', encoding='utf-8') as manifest:
            manifest.write('id\tpath\turl\ttitle\tjournal\n')
            for doc_id in range(121):
                text = 'shared matter particles '
                text += 'dark' if doc_id % 2 == 0 else 'quark'
                if doc_id % 3 == 0:
                    text += ' electron'
                path = cls.folder / f'doc_{doc_id}.txt.gz'
                with gzip.open(path, 'wt', encoding='utf-8') as output:
                    output.write(text)
                title = f'Article {doc_id}' if doc_id else '<script>alert("metadata")</script>'
                manifest.write(f'{doc_id}\t{path}\thttps://example.test/article/{doc_id}?a=1&b=2\t{title}\tJournal <physics>\n')
        result = subprocess.run([str(BINARY), 'build', str(cls.documents), str(cls.index), 'stem', '17'],
                                text=True, capture_output=True, timeout=60)
        if result.returncode:
            raise AssertionError(f'Native fixture build failed:\n{result.stderr}')
        cls.server = HTTPServer(('127.0.0.1', 0), web_native.make_handler(BINARY, cls.index))
        cls.thread = threading.Thread(target=cls.server.serve_forever, daemon=True)
        cls.thread.start()
        cls.base_url = f'http://127.0.0.1:{cls.server.server_port}'

    @classmethod
    def tearDownClass(cls):
        cls.server.shutdown()
        cls.server.server_close()
        cls.thread.join(timeout=5)
        cls.temporary.cleanup()

    def native_search(self, query, *options):
        result = subprocess.run([str(BINARY), 'search', str(self.index), '--json', *options],
                                input=query + '\n', text=True, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        return json.loads(result.stdout)

    def http_get(self, path):
        try:
            response = build_opener(ProxyHandler({})).open(self.base_url + path, timeout=10)
        except HTTPError as error:
            response = error
        with response:
            body = response.read()
            self.assertIn('text/html', response.headers['Content-Type'])
            self.assertEqual(int(response.headers['Content-Length']), len(body))
            return response.status, body.decode('utf-8')

    def test_binary_build_and_report_cover_every_fixture_document(self):
        stats = json.loads((self.index / 'stats.json').read_text())
        self.assertEqual(stats['documents'], 121)
        self.assertGreater(stats['blocks'], 1)
        for filename, magic in [('dictionary.bin', b'IRDICT01'), ('postings.bin', b'IRPOST01'),
                                ('documents.bin', b'IRDOCS01'), ('meta.bin', b'IRMETA01')]:
            with (self.index / filename).open('rb') as file:
                self.assertEqual(file.read(8), magic)
                self.assertEqual(file.read(4), b'\x01\x00\x00\x00')
        answer = self.native_search('shared')
        self.assertEqual(answer['count'], 121)
        self.assertEqual(answer['ids'], list(range(121)))
        self.assertEqual([row['id'] for row in answer['documents']], list(range(121)))

    def test_boolean_results_match_explicit_document_rules(self):
        cases = {
            'dark matter': [i for i in range(121) if i % 2 == 0],
            '(dark || quark) !electron': [i for i in range(121) if i % 3 != 0],
            '!dark': [i for i in range(121) if i % 2 != 0],
            'dark || quark electron': [i for i in range(121) if i % 2 == 0 or (i % 2 and i % 3 == 0)],
            '!!dark': [i for i in range(121) if i % 2 == 0],
            'particle': list(range(121)),
            'nonexistent': [],
        }
        for query, expected in cases.items():
            with self.subTest(query=query):
                answer = self.native_search(query)
                self.assertEqual(answer['count'], len(expected))
                self.assertEqual(answer['ids'], expected)

    def test_cli_pages_have_fifty_items_and_preserve_total(self):
        for offset, expected in [(0, list(range(50))), (50, list(range(50, 100))), (100, list(range(100, 121)))]:
            with self.subTest(offset=offset):
                answer = self.native_search('shared', '--offset', str(offset), '--limit', '50')
                self.assertEqual(answer['count'], 121)
                self.assertEqual(answer['ids'], expected)
                self.assertEqual(len(answer['documents']), len(expected))

    def test_malformed_cli_query_does_not_hide_later_valid_query(self):
        result = subprocess.run([str(BINARY), 'search', str(self.index), '--json'],
                                input='(dark]\nshared\n', text=True, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 1)
        rows = [json.loads(line) for line in result.stdout.splitlines()]
        self.assertIn('error', rows[0])
        self.assertEqual(rows[1]['count'], 121)

    def test_http_home_contains_the_search_form(self):
        status, body = self.http_get('/')
        self.assertEqual(status, 200)
        self.assertIn('<form action="/search">', body)
        self.assertIn('name="q"', body)
        self.assertNotIn('<article>', body)

    def test_http_first_page_escapes_metadata_and_links_next_fifty(self):
        status, body = self.http_get('/search?' + urlencode({'q': 'shared'}))
        self.assertEqual(status, 200)
        self.assertEqual(body.count('<article>'), 50)
        self.assertIn('Найдено: 121', body)
        self.assertIn('Следующие 50', body)
        self.assertIn('page=2', body)
        self.assertIn('&lt;script&gt;alert(&quot;metadata&quot;)&lt;/script&gt;', body)
        self.assertIn('Journal &lt;physics&gt;', body)
        self.assertIn('?a=1&amp;b=2', body)
        self.assertNotIn('<script>', body)

    def test_http_second_and_last_page_have_correct_boundaries(self):
        status, body = self.http_get('/search?' + urlencode({'q': 'shared', 'page': 2}))
        self.assertEqual(status, 200)
        self.assertEqual(body.count('<article>'), 50)
        self.assertIn('Article 50', body)
        self.assertIn('Article 99', body)
        self.assertIn('Следующие 50', body)
        self.assertIn('Назад', body)
        status, body = self.http_get('/search?' + urlencode({'q': 'shared', 'page': 3}))
        self.assertEqual(status, 200)
        self.assertEqual(body.count('<article>'), 21)
        self.assertIn('Article 100', body)
        self.assertIn('Article 120', body)
        self.assertNotIn('Следующие 50', body)

    def test_http_query_value_is_escaped(self):
        status, body = self.http_get('/search?' + urlencode({'q': '<img src=x onerror="alert">'}))
        self.assertEqual(status, 200)
        self.assertNotIn('<img', body)
        self.assertIn('&lt;img src=x onerror=&quot;alert&quot;&gt;', body)

    def test_http_malformed_query_and_invalid_page_return_400(self):
        for values in [{'q': '(dark]'}, {'q': 'shared', 'page': 0}, {'q': 'shared', 'page': 'text'}]:
            with self.subTest(values=values):
                status, body = self.http_get('/search?' + urlencode(values))
                self.assertEqual(status, 400)
                self.assertIn('role="alert"', body)

    def test_http_missing_result_page_and_route_return_404(self):
        status, body = self.http_get('/search?' + urlencode({'q': 'shared', 'page': 4}))
        self.assertEqual(status, 404)
        self.assertEqual(body.count('<article>'), 0)
        status, _ = self.http_get('/does-not-exist')
        self.assertEqual(status, 404)

    def test_metrics_cli_matches_hand_calculated_graded_fixture(self):
        qrels = self.folder / 'fixture_qrels.tsv'
        run = self.folder / 'fixture_run.tsv'
        qrels.write_text('q1\ta\t3\nq1\tb\t1\nq2\tc\t2\n', encoding='utf-8')
        run.write_text('q1\tz\t1\nq1\ta\t2\nq1\tb\t4\n', encoding='utf-8')
        result = subprocess.run([str(BINARY), 'metrics', str(qrels), str(run), '5'],
                                text=True, capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr)
        answer = json.loads(result.stdout)
        self.assertEqual(answer['query_count'], 2)
        first = answer['queries'][0]['metrics']
        expected_dcg = 7 / math.log2(3) + 1 / math.log2(5)
        expected_ndcg = expected_dcg / (7 + 1 / math.log2(3))
        expected = {'p': 0.4, 'r': 1.0, 'ap': 0.5, 'dcg': expected_dcg,
                    'ndcg': expected_ndcg, 'err': 0.44140625}
        for name, value in expected.items():
            with self.subTest(metric=name):
                self.assertAlmostEqual(first[name], value, places=7)
                self.assertAlmostEqual(answer['average'][name], value / 2, places=7)
        self.assertTrue(all(value == 0 for value in answer['queries'][1]['metrics'].values()))

    def test_metrics_rejects_duplicate_rank_and_invalid_cutoff(self):
        qrels = self.folder / 'bad_qrels.tsv'
        run = self.folder / 'bad_run.tsv'
        qrels.write_text('q\ta\t3\nq\tb\t1\n', encoding='utf-8')
        run.write_text('q\ta\t1\nq\tb\t1\n', encoding='utf-8')
        duplicate = subprocess.run([str(BINARY), 'metrics', str(qrels), str(run), '5'],
                                   text=True, capture_output=True, timeout=10)
        self.assertNotEqual(duplicate.returncode, 0)
        run.write_text('q\ta\t1\n', encoding='utf-8')
        invalid = subprocess.run([str(BINARY), 'metrics', str(qrels), str(run), '5bad'],
                                 text=True, capture_output=True, timeout=10)
        self.assertNotEqual(invalid.returncode, 0)


if __name__ == '__main__':
    unittest.main(verbosity=2)
