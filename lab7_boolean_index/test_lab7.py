import os
import sys
import json
import shutil
import struct
import tempfile
import unittest
import subprocess

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

import build_index
import index_reader


DOCS = {
    "a/doc1": "Ракета летит в космос. Ракета большая.",
    "a/doc2": "Спутник летит по орбите",
    "b/doc3": "Космос и спутник",
    "b/doc4": "Собака Лайка",
}


def make_corpus(folder):
    for doc_id in DOCS:
        source, name = doc_id.split("/")
        os.makedirs(os.path.join(folder, source), exist_ok=True)
        f = open(os.path.join(folder, source, name + ".txt"), "w", encoding="utf-8")
        f.write(DOCS[doc_id])
        f.close()


class TestMerge(unittest.TestCase):
    def test_merge_sorted_lists(self):
        self.assertEqual(build_index.merge_sorted_lists([1, 3, 5], [2, 3, 6]), [1, 2, 3, 5, 6])

    def test_merge_empty(self):
        self.assertEqual(build_index.merge_sorted_lists([], [1]), [1])
        self.assertEqual(build_index.merge_sorted_lists([], []), [])

    def test_intersect(self):
        self.assertEqual(index_reader.intersect([1, 2, 3, 7], [2, 3, 4, 7]), [2, 3, 7])
        self.assertEqual(index_reader.intersect([1], [2]), [])


class TestBuild(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.text = os.path.join(self.tmp, "text")
        make_corpus(self.text)

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def build(self, mode="raw", block_limit=1000000):
        index_dir = os.path.join(self.tmp, "index_" + mode + "_" + str(block_limit))
        stats = build_index.build_index(self.text, index_dir, mode, block_limit)
        return index_dir, stats

    def test_files_created(self):
        index_dir, stats = self.build()
        for name in ["dictionary.txt", "postings.bin", "docs.txt", "meta.txt"]:
            self.assertTrue(os.path.exists(os.path.join(index_dir, name)), name)
        self.assertFalse(os.path.exists(os.path.join(index_dir, "blocks")))

    def test_stats(self):
        index_dir, stats = self.build()
        self.assertEqual(stats["documents"], 4)
        self.assertEqual(stats["blocks"], 1)
        self.assertEqual(stats["postings_bytes"], stats["postings"] * 4)
        self.assertEqual(stats["tokens"], 15)

    def test_dictionary_sorted(self):
        index_dir, stats = self.build()
        terms = [line.split("\t")[0] for line in open(os.path.join(index_dir, "dictionary.txt"), encoding="utf-8")]
        self.assertEqual(terms, sorted(terms))
        self.assertEqual(len(terms), len(set(terms)))

    def test_postings_raw(self):
        index_dir, stats = self.build()
        index = index_reader.BooleanIndex(index_dir)
        self.assertEqual(index.get_postings("ракета"), [0])
        self.assertEqual(index.get_postings("космос"), [0, 2])
        self.assertEqual(index.get_postings("спутник"), [1, 2])
        self.assertEqual(index.get_postings("летит"), [0, 1])
        self.assertEqual(index.get_postings("нету"), [])
        self.assertEqual(index.document_frequency("летит"), 2)
        index.close()

    def test_postings_stem(self):
        index_dir, stats = self.build("stem")
        index = index_reader.BooleanIndex(index_dir)
        self.assertEqual(index.mode, "stem")
        self.assertEqual(index.get_postings("спутники"), [1, 2])
        self.assertEqual(index.get_postings("ракеты"), [0])
        index.close()

    def test_multi_block_same_result(self):
        single_dir, single_stats = self.build("raw", 1000000)
        multi_dir, multi_stats = self.build("raw", 3)
        self.assertTrue(multi_stats["blocks"] > 1)
        single = open(os.path.join(single_dir, "dictionary.txt"), encoding="utf-8").read()
        multi = open(os.path.join(multi_dir, "dictionary.txt"), encoding="utf-8").read()
        self.assertEqual(single, multi)
        single_bin = open(os.path.join(single_dir, "postings.bin"), "rb").read()
        multi_bin = open(os.path.join(multi_dir, "postings.bin"), "rb").read()
        self.assertEqual(single_bin, multi_bin)

    def test_binary_format(self):
        index_dir, stats = self.build()
        index = index_reader.BooleanIndex(index_dir)
        df, offset, length = index.dictionary["космос"]
        raw = open(os.path.join(index_dir, "postings.bin"), "rb").read()
        self.assertEqual(struct.unpack("<%dI" % df, raw[offset:offset + length]), (0, 2))
        index.close()

    def test_and_search(self):
        index_dir, stats = self.build()
        index = index_reader.BooleanIndex(index_dir)
        self.assertEqual(index.names(index.search_and("спутник летит")), ["a/doc2"])
        self.assertEqual(index.names(index.search_and("космос спутник")), ["b/doc3"])
        self.assertEqual(index.search_and("космос собака"), [])
        self.assertEqual(index.search_and(""), [])
        index.close()

    def test_doc_names(self):
        index_dir, stats = self.build()
        index = index_reader.BooleanIndex(index_dir)
        self.assertEqual(index.doc_names, ["a/doc1", "a/doc2", "b/doc3", "b/doc4"])
        index.close()

    def test_cli(self):
        index_dir, stats = self.build()
        script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "index_reader.py")
        result = subprocess.run([sys.executable, script, index_dir, "--json"], input="космос\n".encode("utf-8"), stdout=subprocess.PIPE)
        data = json.loads(result.stdout.decode("utf-8"))
        self.assertEqual(data["count"], 2)
        self.assertEqual(data["documents"], ["a/doc1", "b/doc3"])


if __name__ == "__main__":
    unittest.main(verbosity=2)
