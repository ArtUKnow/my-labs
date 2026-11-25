import os
import sys
import json
import struct

sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lab03_tokenizer"))
sys.path.insert(0, os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lab04_stemmer"))

from tokenizer import tokenize
from stemmer import stem


BASE_DIR = os.path.dirname(os.path.abspath(__file__))
INDEX_DIR = os.path.join(BASE_DIR, "..", "data", "index")


def intersect(a, b):
    result = []
    i = 0
    j = 0
    while i < len(a) and j < len(b):
        if a[i] == b[j]:
            result.append(a[i])
            i += 1
            j += 1
        elif a[i] < b[j]:
            i += 1
        else:
            j += 1
    return result


class BooleanIndex:
    def __init__(self, index_dir):
        self.index_dir = index_dir
        self.dictionary = {}
        self.doc_names = []
        self.mode = "raw"
        for line in open(os.path.join(index_dir, "dictionary.txt"), encoding="utf-8"):
            term, df, offset, length = line.rstrip("\n").split("\t")
            self.dictionary[term] = (int(df), int(offset), int(length))
        for line in open(os.path.join(index_dir, "docs.txt"), encoding="utf-8"):
            doc_id, name = line.rstrip("\n").split("\t")
            self.doc_names.append(name)
        meta_path = os.path.join(index_dir, "meta.txt")
        if os.path.exists(meta_path):
            for line in open(meta_path, encoding="utf-8"):
                key, value = line.rstrip("\n").split("\t")
                if key == "mode":
                    self.mode = value
        self.postings_file = open(os.path.join(index_dir, "postings.bin"), "rb")

    def normalize(self, token):
        if self.mode == "stem":
            return stem(token)
        return token.lower().replace("ё", "е")

    def document_frequency(self, term):
        term = self.normalize(term)
        if term not in self.dictionary:
            return 0
        return self.dictionary[term][0]

    def get_postings(self, term):
        term = self.normalize(term)
        if term not in self.dictionary:
            return []
        df, offset, length = self.dictionary[term]
        self.postings_file.seek(offset)
        data = self.postings_file.read(length)
        return list(struct.unpack("<%dI" % df, data))

    def search_and(self, query):
        terms = tokenize(query)
        if len(terms) == 0:
            return []
        lists = []
        for term in terms:
            lists.append(self.get_postings(term))
        lists.sort(key=len)
        result = lists[0]
        for other in lists[1:]:
            result = intersect(result, other)
            if len(result) == 0:
                break
        return result

    def names(self, doc_ids):
        return [self.doc_names[d] for d in doc_ids]

    def close(self):
        self.postings_file.close()


def main():
    index_dir = INDEX_DIR
    as_json = False
    for arg in sys.argv[1:]:
        if arg == "--json":
            as_json = True
        else:
            index_dir = arg
    index = BooleanIndex(index_dir)
    for line in sys.stdin:
        query = line.strip()
        if query == "":
            continue
        doc_ids = index.search_and(query)
        if as_json:
            terms = []
            for token in tokenize(query):
                terms.append({"token": token, "term": index.normalize(token), "df": index.document_frequency(token)})
            print(json.dumps({"query": query, "terms": terms, "count": len(doc_ids), "documents": index.names(doc_ids)}, ensure_ascii=False))
        else:
            print("query:", query, "->", len(doc_ids), "documents")
            for name in index.names(doc_ids):
                print("  " + name)
    index.close()


if __name__ == "__main__":
    main()
