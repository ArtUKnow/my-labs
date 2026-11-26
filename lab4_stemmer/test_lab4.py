import os
import sys
import unittest
import subprocess

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from stemmer import stem, stem_russian, stem_english, find_regions
from lemmatizer import Lemmatizer
from search import SimpleSearch
import compare_quality


class TestRegions(unittest.TestCase):
    def test_rv_r1_r2(self):
        rv, r1, r2 = find_regions("противоестественном")
        self.assertEqual(rv, 3)
        self.assertEqual(r1, 4)
        self.assertEqual(r2, 6)


class TestRussianStemmer(unittest.TestCase):
    def test_noun_forms_same_stem(self):
        forms = ["ракета", "ракеты", "ракете", "ракету", "ракетой", "ракетами", "ракетах"]
        stems = set(stem_russian(w) for w in forms)
        self.assertEqual(stems, {"ракет"})

    def test_adjective_forms(self):
        forms = ["космический", "космическая", "космические", "космического", "космическим"]
        stems = set(stem_russian(w) for w in forms)
        self.assertEqual(len(stems), 1)

    def test_verb_forms(self):
        self.assertEqual(stem_russian("летала"), "лета")
        self.assertEqual(stem_russian("летать"), "лета")

    def test_reflexive(self):
        self.assertEqual(stem_russian("вращается"), "враща")

    def test_derivational(self):
        self.assertEqual(stem_russian("невесомость"), "невесом")

    def test_yo_replaced(self):
        self.assertEqual(stem_russian("полёт"), "полет")

    def test_short_word(self):
        self.assertEqual(stem_russian("и"), "и")
        self.assertEqual(stem_russian("в"), "в")

    def test_overstemming_example(self):
        self.assertEqual(stem_russian("орбите"), "орб")
        self.assertEqual(stem_russian("орбиты"), "орбит")

    def test_superlative(self):
        self.assertEqual(stem_russian("быстрейший"), "быстр")


class TestEnglishStemmer(unittest.TestCase):
    def test_plural(self):
        self.assertEqual(stem_english("rockets"), "rocket")
        self.assertEqual(stem_english("flies"), "fli")

    def test_ing_ed(self):
        self.assertEqual(stem_english("landing"), "land")
        self.assertEqual(stem_english("launched"), "launch")

    def test_short_not_touched(self):
        self.assertEqual(stem_english("gas"), "gas")
        self.assertEqual(stem_english("sing"), "sing")

    def test_dispatch(self):
        self.assertEqual(stem("Rockets"), "rocket")
        self.assertEqual(stem("Ракеты"), "ракет")


class TestLemmatizer(unittest.TestCase):
    def setUp(self):
        vocabulary = {
            "ракета": 16, "ракеты": 74, "ракет": 31, "ракете": 4, "ракету": 8, "ракетой": 3, "ракетами": 7,
            "спутник": 20, "спутника": 10, "спутников": 15, "спутники": 12,
            "космический": 30, "космическая": 20, "космические": 10, "космического": 25,
            "орбита": 10, "орбиты": 20, "орбите": 15,
            "rocket": 10, "rockets": 5,
        }
        self.lemmatizer = Lemmatizer(vocabulary)

    def test_feminine_noun(self):
        for form in ["ракеты", "ракет", "ракете", "ракетами", "ракета"]:
            self.assertEqual(self.lemmatizer.lemmatize(form), "ракета", form)

    def test_masculine_noun(self):
        for form in ["спутника", "спутников", "спутники", "спутник"]:
            self.assertEqual(self.lemmatizer.lemmatize(form), "спутник", form)

    def test_adjective(self):
        for form in ["космическая", "космические", "космического"]:
            self.assertEqual(self.lemmatizer.lemmatize(form), "космический", form)

    def test_exceptions(self):
        self.assertEqual(self.lemmatizer.lemmatize("люди"), "человек")
        self.assertEqual(self.lemmatizer.lemmatize("were"), "be")

    def test_english(self):
        self.assertEqual(self.lemmatizer.lemmatize("rockets"), "rocket")

    def test_unknown_word_falls_to_stem(self):
        self.assertEqual(self.lemmatizer.lemmatize("невесомости"), "невесом")

    def test_cache(self):
        self.lemmatizer.lemmatize("ракеты")
        self.assertIn("ракеты", self.lemmatizer.cache)


class TestSearch(unittest.TestCase):
    def setUp(self):
        self.documents = [
            ("d1", "Ракета вышла на орбиту. Ракеты летают."),
            ("d2", "Спутник вышел на орбиты Земли"),
            ("d3", "Про собак и кошек"),
        ]

    def test_raw_search_exact_form(self):
        engine = SimpleSearch(self.documents, "raw")
        self.assertEqual(engine.search("орбиту"), ["d1"])

    def test_stem_search_all_forms(self):
        engine = SimpleSearch(self.documents, "stem")
        self.assertEqual(sorted(engine.search("орбиту")), ["d1", "d2"])

    def test_ranking_by_matched_terms(self):
        engine = SimpleSearch(self.documents, "stem")
        self.assertEqual(engine.search("ракеты орбита")[0], "d1")

    def test_stop_words_ignored(self):
        engine = SimpleSearch(self.documents, "raw")
        self.assertEqual(engine.search("на"), [])

    def test_no_results(self):
        engine = SimpleSearch(self.documents, "lemma")
        self.assertEqual(engine.search("марс"), [])


class TestMetrics(unittest.TestCase):
    def test_precision(self):
        self.assertEqual(compare_quality.precision_at_k(["a", "b", "c"], {"a": 1, "c": 2}, 3), 2 / 3)

    def test_recall(self):
        self.assertEqual(compare_quality.recall_at_k(["a"], {"a": 1, "b": 1}, 5), 0.5)

    def test_recall_empty(self):
        self.assertEqual(compare_quality.recall_at_k(["a"], {}, 5), 0)


class TestCli(unittest.TestCase):
    def test_stemmer_stdin(self):
        script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "stemmer.py")
        result = subprocess.run([sys.executable, script], input="ракеты\nспутников\n".encode("utf-8"), stdout=subprocess.PIPE)
        self.assertEqual(result.stdout.decode("utf-8"), "ракет\nспутник\n")


class TestFullCorpusBatch(unittest.TestCase):
    def test_all_documents_compete_with_identical_ranking(self):
        import gzip
        import tempfile
        from collections import Counter
        from pathlib import Path
        from corpus_evaluation import retrieve
        documents = [("scoap3/a", "particles particle field"),
                     ("scoap3/b", "particle field field"),
                     ("scoap3/c", "dark matter particle"),
                     ("scoap3/unlabeled", "particle")]
        queries = [("q1", "particle"), ("q2", "field field"), ("q3", "missing")]
        vocabulary = Counter(word for _, text in documents for word in text.split())
        with tempfile.TemporaryDirectory() as folder:
            rows, paths = [], []
            for doc_id, text in documents:
                name = doc_id.split("/")[1] + ".txt.gz"
                path = Path(folder) / name
                with gzip.open(path, "wt") as file:
                    file.write("\n".join(text.split()))
                rows.append({"file": name})
                paths.append(path)
            runs, sizes = retrieve(rows, paths, queries, k=4)
        for mode in ["raw", "stem", "lemma"]:
            engine = SimpleSearch(documents, mode, vocabulary)
            self.assertEqual(sizes[mode], len(engine.index))
            for query_id, text in queries:
                self.assertEqual(runs[mode][query_id], engine.search(text, 4))
            self.assertIn("scoap3/unlabeled", runs[mode]["q1"])

    def test_known_item_protocol_covers_each_journal(self):
        from corpus_evaluation import manifest_rows, prepare_queries, QUERY_DIR
        import json
        rows = manifest_rows()
        queries = prepare_queries(rows)
        protocol = json.loads((QUERY_DIR / "protocol.json").read_text())
        self.assertEqual(protocol["corpus_documents"], len(rows))
        self.assertEqual({q["journal"] for q in protocol["queries"]}, {row["journal"] for row in rows})
        expected = {"scoap3/" + row["file"].removesuffix(".txt.gz") for row in rows}
        self.assertTrue(all(q["target"] in expected for q in protocol["queries"]))
        self.assertEqual(len(queries), len(protocol["queries"]))


if __name__ == "__main__":
    unittest.main(verbosity=2)
