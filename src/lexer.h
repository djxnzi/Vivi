struct Lexer {
    const char *src;
    const char *cur;
    int line, col;
};

struct Keyword { const char *text; TokenType type; };

static char peek(Lexer *lx);
static char peek_next(Lexer *lx);
static char advance(Lexer *lx);
static bool is_at_end(Lexer *lx);
static Token make_token(Lexer *lx, TokenType type, const char *start, int len, bool preceded_nl);
static TokenType check_keyword(const char *start, int len);
static Token lex_quoted_literal(Lexer *lx, const char *start, char quote, TokenType tok_type, bool preceded_nl);
static void skip_whitespace_and_comments(Lexer *lx, bool *saw_newline);
static bool match_word(Lexer *lx, const char *word);

static char peek(Lexer *lx) { return *lx->cur; }
static char peek_next(Lexer *lx) { return lx->cur[0] == '\0' ? '\0' : lx->cur[1]; }

static Keyword keywords[] = {
    {"if", TOK_IF}, {"else", TOK_ELSE}, {"while", TOK_WHILE}, {"for", TOK_FOR}, {"in", TOK_IN},
    {"switch", TOK_SWITCH}, {"default", TOK_DEFAULT}, {"break", TOK_BREAK},
    {"continue", TOK_CONTINUE}, {"fallthrough", TOK_FALLTHROUGH},
    {"fn", TOK_FN}, {"return", TOK_RETURN}, {"defer", TOK_DEFER},
    {"struct", TOK_STRUCT}, {"enum", TOK_ENUM},
    {"local", TOK_LOCAL}, {"static", TOK_STATIC},
    {"try", TOK_TRY}, {"catch", TOK_CATCH},
    {"as", TOK_AS},
    {"null", TOK_NULL}, {"true", TOK_TRUE}, {"false", TOK_FALSE},
    {"and", TOK_AND}, {"or", TOK_OR}, {"not", TOK_NEQ},
};

static const int keyword_count = sizeof(keywords) / sizeof(keywords[0]);