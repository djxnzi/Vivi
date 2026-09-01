static Token peek_at(Parser *p, int n) {
    while (p->lookahead_count < n) {
        p->lookahead[p->lookahead_count++] = next_token(p->lx);
    }
    return p->lookahead[n - 1];
}

static void parser_advance(Parser *p) {
    p->previous = p->current;
    if (p->lookahead_count > 0) {
        p->current = p->lookahead[0];
        for (int i = 1; i < p->lookahead_count; i++) p->lookahead[i - 1] = p->lookahead[i];
        p->lookahead_count--;
    } else {
        p->current = next_token(p->lx);
    }
}

static bool check(Parser *p, TokenType type) {
    return p->current.type == type;
}

static bool match(Parser *p, TokenType type) {
    if (!check(p, type)) return false;
    parser_advance(p);
    return true;
}

static void parser_error(Parser *p, const char *msg) {
    p->had_error = true;
    current_line = p->current.line; current_col = p->current.col;
    print_err("parse_error", p->current.line, p->current.col, msg, p->current.len, p->current.start);
}

static void expect(Parser *p, TokenType type, const char *msg) {
    if (check(p, type)) { parser_advance(p); return; }
    current_line = p->current.line; current_col = p->current.col;
    print_err("malformed_stmt", msg);
    parser_advance(p);
}

static Precedence binary_precedence(TokenType type) {
    switch (type) {
        case TOK_OR:            return PREC_OR;
        case TOK_AND:           return PREC_AND;
        case TOK_EQ:
        case TOK_NEQ:
        case TOK_IN:
                                return PREC_EQUALITY;
        case TOK_LT:
        case TOK_LTE:
        case TOK_GT:
        case TOK_GTE:
                                return PREC_COMPARISON;
        case TOK_TILDE:         return PREC_CONCAT;
        case TOK_SLICE:         return PREC_SLICE;
        case TOK_PLUS:
        case TOK_MINUS:
                                return PREC_ADDITIVE;
        case TOK_STAR:
        case TOK_SLASH:
        case TOK_PERCENT:
                                return PREC_MULTIPLICATIVE;
        case TOK_AMP:
        case TOK_CARET:
                                return PREC_BITWISE;
        case TOK_SHL:
        case TOK_SHR:
                                return PREC_SHIFT;
        default:                return PREC_NONE;
    }
}

static bool is_binary_op(TokenType type) {
    return binary_precedence(type) != PREC_NONE;
}

static bool is_assign_op(TokenType type) {
    switch (type) {
        case TOK_ASSIGN:
        case TOK_PLUSEQ: case TOK_MINUSEQ:
        case TOK_STAREQ: case TOK_SLASHEQ:
        case TOK_MODEQ:
            return true;
        default:
            return false;
    }
}

static bool is_type_keyword(TokenType type) {
    switch (type) {
        case TOK_TYPE_RUNE: case TOK_TYPE_STR: case TOK_TYPE_BOOL:
        case TOK_TYPE_I8: case TOK_TYPE_I16: case TOK_TYPE_I32: case TOK_TYPE_I64:
        case TOK_TYPE_U8: case TOK_TYPE_U16: case TOK_TYPE_U32: case TOK_TYPE_U64:
        case TOK_TYPE_F32: case TOK_TYPE_F64: case TOK_TYPE_EXT:
            return true;
        default:
            return false;
    }
}

static bool is_type_name_token(Token t) {
    if (t.type != TOK_IDENT) return false;
    ValueType vt;
    return ident_is_type_name(t.start, t.len, &vt);
}

static bool at_type_annotation(Parser *p) {
    if (is_type_keyword(p->current.type)) return true;
    if (check(p, TOK_IDENT) && peek_at(p, 1).type == TOK_IDENT) return true;
    if (is_type_name_token(p->current)) {
        TokenType next = peek_at(p, 1).type;
        if (next == TOK_LBRACKET || next == TOK_STAR || next == TOK_ELLIPSES) return true;
    }
    return false;
}

static bool starts_expression(TokenType type) {
    switch (type) {
        // Bunch of token checks to check if we're dereferencing or multiplying
        case TOK_NOT: case TOK_MINUS:
        case TOK_AMP: case TOK_INT_LIT:
        case TOK_FLOAT_LIT: case TOK_STR_LIT:
        case TOK_RUNE_LIT: case TOK_TRUE:
        case TOK_FALSE: case TOK_NULL:
        case TOK_IDENT: case TOK_FN:
        case TOK_LPAREN: case TOK_LBRACKET:
        case TOK_LBRACE: case TOK_IF:
            return true;
        default:
            return is_type_keyword(type);
    }
}

static void sync(Parser *p) {
    while (!check(p, TOK_EOF)) {
        if (p->current.preceded_nl) return;
        if (check(p, TOK_SEMICOLON)) { parser_advance(p); return; }
        if (check(p, TOK_RBRACE)) return;
        parser_advance(p);
    }
}