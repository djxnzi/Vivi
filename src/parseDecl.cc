static AstNode* parse_decl(Parser *p) {
    int line = p->current.line, col = p->current.col;

    AstNode *type_annotation = nullptr;
    AstNode *type_size = nullptr;
    bool is_unsized_array = false;
    if (at_type_annotation(p) || (check(p, TOK_IDENT) && peek_at(p, 1).type == TOK_STAR)) {
        type_annotation = make_ident(p->current.start, p->current.len, p->current.line, p->current.col);
        parser_advance(p);
        if (match(p, TOK_STAR)) {
            type_annotation = make_unary(TOK_STAR, type_annotation, p->previous.line, p->previous.col);
        } else if (match(p, TOK_LBRACKET)) {
            if (!check(p, TOK_RBRACKET)) {
                type_size = parse_expression(p);
            } else {
                is_unsized_array = true;
            }
            expect(p, TOK_RBRACKET, "expected ']' after array size");
        }
    }

    expect(p, TOK_IDENT, "expected identifier in declaration");
    bool has_type_annotation = (type_annotation != nullptr);
    const char *name = p->previous.start;
    int name_len = p->previous.len;

    bool is_immutable = false;
    AstNode *value = nullptr;
    if (match(p, TOK_MUT)) {
        value = parse_expression(p);
    } else if (match(p, TOK_IMMUT)) {
        is_immutable = true;
        value = parse_expression(p);
    } else if (!has_type_annotation) {
        parser_error(p, "expected ':=' or '::' in declaration");
    }

    if (!match(p, TOK_SEMICOLON) && !p->current.preceded_nl
        && !check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        parser_error(p, "expected newline or ';' after declaration");
    }

    AstNode *n = is_immutable
        ? make_immut_decl(name, name_len, has_type_annotation, type_annotation, value, line, col)
        : make_mut_decl(name, name_len, has_type_annotation, type_annotation, value, line, col);
    n->as.decl.type_size = type_size;
    n->as.decl.is_unsized_array = is_unsized_array;
    return n;
}

static AstNode* parse_destructure_decl(Parser *p) {
    int line = p->current.line, col = p->current.col;

    AstList targets;
    ast_list_init(&targets);

    do {
        int t_line = p->current.line, t_col = p->current.col;
        AstNode *type_annotation = nullptr;
        if (at_type_annotation(p)) {
            type_annotation = make_ident(p->current.start, p->current.len, p->current.line, p->current.col);
            parser_advance(p);
        }
        expect(p, TOK_IDENT, "expected identifier in destructuring declaration");
        ast_list_push(&targets, make_param(p->previous.start, p->previous.len, type_annotation, nullptr, t_line, t_col));
    } while (match(p, TOK_COMMA));

    bool is_immutable = false;
    if (match(p, TOK_MUT)) {
        is_immutable = false;
    } else if (match(p, TOK_IMMUT)) {
        is_immutable = true;
    } else {
        parser_error(p, "expected ':=' or '::' in destructuring declaration");
    }

    AstNode *value = parse_expression(p);

    if (!match(p, TOK_SEMICOLON) && !p->current.preceded_nl
        && !check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        parser_error(p, "expected newline or ';' after declaration");
    }

    int count;
    AstNode **items = ast_list_finish(&targets, &count);
    return make_destructure_decl(items, count, is_immutable, value, line, col);
}

static AstNode** parse_param_list(Parser *p, int *out_count) {
    AstList params;
    ast_list_init(&params);

    if (!match(p, TOK_LPAREN)) {
        return ast_list_finish(&params, out_count);
    }

    if (!check(p, TOK_RPAREN)) {
        do {
            int line = p->current.line, col = p->current.col;

            AstNode *type_expr = nullptr;
            if (match(p, TOK_STAR)) {
                type_expr = make_unary(TOK_STAR, nullptr, p->previous.line, p->previous.col);
            } else if (at_type_annotation(p)) {
                int tline = p->current.line, tcol = p->current.col;
                type_expr = make_ident(p->current.start, p->current.len, tline, tcol);
                parser_advance(p);
                if (check(p, TOK_LBRACKET) && peek_at(p, 1).type == TOK_RBRACKET) {
                    parser_advance(p); parser_advance(p);
                    type_expr = make_array_type(type_expr, tline, tcol);
                }
            }

            if (match(p, TOK_ELLIPSES)) {
                AstNode *variadic = make_param(nullptr, 0, type_expr, nullptr, line, col);
                variadic->as.field.is_variadic = true;
                ast_list_push(&params, variadic);
                break;
            }

            expect(p, TOK_IDENT, "expected parameter name");
            const char *name = p->previous.start;
            int name_len = p->previous.len;

            AstNode *default_value = nullptr;
            if (match(p, TOK_MUT)) {
                default_value = parse_expression(p);
            }

            ast_list_push(&params, make_param(name, name_len, type_expr, default_value, line, col));
        } while (match(p, TOK_COMMA));
    }

    expect(p, TOK_RPAREN, "expected ')' after parameter list");
    return ast_list_finish(&params, out_count);
}

static AstNode* parse_fn_lit(Parser *p, int line, int col) {
    int param_count;
    AstNode **params = parse_param_list(p, &param_count);
    AstNode *return_type = parse_optional_return(p);
    AstNode *body = parse_block(p);
    return make_fn_lit(params, param_count, return_type, body, line, col);
}

static AstNode* parse_fn_decl(Parser *p) {
    int line = p->current.line, col = p->current.col;
    expect(p, TOK_FN, "expected 'fn'");

    expect(p, TOK_IDENT, "expected function name after 'fn'");
    const char *name = p->previous.start;
    int name_len = p->previous.len;

    expect(p, TOK_IMMUT, "expected '::' after function name");

    int param_count;
    AstNode **params = parse_param_list(p, &param_count);
    AstNode *return_type = parse_optional_return(p);
    AstNode *body = parse_block(p);

    return make_fn_decl(name, name_len, params, param_count, return_type, body, line, col);
}

static AstNode* parse_lambda(Parser *p) {
    int line = p->current.line, col = p->current.col;
    parser_advance(p);
    AstNode *lit = parse_fn_lit(p, line, col);

    AstList args;
    ast_list_init(&args);
    AstNode *call = make_call(lit, &args, line, col);

    if (match(p, TOK_SEMICOLON)) {
    } else if (p->current.preceded_nl || check(p, TOK_RBRACE) || check(p, TOK_EOF)) {
    } else {
        parser_error(p, "expected newline or ';' after expression statement");
    }

    return make_expr_stmt(call, line, col);
}

static AstNode* parse_single_return(Parser *p) {
    if (match(p, TOK_STAR)) {
        int line = p->previous.line, col = p->previous.col;
        if (!is_type_keyword(p->current.type) && !check(p, TOK_IDENT) && !check(p, TOK_STRUCT)) {
            parser_error(p, "expected type after '*' in return type");
            return nullptr;
        }
        AstNode *pointee = make_ident(p->current.start, p->current.len, p->current.line, p->current.col);
        parser_advance(p);
        return make_unary(TOK_STAR, pointee, line, col);
    }
    if (!is_type_keyword(p->current.type) && !check(p, TOK_IDENT) && !check(p, TOK_STRUCT)) {
        parser_error(p, "expected return type after '->'");
        return nullptr;
    }
    int line = p->current.line, col = p->current.col;
    AstNode *type_expr = make_ident(p->current.start, p->current.len, line, col);
    parser_advance(p);
    if (check(p, TOK_LBRACKET) && peek_at(p, 1).type == TOK_RBRACKET) {
        parser_advance(p); parser_advance(p);
        return make_array_type(type_expr, line, col);
    }
    return type_expr;
}

static AstNode* parse_optional_return(Parser *p) {
    if (!match(p, TOK_ARROW)) return nullptr;

    AstNode *first = parse_single_return(p);

    if (check(p, TOK_PIPE)) {
        AstNode *left = first;
        while (match(p, TOK_PIPE)) {
            int line = p->current.line, col = p->current.col;
            left = make_binary(TOK_PIPE, left, parse_single_return(p), line, col);
        }
        return left;
    }

    if (check(p, TOK_COMMA)) {
        AstList types;
        ast_list_init(&types);
        ast_list_push(&types, first);
        while (match(p, TOK_COMMA)) {
            ast_list_push(&types, parse_single_return(p));
        }
        int count;
        AstNode **items = ast_list_finish(&types, &count);
        return make_return_type_list(items, count, first->line, first->col);
    }

    return first;
}

static AstNode* parse_struct_decl(Parser *p) {
    int line = p->current.line, col = p->current.col;

    expect(p, TOK_STRUCT, "expected 'struct'");
    expect(p, TOK_IDENT, "expected struct name");

    const char *name = p->previous.start;
    int name_len = p->previous.len;
    expect(p, TOK_LBRACE, "expected '{'");

    AstList fields;
    ast_list_init(&fields);
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        int field_line = p->current.line, field_col = p->current.col;

        AstNode *type_expr = nullptr;
        if (at_type_annotation(p)) {
            type_expr = make_ident(p->current.start, p->current.len, p->current.line, p->current.col);
            parser_advance(p);
        }

        expect(p, TOK_IDENT, "expected field name");

        const char *fname = p->previous.start;
        int fname_len = p->previous.len;
        expect(p, TOK_COLON, "expected ':' after field name");
        AstNode *value = parse_expression(p);

        ast_list_push(&fields, make_field(fname, fname_len, type_expr, value, field_line, field_col));
        if (!match(p, TOK_COMMA)) break;
    }

    expect(p, TOK_RBRACE, "expected '}'");

    int count;
    AstNode **items = ast_list_finish(&fields, &count);
    return make_struct_decl(name, name_len, items, count, line, col);
}

static AstNode* parse_enum_decl(Parser *p) {
    int line = p->current.line, col = p->current.col;
    expect(p, TOK_ENUM, "expected 'enum'");
    expect(p, TOK_IDENT, "expected enum name");
    const char *name = p->previous.start;
    int name_len = p->previous.len;
    expect(p, TOK_LBRACE, "expected '{'");

    AstList variants;
    ast_list_init(&variants);
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        int vline = p->current.line, vcol = p->current.col;
        expect(p, TOK_IDENT, "expected enum variant name");
        const char *vname = p->previous.start;
        int vlen = p->previous.len;
        AstNode *value = nullptr;
        if (match(p, TOK_COLON)) {
            value = parse_expression(p);
        }
        ast_list_push(&variants, make_field(vname, vlen, nullptr, value, vline, vcol));
        if (!match(p, TOK_COMMA)) break;
    }
    expect(p, TOK_RBRACE, "expected '}'");
    int count;
    AstNode **items = ast_list_finish(&variants, &count);
    return make_enum_decl(name, name_len, items, count, line, col);
}

static AstNode* parse_type_block(Parser *p) {
    int line = p->current.line, col = p->current.col;

    TokenType type_annotation = p->current.type;
    AstNode *field_type = make_ident(p->current.start, p->current.len, p->current.line, p->current.col);
    parser_advance(p);

    expect(p, TOK_LBRACE, "expected '{' after type in mass type declaration");

    AstList decls;
    ast_list_init(&decls);

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        expect(p, TOK_IDENT, "expected identifier in mass type declaration");
        const char *name = p->previous.start;
        int name_len = p->previous.len;

        bool is_immutable = false;
        if (match(p, TOK_MUT)) {
            is_immutable = false;
        } else if (match(p, TOK_IMMUT)) {
            is_immutable = true;
        } else {
            parser_error(p, "expected ':=' or '::' in mass type declaration");
        }

        AstNode *value = parse_expression(p);

        if (match(p, TOK_SEMICOLON)) {
        } else if (p->current.preceded_nl || check(p, TOK_RBRACE) || check(p, TOK_EOF)) {
        } else {
            parser_error(p, "expected newline or ';' after declaration in type block");
        }

        AstNode *decl = is_immutable
            ? make_immut_decl(name, name_len, true, field_type, value, line, col)
            : make_mut_decl(name, name_len, true, field_type, value, line, col);
            
        ast_list_push(&decls, decl);
    }

    expect(p, TOK_RBRACE, "expected '}' after mass type declaration");

    int count;
    AstNode **items = ast_list_finish(&decls, &count);
    return make_type_block(type_annotation, items, count, line, col);
}

// Directives

static AstNode* parse_primitive(Parser *p) {
    int line = p->current.line, col = p->current.col;
    expect(p, TOK_PRIMITIVE, "expected '#primitive'");

    expect(p, TOK_IDENT, "expected primitive name after '#primitive'");
    const char *name = p->previous.start;
    int name_len = p->previous.len;

    expect(p, TOK_IMMUT, "expected '::' after primitive name");

    int param_count;
    AstNode **params = parse_param_list(p, &param_count);
    AstNode *return_type = parse_optional_return(p);

    if (!match(p, TOK_SEMICOLON) && !p->current.preceded_nl
        && !check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        parser_error(p, "expected newline or ';' after primitive declaration");
    }

    return make_primitive(name, name_len, params, param_count, return_type, line, col);
}

static AstNode* parse_method(Parser *p) {
    int line = p->current.line, col = p->current.col;
    expect(p, TOK_METHOD, "expected '#method'");

    const char *name;
    int name_len;
    bool is_mut;

    if (check(p, TOK_FN)) {
        // Immutable functions
        parser_advance(p);
        expect(p, TOK_IDENT, "expected method name after 'fn'");
        name = p->previous.start;
        name_len = p->previous.len;
        expect(p, TOK_IMMUT, "expected '::' after method name");
        is_mut = false;
    } else {
        expect(p, TOK_IDENT, "expected method name after '#method");
        name = p->previous.start;
        name_len = p->previous.len;

        if (check(p, TOK_ASSIGN)) {
            // Reassign
            parser_advance(p);
            AstNode *value = parse_expression(p);

            if (!match(p, TOK_SEMICOLON) && !p->current.preceded_nl && !check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
                parser_error(p, "expected newline or ';' after method reassignment");
            }

            AstNode *n = make_assign(make_ident(name, name_len, line, col), TOK_ASSIGN, value, line, col);
            n->as.assign.is_method = true;
            return n;
        }

        // Mmutable functions
        expect(p, TOK_MUT, "expected ':=' after method name");
        expect(p, TOK_FN, "expected 'fn' after ':='");
        is_mut = true;
    }

    int param_count;
    AstNode **params = parse_param_list(p, &param_count);
    AstNode *return_type = parse_optional_return(p);
    AstNode *body = parse_block(p);

    AstNode *n = make_method(name, name_len, params, param_count, return_type, body, line, col);
    n->as.method.is_mut = is_mut;
    return n;
}