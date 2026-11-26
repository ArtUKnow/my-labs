"""Boolean query parser and sorted-posting evaluation for the lab 7 index."""

import os
import sys

sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "lab3_tokenizer"))
sys.path.insert(0, os.path.join(os.path.dirname(__file__), "..", "lab7_boolean_index"))

from tokenizer import tokenize
from index_reader import BooleanIndex, intersect


class QuerySyntaxError(ValueError):
    pass


def lex(query):
    result = []
    i = 0
    while i < len(query):
        char = query[i]
        if char.isspace():
            i += 1
        elif query.startswith("&&", i):
            result.append(("AND", "&&"))
            i += 2
        elif query.startswith("||", i):
            result.append(("OR", "||"))
            i += 2
        elif char in "!([":
            result.append(("NOT" if char == "!" else "LPAREN", char))
            i += 1
        elif char in ")]":
            result.append(("RPAREN", char))
            i += 1
        elif char in "&|":
            raise QuerySyntaxError("Используйте && для И и || для ИЛИ")
        else:
            start = i
            while i < len(query) and not query[i].isspace() and query[i] not in "&|!()[]":
                i += 1
            for term in tokenize(query[start:i]):
                result.append(("TERM", term))
    return result


class Parser:
    def __init__(self, tokens):
        self.tokens = tokens
        self.position = 0

    def peek(self):
        if self.position < len(self.tokens):
            return self.tokens[self.position][0]
        return None

    def take(self):
        token = self.tokens[self.position]
        self.position += 1
        return token

    def parse(self):
        if not self.tokens:
            return None
        tree = self.parse_or()
        if self.peek() is not None:
            raise QuerySyntaxError("Лишний оператор или закрывающая скобка")
        return tree

    def parse_or(self):
        children = [self.parse_and()]
        while self.peek() == "OR":
            self.take()
            children.append(self.parse_and())
        return children[0] if len(children) == 1 else ("OR", children)

    def parse_and(self):
        children = [self.parse_not()]
        while self.peek() in {"AND", "TERM", "NOT", "LPAREN"}:
            if self.peek() == "AND":
                self.take()
            children.append(self.parse_not())
        return children[0] if len(children) == 1 else ("AND", children)

    def parse_not(self):
        if self.peek() == "NOT":
            self.take()
            return ("NOT", self.parse_not())
        return self.parse_atom()

    def parse_atom(self):
        kind = self.peek()
        if kind == "TERM":
            return self.take()
        if kind == "LPAREN":
            opening = self.take()[1]
            closing = ")" if opening == "(" else "]"
            if self.peek() == "RPAREN":
                raise QuerySyntaxError("Пустые скобки")
            tree = self.parse_or()
            if self.peek() != "RPAREN" or self.tokens[self.position][1] != closing:
                raise QuerySyntaxError("Не закрыта скобка")
            self.take()
            return tree
        raise QuerySyntaxError("Ожидалось слово или открывающая скобка")


def parse_query(query):
    return Parser(lex(query)).parse()


def union(a, b):
    result = []
    i = j = 0
    while i < len(a) and j < len(b):
        if a[i] < b[j]:
            result.append(a[i])
            i += 1
        elif b[j] < a[i]:
            result.append(b[j])
            j += 1
        else:
            result.append(a[i])
            i += 1
            j += 1
    result.extend(a[i:])
    result.extend(b[j:])
    return result


def difference(a, b):
    result = []
    i = j = 0
    while i < len(a) and j < len(b):
        if a[i] < b[j]:
            result.append(a[i])
            i += 1
        elif b[j] < a[i]:
            j += 1
        else:
            i += 1
            j += 1
    result.extend(a[i:])
    return result


class BooleanSearch:
    def __init__(self, index_dir):
        self.index = BooleanIndex(index_dir)
        self.document_count = len(self.index.doc_names)

    def search(self, query):
        tree = parse_query(query)
        if tree is None:
            return []
        cache = {}
        return self._evaluate(tree, cache)

    def _evaluate(self, tree, cache):
        kind = tree[0]
        if kind == "TERM":
            term = tree[1]
            if term not in cache:
                cache[term] = self.index.get_postings(term)
            return cache[term]
        if kind == "NOT":
            return difference(list(range(self.document_count)), self._evaluate(tree[1], cache))
        if kind == "OR":
            parts = [self._evaluate(child, cache) for child in tree[1]]
            result = []
            for part in parts:
                result = union(result, part)
            return result
        positives = []
        negatives = []
        for child in tree[1]:
            if child[0] == "NOT":
                negatives.append(self._evaluate(child[1], cache))
            else:
                positives.append(self._evaluate(child, cache))
        if positives:
            positives.sort(key=len)
            result = positives[0]
            for part in positives[1:]:
                result = intersect(result, part)
                if not result:
                    break
        else:
            result = list(range(self.document_count))
        for part in negatives:
            result = difference(result, part)
            if not result:
                break
        return result

    def close(self):
        self.index.close()
