#ifndef IR_LAB08_SEARCH_QUERY_PARSER_H
#define IR_LAB08_SEARCH_QUERY_PARSER_H
struct QueryWord {
    Kind kind;
    char *text;
    char bracket;
};
struct LexerContext {
    Array<QueryWord> *tokens;
};
inline void query_token(const char *t, size_t n, void *state) {
    LexerContext *c = (LexerContext *)state;
    QueryWord l = {TERM, duplicate(t, n), 0};
    c->tokens->add(l);
}
inline bool read_query_words(const char *s, Array<QueryWord> &tokens, const char *&error) {
    size_t i = 0;
    while (s[i]) {
        unsigned char c = (unsigned char)s[i];
        if (c <= 32) {
            ++i;
            continue;
        }
        QueryWord l = {END, 0, 0};
        if (c == '&' || c == '|') {
            if (s[i + 1] != (char)c) {
                error = "Use && for AND and || for OR";
                return false;
            }
            l.kind = c == '&' ? AND : OR;
            i += 2;
        } else if (c == '!') {
            l.kind = NOT;
            ++i;
        } else if (c == '(' || c == '[') {
            l.kind = LEFT;
            l.bracket = (char)c;
            ++i;
        } else if (c == ')' || c == ']') {
            l.kind = RIGHT;
            l.bracket = (char)c;
            ++i;
        } else {
            size_t start = i;
            while (s[i] && (unsigned char)s[i] > 32 && !strchr("&|!()[]", s[i]))
                ++i;
            LexerContext context = {&tokens};
            ir_tokenize(s + start, i - start, query_token, &context);
            continue;
        }
        tokens.add(l);
    }
    return true;
}
inline Array<uint32_t> *combine(Array<uint32_t> *a, Array<uint32_t> *b, Kind operation) {
    Array<uint32_t> *r = new Array<uint32_t>;
    size_t i = 0, j = 0;
    while (i < a->length && j < b->length) {
        uint32_t x = a->items[i], y = b->items[j];
        if (x == y) {
            if (operation != NOT)
                r->add(x);
            ++i;
            ++j;
        } else if (x < y) {
            if (operation == OR || operation == NOT)
                r->add(x);
            ++i;
        } else {
            if (operation == OR)
                r->add(y);
            ++j;
        }
    }
    if (operation == OR || operation == NOT)
        while (i < a->length)
            r->add(a->items[i++]);
    if (operation == OR)
        while (j < b->length)
            r->add(b->items[j++]);
    delete a;
    delete b;
    return r;
}
struct Parser {
    Index &index;
    Array<QueryWord> &tokens;
    size_t position;
    unsigned depth;
    const char *error;
    Parser(Index &i, Array<QueryWord> &t) : index(i), tokens(t), position(0), depth(0), error(0) {
    }
    Kind peek() {
        return position < tokens.length ? tokens.items[position].kind : END;
    }
    Array<uint32_t> *all() {
        Array<uint32_t> *r = new Array<uint32_t>;
        for (size_t i = 0; i < index.docs.length; ++i)
            r->add((uint32_t)i);
        return r;
    }
    Array<uint32_t> *atom() {
        if (++depth > 256) {
            error = "Query nesting exceeds 256";
            --depth;
            return new Array<uint32_t>;
        }
        Array<uint32_t> *r = 0;
        if (peek() == TERM) {
            r = index.find(tokens.items[position++].text);
        } else if (peek() == LEFT) {
            char open = tokens.items[position++].bracket;
            r = parse_or();
            if (!error &&
                (peek() != RIGHT || tokens.items[position].bracket != (open == '(' ? ')' : ']')))
                error = "Mismatched or unclosed bracket";
            if (!error)
                ++position;
        } else {
            error = "Expected term or opening bracket";
            r = new Array<uint32_t>;
        }
        --depth;
        return r;
    }
    Array<uint32_t> *parse_not() {
        if (peek() == NOT) {
            if (++depth > 256) {
                error = "Query nesting exceeds 256";
                --depth;
                return new Array<uint32_t>;
            }
            ++position;
            Array<uint32_t> *r = parse_not();
            --depth;
            return combine(all(), r, NOT);
        }
        return atom();
    }
    Array<uint32_t> *parse_and() {
        Array<uint32_t> *r = parse_not();
        while (!error && (peek() == AND || peek() == TERM || peek() == NOT || peek() == LEFT)) {
            if (peek() == AND)
                ++position;
            r = combine(r, parse_not(), AND);
        }
        return r;
    }
    Array<uint32_t> *parse_or() {
        Array<uint32_t> *r = parse_and();
        while (!error && peek() == OR) {
            ++position;
            r = combine(r, parse_and(), OR);
        }
        return r;
    }
    Array<uint32_t> *parse() {
        if (!tokens.length)
            return new Array<uint32_t>;
        Array<uint32_t> *r = parse_or();
        if (!error && peek() != END)
            error = "Unexpected closing bracket or operator";
        return r;
    }
};
#endif
