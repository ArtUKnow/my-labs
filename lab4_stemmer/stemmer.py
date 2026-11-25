import sys


VOWELS = "аеиоуыэюя"

PERFECTIVE_GERUND_1 = ["вшись", "вши", "в"]
PERFECTIVE_GERUND_2 = ["ившись", "ывшись", "ивши", "ывши", "ив", "ыв"]
ADJECTIVE = ["ими", "ыми", "его", "ого", "ему", "ому", "ее", "ие", "ые", "ое", "ей", "ий", "ый", "ой", "ем", "им", "ым", "ом", "их", "ых", "ую", "юю", "ая", "яя", "ою", "ею"]
PARTICIPLE_1 = ["ем", "нн", "вш", "ющ", "щ"]
PARTICIPLE_2 = ["ивш", "ывш", "ующ"]
REFLEXIVE = ["ся", "сь"]
VERB_1 = ["ешь", "нно", "ете", "йте", "ла", "на", "ли", "ем", "ло", "но", "ет", "ют", "ны", "ть", "й", "л", "н"]
VERB_2 = ["ейте", "уйте", "ила", "ыла", "ена", "ите", "или", "ыли", "ило", "ыло", "ено", "ует", "уют", "ены", "ить", "ыть", "ишь", "ей", "уй", "ил", "ыл", "им", "ым", "ен", "ят", "ит", "ыт", "ую", "ю"]
NOUN = ["иями", "ями", "ами", "ией", "иям", "ием", "иях", "ев", "ов", "ие", "ье", "еи", "ии", "ей", "ой", "ий", "ям", "ем", "ам", "ом", "ах", "ях", "ию", "ью", "ия", "ья", "а", "е", "и", "й", "о", "у", "ы", "ь", "ю", "я"]
SUPERLATIVE = ["ейше", "ейш"]
DERIVATIONAL = ["ость", "ост"]


def find_regions(word):
    rv = len(word)
    for i in range(len(word)):
        if word[i] in VOWELS:
            rv = i + 1
            break
    r1 = len(word)
    for i in range(1, len(word)):
        if word[i - 1] in VOWELS and word[i] not in VOWELS:
            r1 = i + 1
            break
    r2 = len(word)
    for i in range(r1 + 1, len(word)):
        if word[i - 1] in VOWELS and word[i] not in VOWELS:
            r2 = i + 1
            break
    return rv, r1, r2


def cut_ending(word, endings, rv, need_prev=False):
    for ending in endings:
        if word.endswith(ending) and len(word) - len(ending) >= rv:
            if need_prev:
                prev_pos = len(word) - len(ending) - 1
                if prev_pos < 0 or word[prev_pos] not in "ая":
                    continue
            return word[: -len(ending)], True
    return word, False


def stem_russian(word):
    word = word.replace("ё", "е")
    rv, r1, r2 = find_regions(word)
    if rv >= len(word):
        return word

    word, done = cut_ending(word, PERFECTIVE_GERUND_2, rv)
    if not done:
        word, done = cut_ending(word, PERFECTIVE_GERUND_1, rv, True)
    if not done:
        word, done = cut_ending(word, REFLEXIVE, rv)
        word, done = cut_ending(word, ADJECTIVE, rv)
        if done:
            word, extra = cut_ending(word, PARTICIPLE_2, rv)
            if not extra:
                word, extra = cut_ending(word, PARTICIPLE_1, rv, True)
        if not done:
            word, done = cut_ending(word, VERB_2, rv)
            if not done:
                word, done = cut_ending(word, VERB_1, rv, True)
        if not done:
            word, done = cut_ending(word, NOUN, rv)

    if word.endswith("и") and len(word) - 1 >= rv:
        word = word[:-1]

    for ending in DERIVATIONAL:
        if word.endswith(ending) and len(word) - len(ending) >= r2:
            word = word[: -len(ending)]
            break

    if word.endswith("нн") and len(word) - 1 >= rv:
        word = word[:-1]
    else:
        word, done = cut_ending(word, SUPERLATIVE, rv)
        if done and word.endswith("нн") and len(word) - 1 >= rv:
            word = word[:-1]
        elif word.endswith("ь") and len(word) - 1 >= rv:
            word = word[:-1]
    return word


def has_vowel(word):
    for ch in word:
        if ch in "aeiouy":
            return True
    return False


def stem_english(word):
    if len(word) <= 3:
        return word
    if word.endswith("sses"):
        word = word[:-2]
    elif word.endswith("ies"):
        word = word[:-2]
    elif word.endswith("ss"):
        pass
    elif word.endswith("s") and len(word) > 3:
        word = word[:-1]
    if word.endswith("ing") and has_vowel(word[:-3]) and len(word) > 5:
        word = word[:-3]
    elif word.endswith("ed") and has_vowel(word[:-2]) and len(word) > 4:
        word = word[:-2]
    if word.endswith("ly") and len(word) > 4:
        word = word[:-2]
    if word.endswith("ness") and len(word) > 6:
        word = word[:-4]
    if word.endswith("ment") and len(word) > 7:
        word = word[:-4]
    if word.endswith("tion") and len(word) > 6:
        word = word[:-3]
    return word


def is_cyrillic(word):
    for ch in word:
        if "а" <= ch <= "я" or ch == "ё":
            return True
    return False


def stem(word):
    word = word.lower()
    if is_cyrillic(word):
        return stem_russian(word)
    return stem_english(word)


def main():
    for line in sys.stdin:
        word = line.strip()
        if word != "":
            sys.stdout.write(stem(word) + "\n")


if __name__ == "__main__":
    main()
