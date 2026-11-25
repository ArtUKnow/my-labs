import os
import sys

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))

from stemmer import stem, is_cyrillic


EXCEPTIONS = {
    "люди": "человек",
    "людей": "человек",
    "людям": "человек",
    "людьми": "человек",
    "дети": "ребенок",
    "детей": "ребенок",
    "детям": "ребенок",
    "года": "год",
    "году": "год",
    "лет": "год",
    "годы": "год",
    "годов": "год",
    "годах": "год",
    "был": "быть",
    "была": "быть",
    "было": "быть",
    "были": "быть",
    "есть": "быть",
    "является": "являться",
    "являются": "являться",
    "являлся": "являться",
    "являлась": "являться",
    "его": "он",
    "ему": "он",
    "им": "он",
    "нем": "он",
    "ее": "она",
    "ей": "она",
    "ею": "она",
    "их": "они",
    "ими": "они",
    "них": "они",
    "нас": "мы",
    "нам": "мы",
    "нами": "мы",
    "was": "be",
    "were": "be",
    "is": "be",
    "are": "be",
    "been": "be",
    "has": "have",
    "had": "have",
    "went": "go",
    "gone": "go",
    "men": "man",
    "people": "person",
    "children": "child",
    "feet": "foot",
    "mice": "mouse",
}

RUSSIAN_PARADIGMS = [
    (["а", "у", "ом", "е", "ы", "и", "ов", "ам", "ами", "ах", ""], ""),
    (["а", "ы", "и", "е", "у", "ой", "ою", "ам", "ами", "ах", ""], "а"),
    (["я", "и", "е", "ю", "ей", "ею", "ям", "ями", "ях", "ий"], "я"),
    (["я", "ю", "ем", "е", "и", "ей", "ев", "ям", "ями", "ях", "ь"], "ь"),
    (["и", "ью", "ей", "ям", "ями", "ях", "ь"], "ь"),
    (["а", "у", "ом", "е", "ами", "ам", "ах", ""], "о"),
    (["я", "ю", "ем", "и", "ями", "ям", "ях", "ий"], "е"),
    (["ия", "ию", "ием", "ии", "ий", "иям", "иями", "иях"], "ие"),
    (["ии", "ию", "ией", "ий", "иям", "иями", "иях"], "ия"),
    (["ого", "ому", "ым", "ом", "ая", "ой", "ую", "ое", "ые", "ых", "ыми"], "ый"),
    (["ого", "ому", "им", "ом", "ая", "ой", "ую", "ое", "ие", "их", "ими"], "ий"),
    (["его", "ему", "им", "ем", "яя", "ей", "юю", "ее", "ие", "их", "ими"], "ий"),
    (["ю", "ешь", "ет", "ем", "ете", "ют", "л", "ла", "ло", "ли", "я", "вший", "вшая", "вшие", "емый"], "ть"),
    (["у", "ишь", "ит", "им", "ите", "ат", "ят", "л", "ла", "ло", "ли", "я", "вший", "вшая", "вшие", "имый"], "ить"),
    (["ется", "ются", "лся", "лась", "лось", "лись", "ясь", "ющийся", "ющаяся", "ющиеся"], "ться"),
    (["ется", "ются", "ется", "лся", "лась", "лось", "лись", "ясь"], "аться"),
]

ENGLISH_PARADIGMS = [
    (["ies"], "y"),
    (["es", "s"], ""),
    (["ed", "ing"], ""),
    (["ed", "ing"], "e"),
    (["ied"], "y"),
]


class Lemmatizer:
    def __init__(self, vocabulary=None):
        self.vocabulary = vocabulary
        if self.vocabulary is None:
            self.vocabulary = {}
        self.cache = {}

    def candidates(self, word):
        result = []
        if is_cyrillic(word):
            paradigms = RUSSIAN_PARADIGMS
        else:
            paradigms = ENGLISH_PARADIGMS
        for endings, base_ending in paradigms:
            for ending in endings:
                if ending == "":
                    if len(word) >= 3:
                        result.append((word + base_ending, word, endings))
                    continue
                if word.endswith(ending) and len(word) - len(ending) >= 2:
                    stem_part = word[: -len(ending)]
                    result.append((stem_part + base_ending, stem_part, endings))
        return result

    def paradigm_score(self, base, stem_part, endings):
        if base not in self.vocabulary:
            return 0
        score = 2
        seen = [base]
        for ending in endings:
            form = stem_part + ending
            if form in self.vocabulary and form not in seen:
                seen.append(form)
                score += 1
        return score

    def lemmatize(self, word):
        word = word.lower().replace("ё", "е")
        if word in self.cache:
            return self.cache[word]
        if word in EXCEPTIONS:
            self.cache[word] = EXCEPTIONS[word]
            return EXCEPTIONS[word]
        best = None
        best_score = 0
        for base, stem_part, endings in self.candidates(word):
            score = self.paradigm_score(base, stem_part, endings)
            if score > best_score:
                best = base
                best_score = score
        if best is None:
            if word in self.vocabulary:
                best = word
            else:
                best = stem(word)
        self.cache[word] = best
        return best


def load_vocabulary(tokens_dir, allowed_ids=None):
    vocabulary = {}
    if not os.path.isdir(tokens_dir):
        return vocabulary
    for source in os.listdir(tokens_dir):
        source_dir = os.path.join(tokens_dir, source)
        if not os.path.isdir(source_dir):
            continue
        for name in os.listdir(source_dir):
            if allowed_ids is not None and source + "/" + name[:-4] not in allowed_ids:
                continue
            for line in open(os.path.join(source_dir, name), encoding="utf-8"):
                token = line.strip().replace("ё", "е")
                if token != "":
                    vocabulary[token] = vocabulary.get(token, 0) + 1
    return vocabulary


def main():
    tokens_dir = os.path.join(os.path.dirname(os.path.abspath(__file__)), "..", "lab1_corpus", "data", "tokens")
    lemmatizer = Lemmatizer(load_vocabulary(tokens_dir))
    for line in sys.stdin:
        word = line.strip()
        if word != "":
            sys.stdout.write(lemmatizer.lemmatize(word) + "\n")


if __name__ == "__main__":
    main()
