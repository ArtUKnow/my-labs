import sys


def is_word_char(ch):
    return ch.isalpha() or ch.isdigit()


def is_inner_char(ch):
    return ch == "-" or ch == "'" or ch == "’" or ch == "." or ch == ","


def tokenize(text):
    tokens = []
    current = ""
    length = len(text)
    i = 0
    while i < length:
        ch = text[i]
        if is_word_char(ch):
            current += ch
        elif is_inner_char(ch) and current != "" and i + 1 < length and is_word_char(text[i + 1]):
            if ch == "." or ch == ",":
                if current[-1].isdigit() and text[i + 1].isdigit():
                    current += ch
                else:
                    tokens.append(current.lower())
                    current = ""
            else:
                current += ch
        else:
            if current != "":
                tokens.append(current.lower())
                current = ""
        i += 1
    if current != "":
        tokens.append(current.lower())
    return tokens


def token_kind(token):
    has_letter = False
    has_digit = False
    for ch in token:
        if ch.isalpha():
            has_letter = True
        if ch.isdigit():
            has_digit = True
    if has_letter and has_digit:
        return "mixed"
    if has_digit:
        return "number"
    return "word"


def main():
    text = sys.stdin.read()
    for token in tokenize(text):
        sys.stdout.write(token + "\n")


if __name__ == "__main__":
    main()
