import os
import sys
import math
import unittest
import subprocess

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import metrics
import evaluate


GRADES = {"a": 3, "b": 2, "c": 1, "d": 0}


class TestPrecisionRecall(unittest.TestCase):
    def test_precision(self):
        self.assertEqual(metrics.precision_at_k(["a", "x", "b", "y", "z"], GRADES, 5), 0.4)

    def test_precision_short_results(self):
        self.assertEqual(metrics.precision_at_k(["a"], GRADES, 5), 0.2)

    def test_recall(self):
        self.assertEqual(metrics.recall_at_k(["a", "x", "b"], GRADES, 3), 2 / 3)

    def test_recall_no_relevant(self):
        self.assertEqual(metrics.recall_at_k(["a"], {}, 3), 0.0)

    def test_zero_grade_not_relevant(self):
        self.assertEqual(metrics.precision_at_k(["d"], GRADES, 1), 0.0)


class TestAveragePrecision(unittest.TestCase):
    def test_perfect(self):
        self.assertEqual(metrics.average_precision(["a", "b", "c"], GRADES, 3), 1.0)

    def test_partial(self):
        ap = metrics.average_precision(["x", "a", "y", "b"], GRADES, 4)
        self.assertTrue(abs(ap - (0.5 + 0.5) / 3) < 0.0001)

    def test_empty(self):
        self.assertEqual(metrics.average_precision([], GRADES, 5), 0.0)


class TestDcg(unittest.TestCase):
    def test_dcg_single(self):
        self.assertEqual(metrics.dcg_at_k(["a"], GRADES, 1), 7.0)

    def test_dcg_two(self):
        expected = 7.0 + 3.0 / math.log2(3)
        self.assertTrue(abs(metrics.dcg_at_k(["a", "b"], GRADES, 2) - expected) < 0.0001)

    def test_ideal_dcg(self):
        self.assertTrue(abs(metrics.ideal_dcg_at_k(GRADES, 2) - (7.0 + 3.0 / math.log2(3))) < 0.0001)

    def test_ndcg_perfect(self):
        self.assertTrue(abs(metrics.ndcg_at_k(["a", "b", "c"], GRADES, 3) - 1.0) < 0.0001)

    def test_ndcg_reversed_less(self):
        self.assertTrue(metrics.ndcg_at_k(["c", "b", "a"], GRADES, 3) < 1.0)

    def test_ndcg_no_relevant(self):
        self.assertEqual(metrics.ndcg_at_k(["a"], {}, 3), 0.0)

    def test_ndcg_between_zero_and_one(self):
        value = metrics.ndcg_at_k(["x", "c", "y", "a"], GRADES, 4)
        self.assertTrue(0 < value < 1)


class TestErr(unittest.TestCase):
    def test_err_single_max(self):
        self.assertEqual(metrics.err_at_k(["a"], GRADES, 1), 7 / 8)

    def test_err_two(self):
        first = 7 / 8
        second = (1 - 7 / 8) * (3 / 8) / 2
        self.assertTrue(abs(metrics.err_at_k(["a", "b"], GRADES, 2) - (first + second)) < 0.0001)

    def test_err_order_matters(self):
        self.assertTrue(metrics.err_at_k(["a", "c"], GRADES, 2) > metrics.err_at_k(["c", "a"], GRADES, 2))

    def test_err_empty(self):
        self.assertEqual(metrics.err_at_k([], GRADES, 5), 0.0)


class TestAverage(unittest.TestCase):
    def test_all_metrics_keys(self):
        row = metrics.all_metrics(["a"], GRADES, 5)
        self.assertEqual(sorted(row.keys()), ["ap", "dcg", "err", "ndcg", "p", "r"])

    def test_average(self):
        avg = metrics.average_metrics([{"p": 1.0, "r": 0.0}, {"p": 0.0, "r": 1.0}])
        self.assertEqual(avg, {"p": 0.5, "r": 0.5})

    def test_average_empty(self):
        self.assertEqual(metrics.average_metrics([]), {})


class TestRunFiles(unittest.TestCase):
    def test_load_run_sorted(self):
        run = evaluate.load_run(["q1\tb\t2", "q1\ta\t1", "q2\tc\t1", ""])
        self.assertEqual(run, {"q1": ["a", "b"], "q2": ["c"]})

    def test_evaluate_run(self):
        queries = [("q1", "one"), ("q2", "two")]
        qrels = {"q1": {"a": 3}, "q2": {"c": 1}}
        run = {"q1": ["a"], "q2": ["x"]}
        result = evaluate.evaluate_run(run, queries, qrels, 5)
        self.assertEqual(result["average"]["p"], 0.1)
        self.assertEqual(result["queries"][0]["metrics"]["ndcg"], 1.0)
        self.assertEqual(result["queries"][1]["metrics"]["ndcg"], 0.0)

    def test_real_qrels_match_queries(self):
        queries = evaluate.load_queries(evaluate.QUERIES_PATH)
        qrels = evaluate.load_qrels(evaluate.QRELS_PATH)
        for query_id, text in queries:
            self.assertIn(query_id, qrels)
            self.assertTrue(len(qrels[query_id]) > 0)

    def test_cli_stdin(self):
        script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "evaluate.py")
        qrels = evaluate.load_qrels(evaluate.QRELS_PATH)
        target = next(iter(qrels["q1"]))
        data = "q1\t" + target + "\t1\n"
        result = subprocess.run([sys.executable, script, "--stdin"], input=data.encode("utf-8"), stdout=subprocess.PIPE)
        output = result.stdout.decode("utf-8")
        self.assertIn("NDCG=1.000", output)
        self.assertIn("q1", output)


class TestSharedCorpusEvaluation(unittest.TestCase):
    def test_artifact_uses_every_primary_document(self):
        import json
        from pathlib import Path
        sys.path.insert(0, str(Path(__file__).resolve().parent.parent / "lab4_stemmer"))
        from corpus_evaluation import manifest_rows, RUNS_PATH, QUERY_DIR
        corpus = manifest_rows()
        report = json.loads((Path(__file__).parent / "results" / "metrics.json").read_text())
        runs = json.loads(RUNS_PATH.read_text())
        self.assertEqual(report["evaluation_documents"], len(corpus))
        self.assertEqual(runs["evaluation_documents"], len(corpus))
        self.assertEqual(report["task"], "known_item_title_retrieval")
        self.assertEqual({system["name"] for system in report["systems"]}, {"raw", "stem", "lemma"})
        ids = {"scoap3/" + row["file"].removesuffix(".txt.gz") for row in corpus}
        for system in report["systems"]:
            self.assertEqual(len(system["queries"]), report["query_count"])
            for query in system["queries"]:
                self.assertTrue(set(query["results"]).issubset(ids))


if __name__ == "__main__":
    unittest.main(verbosity=2)
