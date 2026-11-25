import os
import sys
import shutil
import tempfile
import unittest
import subprocess

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from tokenizer import tokenize, token_kind
import tokenize_corpus


class TestTokenizer(unittest.TestCase):
    def test_simple_words(self):
        self.assertEqual(tokenize("Ракета летит в космос"), ["ракета", "летит", "в", "космос"])

    def test_punctuation(self):
        self.assertEqual(tokenize("Привет, мир! Как дела?"), ["привет", "мир", "как", "дела"])

    def test_lowercase(self):
        self.assertEqual(tokenize("NASA и Роскосмос"), ["nasa", "и", "роскосмос"])

    def test_hyphen_inside_word(self):
        self.assertEqual(tokenize("научно-технический прогресс"), ["научно-технический", "прогресс"])

    def test_hyphen_with_digits(self):
        self.assertEqual(tokenize("ракета Союз-2 и Р-7"), ["ракета", "союз-2", "и", "р-7"])

    def test_dash_as_separator(self):
        self.assertEqual(tokenize("космос - это круто"), ["космос", "это", "круто"])
        self.assertEqual(tokenize("слово- другое"), ["слово", "другое"])

    def test_apostrophe(self):
        self.assertEqual(tokenize("don't stop"), ["don't", "stop"])

    def test_decimal_numbers(self):
        self.assertEqual(tokenize("скорость 7.9 км/с и 1,5 тонны"), ["скорость", "7.9", "км", "с", "и", "1,5", "тонны"])

    def test_dot_end_of_sentence(self):
        self.assertEqual(tokenize("В 1961.Гагарин"), ["в", "1961", "гагарин"])

    def test_dates(self):
        self.assertEqual(tokenize("12 апреля 1961 года"), ["12", "апреля", "1961", "года"])

    def test_empty(self):
        self.assertEqual(tokenize(""), [])
        self.assertEqual(tokenize("   \n\t "), [])

    def test_only_punctuation(self):
        self.assertEqual(tokenize("... --- !!!"), [])

    def test_unicode_letters(self):
        self.assertEqual(tokenize("Ёлка ёж straße"), ["ёлка", "ёж", "straße"])

    def test_mixed_scripts(self):
        self.assertEqual(tokenize("аппарат Voyager 1"), ["аппарат", "voyager", "1"])

    def test_token_kind(self):
        self.assertEqual(token_kind("слово"), "word")
        self.assertEqual(token_kind("1961"), "number")
        self.assertEqual(token_kind("союз-2"), "mixed")


class TestCli(unittest.TestCase):
    def test_stdin_stdout(self):
        script = os.path.join(os.path.dirname(os.path.abspath(__file__)), "tokenizer.py")
        result = subprocess.run(
            [sys.executable, script],
            input="Первый полёт, 1961 год.".encode("utf-8"),
            stdout=subprocess.PIPE,
        )
        lines = result.stdout.decode("utf-8").split("\n")
        self.assertEqual(lines, ["первый", "полёт", "1961", "год", ""])


class TestCorpusRun(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.mkdtemp()
        self.text = os.path.join(self.tmp, "text", "src")
        os.makedirs(self.text)
        f = open(os.path.join(self.text, "a.txt"), "w", encoding="utf-8")
        f.write("Один два три. Четыре 5")
        f.close()
        f = open(os.path.join(self.text, "b.txt"), "w", encoding="utf-8")
        f.write("Ракета-носитель Союз-2")
        f.close()

    def tearDown(self):
        shutil.rmtree(self.tmp)

    def test_stats(self):
        stats = tokenize_corpus.tokenize_corpus(os.path.join(self.tmp, "text"), os.path.join(self.tmp, "tokens"))
        self.assertEqual(stats["documents"], 2)
        self.assertEqual(stats["tokens"], 7)
        self.assertEqual(stats["kinds"]["number"], 1)
        self.assertEqual(stats["kinds"]["mixed"], 1)
        self.assertTrue(stats["kb_per_second"] > 0)
        out = open(os.path.join(self.tmp, "tokens", "src", "b.txt"), encoding="utf-8").read()
        self.assertEqual(out, "ракета-носитель\nсоюз-2")


if __name__ == "__main__":
    unittest.main(verbosity=2)
