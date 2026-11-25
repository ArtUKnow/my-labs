import os
import sys
import math
import json
import shutil
import tempfile
import unittest

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import zipf
import plot_svg


def make_zipf_tokens(c, s, n):
    tokens = []
    for rank in range(1, n + 1):
        count = int(round(c / (rank ** s)))
        for i in range(count):
            tokens.append("w%d" % rank)
    return tokens


class TestCounting(unittest.TestCase):
    def test_count_from_tokens(self):
        freq = zipf.count_from_tokens(["a", "b", "a", "c", "a", "b"])
        self.assertEqual(freq, {"a": 3, "b": 2, "c": 1})

    def test_ranked_order(self):
        ranked = zipf.ranked_list({"a": 3, "b": 2, "c": 1, "d": 2})
        self.assertEqual(ranked, [("a", 3), ("b", 2), ("d", 2), ("c", 1)])

    def test_count_frequencies_from_dir(self):
        tmp = tempfile.mkdtemp()
        os.makedirs(os.path.join(tmp, "src"))
        f = open(os.path.join(tmp, "src", "a.txt"), "w", encoding="utf-8")
        f.write("x\ny\nx\n\n")
        f.close()
        freq = zipf.count_frequencies(tmp)
        shutil.rmtree(tmp)
        self.assertEqual(freq, {"x": 2, "y": 1})


class TestFit(unittest.TestCase):
    def test_zipf_fit_ideal(self):
        tokens = make_zipf_tokens(1000, 1.0, 200)
        ranked = zipf.ranked_list(zipf.count_from_tokens(tokens))
        c, s = zipf.fit_zipf(ranked)
        self.assertTrue(abs(s - 1.0) < 0.1, s)
        self.assertTrue(abs(c - 1000) / 1000 < 0.15, c)

    def test_zipf_fit_other_slope(self):
        tokens = make_zipf_tokens(5000, 1.3, 100)
        ranked = zipf.ranked_list(zipf.count_from_tokens(tokens))
        c, s = zipf.fit_zipf(ranked)
        self.assertTrue(abs(s - 1.3) < 0.1, s)

    def test_error_zero_on_exact(self):
        ranked = [("a", 100), ("b", 50), ("c", 25)]
        error = zipf.zipf_error(ranked, 100, 1.0, 0)
        self.assertTrue(error > 0)
        exact = [("a", 100), ("b", 50)]
        self.assertTrue(zipf.zipf_error(exact, 100, 1.0, 0) < 0.0001)

    def test_mandelbrot_not_worse_than_zipf(self):
        tokens = make_zipf_tokens(2000, 1.1, 300)
        ranked = zipf.ranked_list(zipf.count_from_tokens(tokens))
        c, s = zipf.fit_zipf(ranked)
        mc, ms, mb, m_error = zipf.fit_mandelbrot(ranked)
        self.assertTrue(m_error <= zipf.zipf_error(ranked, c, s, 0) + 0.0001)


class TestAnalyze(unittest.TestCase):
    def test_analyze_fields(self):
        tokens = make_zipf_tokens(500, 1.0, 50)
        result = zipf.analyze(zipf.count_from_tokens(tokens))
        self.assertEqual(result["unique_terms"], 50)
        self.assertEqual(result["total_tokens"], len(tokens))
        self.assertEqual(result["top_terms"][0]["term"], "w1")
        self.assertEqual(result["points"][0]["rank"], 1)
        json.dumps(result)

    def test_hapax(self):
        result = zipf.analyze({"a": 5, "b": 1, "c": 1, "d": 2})
        self.assertEqual(result["hapax_legomena"], 2)
        self.assertEqual(result["hapax_share"], 0.5)

    def test_points_thinned(self):
        tokens = make_zipf_tokens(3000, 1.0, 2000)
        result = zipf.analyze(zipf.count_from_tokens(tokens))
        self.assertTrue(len(result["points"]) < 2000)
        self.assertTrue(len(result["points"]) > 100)


class TestSvg(unittest.TestCase):
    def test_svg_generated(self):
        tokens = make_zipf_tokens(500, 1.0, 50)
        result = zipf.analyze(zipf.count_from_tokens(tokens))
        svg = plot_svg.make_svg(result)
        self.assertTrue(svg.startswith("<svg"))
        self.assertTrue(svg.endswith("</svg>"))
        self.assertEqual(svg.count("<polyline"), 3)

    def test_scale(self):
        scale, low, high = plot_svg.make_scale([1, 10, 100], 300, 50)
        self.assertEqual(low, 0)
        self.assertEqual(high, 2)
        self.assertEqual(scale(1), 50)
        self.assertEqual(scale(100), 250)
        self.assertEqual(scale(10), 150)


if __name__ == "__main__":
    unittest.main(verbosity=2)
