static char advance(Lexer *lx) {
    char c = *lx->cur++;
    if (c == '\n') { lx->line++; lx->col = 1; }
    else { lx->col++; }
    return c;
}

static bool is_at_end(Lexer *lx) { return *lx->cur == '\0'; }

// Tok construction
static Token make_token(Lexer *lx, TokenType type, const char *start, int len, bool preceded_nl) {
    Token t;
    t.type = type;
    t.start = start;
    t.len = len;
    t.line = lx->line;
    t.col = lx->col;
    t.preceded_nl = preceded_nl;
    return t;
}

static TokenType check_keyword(const char *start, int len) {
    for (int i = 0; i < keyword_count; i++) {
        if ((int)strlen(keywords[i].text) == len && strncmp(start, keywords[i].text, len) == 0) {
            return keywords[i].type;
        }
    }
    return TOK_IDENT;
}

// Skip whitespace/comments
static void skip_whitespace_and_comments(Lexer *lx, bool *saw_newline) {
    for (;;) {
        char c = peek(lx);
        if (c == ' ' || c == '\t' || c == '\r') {
            advance(lx);
        } else if (c == '\n') {
            *saw_newline = true;
            advance(lx);
        } else if (c == '/' && peek_next(lx) == '/') {
            while (!is_at_end(lx) && peek(lx) != '\n') advance(lx);
        } else if (c == '/' && peek_next(lx) == '*') {
            advance(lx); advance(lx);
            while (!is_at_end(lx) && !(peek(lx) == '*' && peek_next(lx) == '/')) {
                if (peek(lx) == '\n') *saw_newline = true;
                advance(lx);
            }
            if (!is_at_end(lx)) { advance(lx); advance(lx); }
        } else if (c == ';' && peek_next(lx) == ';') {
            while (!is_at_end(lx) && peek(lx) != '\n') advance(lx);
        } else {
            break;
        }
    }
}

// Matching
static bool match_word(Lexer *lx, const char *word) {
    int len = (int)strlen(word);
    for (int i = 0; i < len; i++) {
        if (lx->cur[i] != word[i]) return false;
    }
    char after = lx->cur[len];
    if (isalnum((unsigned char)after) || after == '_') return false;
    for (int i = 0; i < len; i++) advance(lx);
    return true;
}

// Escaped Lit
static Token lex_quoted_literal(Lexer *lx, const char *start, char quote, TokenType tok_type, bool preceded_nl) {
    int start_line = lx->line;
    while (!is_at_end(lx) && peek(lx) != quote && peek(lx) != '\n') {
        if (peek(lx) == '\\') {
            advance(lx);
            if (!is_at_end(lx)) advance(lx);
            continue;
        }
        advance(lx);
    }
    if (is_at_end(lx) || peek(lx) != quote) {
        print_source_line(stdout, lx->src, start_line);
        print_err("unterminated", start_line);
        return make_token(lx, tok_type, start + 1, 0, preceded_nl);
    }
    advance(lx);

    const char *raw_start = start + 1;
    int raw_len = (int)(lx->cur - start - 2);

    char *decoded = (char*)arena_alloc(raw_len);
    int out_len = 0;
    for (int i = 0; i < raw_len; i++) {
        char ch = raw_start[i];
        if (ch == '\\' && i + 1 < raw_len) {
            i++;

            // Newlines, quotes, escapes
            char esc = raw_start[i];
            if (esc == 'n')       decoded[out_len++] = '\n';
            else if (esc == 't')  decoded[out_len++] = '\t';
            else if (esc == 'r')  decoded[out_len++] = '\r';
            else if (esc == '\\') decoded[out_len++] = '\\';
            else if (esc == quote) decoded[out_len++] = quote;
            else                  decoded[out_len++] = esc;
        } else {
            decoded[out_len++] = ch;
        }
    }
    return make_token(lx, tok_type, decoded, out_len, preceded_nl);
}

// Tokenizer
Token next_token(Lexer *lx) {
    bool preceded_nl = false;
    skip_whitespace_and_comments(lx, &preceded_nl);

    const char *start = lx->cur;
    if (is_at_end(lx)) return make_token(lx, TOK_EOF, start, 0, preceded_nl);

    char c = advance(lx);

    // Idents and keywords
    if (isalpha((unsigned char)c) || c == '_') {
        while (isalnum((unsigned char)peek(lx)) || peek(lx) == '_') advance(lx);
        int len = (int)(lx->cur - start);
        return make_token(lx, check_keyword(start, len), start, len, preceded_nl);
    }

    // Numeric lits
    if (isdigit((unsigned char)c)) {
        bool is_float = false;
        while (isdigit((unsigned char)peek(lx))) advance(lx);
        if (peek(lx) == '.' && peek_next(lx) != '.' && isdigit((unsigned char)peek_next(lx))) {
            is_float = true;
            advance(lx);
            while (isdigit((unsigned char)peek(lx))) advance(lx);
        }
        int len = (int)(lx->cur - start);
        Token t = make_token(lx, is_float ? TOK_FLOAT_LIT : TOK_INT_LIT, start, len, preceded_nl);
        if (is_float) t.float_val = strtod(start, nullptr);
        else t.int_val = strtol(start, nullptr, 10);
        return t;
    }

    // String lits
    if (c == '"') {
        return lex_quoted_literal(lx, start, '"', TOK_STR_LIT, preceded_nl);
    }

    // Rune lits
    if (c == '\'') {
        return lex_quoted_literal(lx, start, '\'', TOK_RUNE_LIT, preceded_nl);
    }

    // Directives and # comments
    if (c == '#') {
        if (match_word(lx, "import"))       return make_token(lx, TOK_IMPORT, start, (int)(lx->cur - start), preceded_nl);
        if (match_word(lx, "proctime"))     return make_token(lx, TOK_PROCTIME, start, (int)(lx->cur - start), preceded_nl);
        if (match_word(lx, "interop"))      return make_token(lx, TOK_INTEROP, start, (int)(lx->cur - start), preceded_nl);
        if (match_word(lx, "primitive"))    return make_token(lx, TOK_PRIMITIVE, start, (int)(lx->cur - start), preceded_nl);
        if (match_word(lx, "inline"))       return make_token(lx, TOK_INLINE, start, (int)(lx->cur - start), preceded_nl);
        if (match_word(lx, "method"))       return make_token(lx, TOK_METHOD, start, (int)(lx->cur - start), preceded_nl);

        // not a directive, comment
        while (!is_at_end(lx) && peek(lx) != '\n') advance(lx);
        return next_token(lx);
    }

    // Single and multi-character operators / punctuation
    switch (c) {
        case '(': return make_token(lx, TOK_LPAREN, start, 1, preceded_nl);
        case ')': return make_token(lx, TOK_RPAREN, start, 1, preceded_nl);
        case '{': return make_token(lx, TOK_LBRACE, start, 1, preceded_nl);
        case '}': return make_token(lx, TOK_RBRACE, start, 1, preceded_nl);
        case '[': return make_token(lx, TOK_LBRACKET, start, 1, preceded_nl);
        case ']': return make_token(lx, TOK_RBRACKET, start, 1, preceded_nl);
        case ',': return make_token(lx, TOK_COMMA, start, 1, preceded_nl);
        case '?': return make_token(lx, TOK_QUESTION, start, 1, preceded_nl);

        case '|':
            if (peek(lx) == '|') { advance(lx); return make_token(lx, TOK_OR, start, 2, preceded_nl); }
            return make_token(lx, TOK_PIPE, start, 1, preceded_nl);

        case ':':
            if (peek(lx) == ':') { advance(lx); return make_token(lx, TOK_IMMUT, start, 2, preceded_nl); }
            if (peek(lx) == '=') { advance(lx); return make_token(lx, TOK_MUT, start, 2, preceded_nl); }
            return make_token(lx, TOK_COLON, start, 1, preceded_nl);

        case '=':
            if (peek(lx) == '=') { advance(lx); return make_token(lx, TOK_EQ, start, 2, preceded_nl); }
            return make_token(lx, TOK_ASSIGN, start, 1, preceded_nl);

        case '!':
            if (peek(lx) == '=') { advance(lx); return make_token(lx, TOK_NEQ, start, 2, preceded_nl); }
            return make_token(lx, TOK_NOT, start, 1, preceded_nl);

        case '&':
            if (peek(lx) == '&') { advance(lx); return make_token(lx, TOK_AND, start, 2, preceded_nl); }
            return make_token(lx, TOK_AMP, start, 1, preceded_nl);

        case '/':
            if (peek(lx) == '=') { advance(lx); return make_token(lx, TOK_SLASHEQ, start, 2, preceded_nl); }
            return make_token(lx, TOK_SLASH, start, 1, preceded_nl);

        case '+':
            if (peek(lx) == '+') { advance(lx); return make_token(lx, TOK_PLUSPLUS, start, 2, preceded_nl); }
            if (peek(lx) == '=') { advance(lx); return make_token(lx, TOK_PLUSEQ, start, 2, preceded_nl); }
            return make_token(lx, TOK_PLUS, start, 1, preceded_nl);

        case '-':
            if (peek(lx) == '-') { advance(lx); return make_token(lx, TOK_MINUSMINUS, start, 2, preceded_nl); }
            if (peek(lx) == '=') { advance(lx); return make_token(lx, TOK_MINUSEQ, start, 2, preceded_nl); }
            if (peek(lx) == '>') { advance(lx); return make_token(lx, TOK_ARROW, start, 2, preceded_nl); }
            return make_token(lx, TOK_MINUS, start, 1, preceded_nl);

        case '*':
            if (peek(lx) == '=') { advance(lx); return make_token(lx, TOK_STAREQ, start, 2, preceded_nl); }
            return make_token(lx, TOK_STAR, start, 1, preceded_nl);

        case '%':
            if (peek(lx) == '=') { advance(lx); return make_token(lx, TOK_MODEQ, start, 2, preceded_nl); }
            return make_token(lx, TOK_PERCENT, start, 1, preceded_nl);

        case '.':
            if (peek(lx) == '.') {
                advance(lx);
                if (peek(lx) == '.') { advance(lx); return make_token(lx, TOK_ELLIPSES, start, 3, preceded_nl); }
                return make_token(lx, TOK_SLICE, start, 2, preceded_nl);
            }
            return make_token(lx, TOK_DOT, start, 1, preceded_nl);

        case '>':
            if (peek(lx) == '=') { advance(lx); return make_token(lx, TOK_GTE, start, 2, preceded_nl); }
            if (peek(lx) == '>') { advance(lx); return make_token(lx, TOK_SHR, start, 2, preceded_nl); }
            return make_token(lx, TOK_GT, start, 1, preceded_nl);

        case '<':
            if (peek(lx) == '=') { advance(lx); return make_token(lx, TOK_LTE, start, 2, preceded_nl); }
            if (peek(lx) == '<') { advance(lx); return make_token(lx, TOK_SHL, start, 2, preceded_nl); }
            return make_token(lx, TOK_LT, start, 1, preceded_nl);

        case '^':
            return make_token(lx, TOK_CARET, start, 1, preceded_nl);

        case ';':
            return make_token(lx, TOK_SEMICOLON, start, 1, preceded_nl);

        case '~':
            return make_token(lx, TOK_TILDE, start, 1, preceded_nl);
    }

    return make_token(lx, TOK_ERROR, start, 1, preceded_nl);
}

// Token name lookup
const char *token_name(TokenType t) {
    switch (t) {
        // Lits & idents
        case TOK_IDENT:         return "IDENT";
        case TOK_INT_LIT:       return "INT_LIT";
        case TOK_FLOAT_LIT:     return "FLOAT_LIT";
        case TOK_STR_LIT:       return "STR_LIT";
        case TOK_RUNE_LIT:      return "RUNE_LIT";

        // Lit keywords
        case TOK_NULL:          return "NULL";
        case TOK_TRUE:          return "TRUE";
        case TOK_FALSE:         return "FALSE";

        // Control flow
        case TOK_IF:            return "IF";
        case TOK_ELSE:          return "ELSE";
        case TOK_WHILE:         return "WHILE";
        case TOK_FOR:           return "FOR";
        case TOK_IN:            return "IN";
        case TOK_SWITCH:        return "SWITCH";
        case TOK_DEFAULT:       return "DEFAULT";
        case TOK_BREAK:         return "BREAK";
        case TOK_CONTINUE:      return "CONTINUE";
        case TOK_FALLTHROUGH:   return "FALLTHROUGH";

        // Functions & program flow
        case TOK_FN:            return "FN";
        case TOK_RETURN:        return "RETURN";
        case TOK_DEFER:         return "DEFER";

        // Declarations
        case TOK_STRUCT:        return "STRUCT";
        case TOK_ENUM:          return "ENUM";
        case TOK_LOCAL:         return "LOCAL";
        case TOK_STATIC:        return "STATIC";

        // Error handling
        case TOK_TRY:           return "TRY";
        case TOK_CATCH:         return "CATCH";

        // Preprocessor-triggered
        case TOK_PROCTIME:      return "PROCTIME";
        case TOK_IMPORT:        return "IMPORT";
        case TOK_INTEROP:       return "INTEROP";
        case TOK_PRIMITIVE:     return "PRIMTIVE";
        case TOK_INLINE:        return "INLINE";
        case TOK_METHOD:        return "METHOD";

        // Import
        case TOK_AS:            return "AS";

        // Type keywords
        case TOK_TYPE_RUNE:     return "TYPE_RUNE";
        case TOK_TYPE_STR:      return "TYPE_STR";
        case TOK_TYPE_BOOL:     return "TYPE_BOOL";
        case TOK_TYPE_I8:       return "TYPE_I8";
        case TOK_TYPE_I16:      return "TYPE_I16";
        case TOK_TYPE_I32:      return "TYPE_I32";
        case TOK_TYPE_I64:      return "TYPE_I64";
        case TOK_TYPE_U8:       return "TYPE_U8";
        case TOK_TYPE_U16:      return "TYPE_U16";
        case TOK_TYPE_U32:      return "TYPE_U32";
        case TOK_TYPE_U64:      return "TYPE_U64";
        case TOK_TYPE_F32:      return "TYPE_F32";
        case TOK_TYPE_F64:      return "TYPE_F64";
        case TOK_TYPE_EXT:      return "TYPE_EXT";

        // Declaration/assignment
        case TOK_IMMUT:         return "IMMUT";
        case TOK_MUT:           return "MUT";
        case TOK_ASSIGN:        return "ASSIGN";

        // Comparison
        case TOK_EQ:            return "EQ";
        case TOK_NEQ:           return "NEQ";
        case TOK_GT:            return "GT";
        case TOK_GTE:           return "GTE";
        case TOK_LT:            return "LT";
        case TOK_LTE:           return "LTE";

        // Logical
        case TOK_AND:           return "AND";
        case TOK_AMP:           return "AMP";
        case TOK_OR:            return "OR";
        case TOK_NOT:           return "NOT";

        // Arithmetic
        case TOK_PLUS:          return "PLUS";
        case TOK_MINUS:         return "MINUS";
        case TOK_STAR:          return "STAR";
        case TOK_SLASH:         return "SLASH";
        case TOK_PERCENT:       return "PERCENT";
        case TOK_CARET:         return "CARET";
        case TOK_SHL:           return "SHL";
        case TOK_SHR:           return "SHR";

        // Compound assignment
        case TOK_PLUSEQ:        return "PLUSEQ";
        case TOK_MINUSEQ:       return "MINUSEQ";
        case TOK_STAREQ:        return "STAREQ";
        case TOK_SLASHEQ:       return "SLASHEQ";
        case TOK_MODEQ:         return "MODEQ";

        // Increment/decrement
        case TOK_PLUSPLUS:      return "PLUSPLUS";
        case TOK_MINUSMINUS:    return "MINUSMINUS";

        // Misc operators
        case TOK_QUESTION:      return "QUESTION";
        case TOK_SLICE:         return "SLICE";
        case TOK_COLON:         return "COLON";
        case TOK_PIPE:          return "PIPE";
        case TOK_DOT:           return "DOT";
        case TOK_ELLIPSES:      return "ELLIPSES";
        case TOK_COMMA:         return "COMMA";
        case TOK_ARROW:         return "ARROW";
        case TOK_SEMICOLON:     return "SEMICOLON";
        case TOK_TILDE:         return "TILDE";

        // Delimiters
        case TOK_LPAREN:        return "LPAREN";
        case TOK_RPAREN:        return "RPAREN";
        case TOK_LBRACE:        return "LBRACE";
        case TOK_RBRACE:        return "RBRACE";
        case TOK_LBRACKET:      return "LBRACKET";
        case TOK_RBRACKET:      return "RBRACKET";

        // Meta
        case TOK_EOF:           return "EOF";
        case TOK_ERROR:         return "ERROR";
    }
    return "?";
}