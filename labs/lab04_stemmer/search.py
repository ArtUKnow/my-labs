import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lab03_tokenizer"))

from tokenizer import tokenize
from stemmer import stem
from lemmatizer import Lemmatizer, load_vocabulary


BASE_DIR = os.path.dirname(os.path.abspath(__file__))
TEXT_DIR = os.path.join(BASE_DIR, "..", "data", "text")
TOKENS_DIR = os.path.join(BASE_DIR, "..", "data", "tokens")

STOP_WORDS = ["в", "на", "и", "с", "по", "из", "к", "о", "об", "от", "для", "the", "a", "an", "in", "on", "of", "to", "and", "is", "are"]


def load_documents(text_dir):
    documents = []
    for source in sorted(os.listdir(text_dir)):
        source_dir = os.path.join(text_dir, source)
        if not os.path.isdir(source_dir):
            continue
        for name in sorted(os.listdir(source_dir)):
            if name.endswith(".txt"):
                text = open(os.path.join(source_dir, name), encoding="utf-8").read()
                doc_id = source + "/" + name[:-4]
                documents.append((doc_id, text))
    return documents


class SimpleSearch:
    def __init__(self, documents, mode, vocabulary=None):
        self.mode = mode
        self.lemmatizer = Lemmatizer(vocabulary)
        self.index = {}
        self.doc_ids = []
        self.doc_lengths = []
        for doc_id, text in documents:
            doc_num = len(self.doc_ids)
            self.doc_ids.append(doc_id)
            terms = self.prepare(text)
            self.doc_lengths.append(len(terms))
            for term in terms:
                if term not in self.index:
                    self.index[term] = {}
                self.index[term][doc_num] = self.index[term].get(doc_num, 0) + 1

    def normalize(self, token):
        if self.mode == "stem":
            return stem(token)
        if self.mode == "lemma":
            return self.lemmatizer.lemmatize(token)
        return token.replace("ё", "е")

    def prepare(self, text):
        result = []
        for token in tokenize(text):
            if token in STOP_WORDS:
                continue
            result.append(self.normalize(token))
        return result

    def search(self, query, limit=10):
        terms = self.prepare(query)
        scores = {}
        matched = {}
        for term in terms:
            postings = self.index.get(term, {})
            for doc_num in postings:
                tf = postings[doc_num]
                scores[doc_num] = scores.get(doc_num, 0) + tf / self.doc_lengths[doc_num] * 1000
                matched[doc_num] = matched.get(doc_num, 0) + 1
        ranked = []
        for doc_num in scores:
            ranked.append((matched[doc_num], scores[doc_num], self.doc_ids[doc_num]))
        ranked.sort(key=lambda item: (-item[0], -item[1], item[2]))
        result = []
        for matched_count, score, doc_id in ranked[:limit]:
            result.append(doc_id)
        return result


def main():
    mode = "raw"
    if len(sys.argv) > 1:
        mode = sys.argv[1]
    documents = load_documents(TEXT_DIR)
    vocabulary = load_vocabulary(TOKENS_DIR)
    engine = SimpleSearch(documents, mode, vocabulary)
    for line in sys.stdin:
        query = line.strip()
        if query == "":
            continue
        print("query:", query)
        for position, doc_id in enumerate(engine.search(query)):
            print("  %d. %s" % (position + 1, doc_id))


if __name__ == "__main__":
    main()
