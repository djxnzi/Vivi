static AstNode* parse_statement(Parser *p) {
    int before = error_count;
    AstNode *stmt = parse_inner(p);
    if (error_count > before) sync(p);
    return stmt;
}

static AstNode* parse_inner(Parser *p) {
    bool want_local = check(p, TOK_LOCAL);
    bool want_static = check(p, TOK_STATIC);
    if (want_local || want_static) parser_advance(p);

    if (!want_local && !want_static) {
        if (check(p, TOK_IF))       return parse_if_stmt(p);
        if (check(p, TOK_WHILE))    return parse_while_stmt(p);
        if (check(p, TOK_SWITCH))   return parse_switch_stmt(p);
        if (check(p, TOK_FOR))      return parse_for_dispatch(p);
        if (check(p, TOK_FN)) {
            if (peek_at(p, 1).type == TOK_LPAREN) return parse_lambda(p);
            return parse_fn_decl(p);
        }
        if (check(p, TOK_IMPORT))       return parse_import(p);
        if (check(p, TOK_PRIMITIVE))    return parse_primitive(p);
        if (check(p, TOK_PROCTIME))     return parse_proctime_stmt(p);
        if (check(p, TOK_METHOD))       return parse_method(p);
        if (check(p, TOK_STRUCT))       return parse_struct_decl(p);
        if (check(p, TOK_ENUM))         return parse_enum_decl(p);
        if (check(p, TOK_TRY))          return parse_try_stmt(p);

        if (check(p, TOK_RETURN))      return parse_return_stmt(p, p->current.line, p->current.col);
        if (check(p, TOK_DEFER))       return parse_defer_stmt(p, p->current.line, p->current.col);
        if (check(p, TOK_BREAK))       return parse_break_stmt(p, p->current.line, p->current.col);
        if (check(p, TOK_CONTINUE))    return parse_continue_stmt(p, p->current.line, p->current.col);
        if (check(p, TOK_FALLTHROUGH)) return parse_fallthrough_stmt(p, p->current.line, p->current.col);
    }

    bool current_is_type_name = is_type_name_token(p->current);
    if (is_type_keyword(p->current.type) || current_is_type_name) {
        Token next = peek_at(p, 1);
        if (next.type == TOK_LBRACKET || next.type == TOK_STAR) {
            AstNode *node = parse_decl(p);
            node->is_local = want_local;
            node->is_static = want_static;
            return node;
        }
        if (next.type == TOK_IDENT) {
            Token after_name = peek_at(p, 2);
            AstNode *node = (after_name.type == TOK_COMMA) ? parse_destructure_decl(p) : parse_decl(p);
            node->is_local = want_local;
            node->is_static = want_static;
            return node;
        }
        if (next.type == TOK_LBRACE) return parse_type_block(p);
        if (!current_is_type_name && (next.type == TOK_MUT || next.type == TOK_IMMUT || is_type_keyword(next.type))) {
            print_err("reserved_type", next.len, next.start);
            return make_expr_stmt(make_null_lit(p->current.line, p->current.col), p->current.line, p->current.col);
        }
    }

    if (check(p, TOK_IDENT)) {
        Token next = peek_at(p, 1);
        AstNode *node = nullptr;
        if (next.type == TOK_COMMA) {
            node = parse_destructure_decl(p);
        } else if (next.type == TOK_IDENT) {
            Token after_name = peek_at(p, 2);
            node = (after_name.type == TOK_COMMA) ? parse_destructure_decl(p) : parse_decl(p);
        } else if ((next.type == TOK_STAR && peek_at(p, 2).type == TOK_IDENT) || next.type == TOK_MUT || next.type == TOK_IMMUT) {
            node = parse_decl(p);
        }
        if (node) {
            node->is_local = want_local;
            node->is_static = want_static;
            return node;
        }
    }

    if (want_local || want_static) {
        print_err("local_static_decl");
    }
    return parse_expr_statement(p);
}

static AstNode* parse_block(Parser *p) {
    int line = p->current.line, col = p->current.col;
    expect(p, TOK_LBRACE, "expected '{'");

    AstList stmts;
    ast_list_init(&stmts);

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        ast_list_push(&stmts, parse_statement(p));
    }

    expect(p, TOK_RBRACE, "expected '}' after block");
    return make_block(&stmts, line, col);
}

static AstNode* parse_program(Parser *p) {
    int line = p->current.line, col = p->current.col;

    AstList stmts;
    ast_list_init(&stmts);

    while (!check(p, TOK_EOF)) {
        ast_list_push(&stmts, parse_statement(p));
    }

    return make_program(&stmts, line, col);
}

static AstNode* parse_expr_statement(Parser *p) {
    int line = p->current.line, col = p->current.col;
    AstNode *expr = parse_expression(p);

    if (expr->type == IDENT && (check(p, TOK_MUT) || check(p, TOK_IMMUT))) {
        bool is_immutable = check(p, TOK_IMMUT);
        parser_advance(p);
        AstNode *value = parse_expression(p);

        if (match(p, TOK_SEMICOLON)) {
        } else if (p->current.preceded_nl || check(p, TOK_RBRACE) || check(p, TOK_EOF)) {
        } else {
            parser_error(p, "expected newline or ';' after declaration");
        }

        return is_immutable
            ? make_immut_decl(expr->as.ident.name, expr->as.ident.len, false, nullptr, value, line, col)
            : make_mut_decl(expr->as.ident.name, expr->as.ident.len, false, nullptr, value, line, col);
    }

    if (is_assign_op(p->current.type)) {
        TokenType op = p->current.type;
        parser_advance(p);
        AstNode *value = parse_expression(p);

        if (match(p, TOK_SEMICOLON)) {
        } else if (p->current.preceded_nl || check(p, TOK_RBRACE) || check(p, TOK_EOF) || check(p, TOK_ELSE)) {
        } else {
            parser_error(p, "expected newline or ';' after assignment");
        }

        return make_assign(expr, op, value, line, col);
    }

    if (match(p, TOK_SEMICOLON)) {
    } else if (p->current.preceded_nl || check(p, TOK_RBRACE) || check(p, TOK_EOF) || check(p, TOK_ELSE)) {
    } else {
        parser_error(p, "expected newline or ';' after expression statement");
    }

    return make_expr_stmt(expr, line, col);
}

static AstNode* parse_if_stmt(Parser *p) {
    int line = p->current.line, col = p->current.col;
    expect(p, TOK_IF, "expected 'if'");

    AstNode *cond = parse_expression(p);
    AstNode *then_branch = parse_if_branch(p);

    AstNode *else_branch = nullptr;
    if (match(p, TOK_ELSE)) {
        if (check(p, TOK_IF)) {
            else_branch = parse_if_stmt(p);
        } else {
            else_branch = parse_if_branch(p);
        }
    }

    return make_if_stmt(cond, then_branch, else_branch, line, col);
}

static AstNode* parse_if_branch(Parser *p) {
    if (check(p, TOK_LBRACE)) {
        return parse_block(p);
    }

    if (p->current.preceded_nl) {
        parser_error(p, "expected '{' or a statement on the same line");
    }

    int line = p->current.line, col = p->current.col;
    AstList stmts;
    ast_list_init(&stmts);

    ast_list_push(&stmts, parse_statement(p));
    while (!p->current.preceded_nl && !check(p, TOK_RBRACE) && !check(p, TOK_EOF) && !check(p, TOK_ELSE)) {
        ast_list_push(&stmts, parse_statement(p));
    }

    return make_block(&stmts, line, col);
}

static AstNode* parse_while_stmt(Parser *p) {
    int line = p->current.line, col = p->current.col;
    expect(p, TOK_WHILE, "expected 'while'");

    AstNode *cond = parse_expression(p);
    AstNode *body = parse_block(p);

    return make_while_stmt(cond, body, line, col);
}

static AstNode* parse_switch_stmt(Parser *p) {
    int line = p->current.line, col = p->current.col;
    expect(p, TOK_SWITCH, "expected 'switch'");

    AstNode *scrutinee = parse_expression(p);
    AstNode *guard = nullptr;
    if (scrutinee->type == BINARY && scrutinee->as.binary.op == TOK_IN) {
        guard = scrutinee->as.binary.right;
        scrutinee = scrutinee->as.binary.left;
    }
    AstList cases;
    ast_list_init(&cases);

    expect(p, TOK_LBRACE, "expected '{'");

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        ast_list_push(&cases, parse_switch_case(p));
    }

    expect(p, TOK_RBRACE, "expected '}'");

    return make_switch_stmt(scrutinee, guard, &cases, line, col);
}

static AstNode* parse_switch_case(Parser *p) {
    int line = p->current.line, col = p->current.col;

    AstNode *label;

    if (check(p, TOK_DEFAULT)) {
        parser_advance(p);
        label = make_default_stmt(line, col);
    } else {
        label = parse_expression(p);
    }

    expect(p, TOK_COLON, "expected ':' after switch label");

    AstList stmts;
    ast_list_init(&stmts);

    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        if (p->current.preceded_nl &&
            (check(p, TOK_INT_LIT) ||
            check(p, TOK_FLOAT_LIT) ||
            check(p, TOK_STR_LIT) ||
            check(p, TOK_RUNE_LIT) ||
            check(p, TOK_IDENT) ||
            check(p, TOK_TRUE) ||
            check(p, TOK_FALSE) ||
            check(p, TOK_NULL) ||
            check(p, TOK_TYPE_RUNE) ||
            check(p, TOK_TYPE_STR) ||
            check(p, TOK_TYPE_BOOL) ||
            check(p, TOK_TYPE_I8) ||
            check(p, TOK_TYPE_I16) ||
            check(p, TOK_TYPE_I32) ||
            check(p, TOK_TYPE_I64) ||
            check(p, TOK_TYPE_U8) ||
            check(p, TOK_TYPE_U16) ||
            check(p, TOK_TYPE_U32) ||
            check(p, TOK_TYPE_U64) ||
            check(p, TOK_TYPE_F32) ||
            check(p, TOK_TYPE_F64) ||
            check(p, TOK_DEFAULT)))
        {
            Token next = peek_at(p, 1);
            if (next.type == TOK_COLON) {
                break;
            }
            if (next.type == TOK_SLICE) {
                Token after_slice_end = peek_at(p, 3);
                if (after_slice_end.type == TOK_COLON) {
                    break;
                }
            }
        }

        ast_list_push(&stmts, parse_statement(p));
    }

    AstNode *body = make_block(&stmts, line, col);

    return make_switch_case(label, body, line, col);
}

static AstNode* parse_for_init_decl(Parser *p) {
    int line = p->current.line, col = p->current.col;

    AstNode *type_annotation = nullptr;
    if (is_type_keyword(p->current.type)) {
        type_annotation = make_ident(p->current.start, p->current.len, p->current.line, p->current.col);
        parser_advance(p);
    }

    expect(p, TOK_IDENT, "expected identifier in for-loop init");
    const char *name = p->previous.start;
    int name_len = p->previous.len;

    if (check(p, TOK_IMMUT)) {
        parser_error(p, "for-loop init must use ':=', not '::' (loop counters can't be proctime-immutable)");
        parser_advance(p);
    } else {
        expect(p, TOK_MUT, "expected ':=' in for-loop init");
    }

    AstNode *value = parse_expression(p);

    return make_mut_decl(name, name_len, type_annotation != nullptr, type_annotation, value, line, col);
}

static AstNode* parse_for_in(Parser *p, int line, int col, bool has_parens) {
    expect(p, TOK_IDENT, "expected identifier in for-in loop");
    const char *key_name = p->previous.start;
    int key_len = p->previous.len;

    expect(p, TOK_COMMA, "expected ',' after first identifier in for-in loop");

    expect(p, TOK_IDENT, "expected second identifier in for-in loop");
    const char *val_name = p->previous.start;
    int val_len = p->previous.len;

    expect(p, TOK_IN, "expected 'in' in for-in loop");
    AstNode *iterable = parse_expression(p);

    if (has_parens) {
        expect(p, TOK_RPAREN, "expected ')' to close for-loop header");
    }

    AstNode *body = parse_block(p);
    return make_for_in(key_name, key_len, val_name, val_len, iterable, body, line, col);
}

static AstNode* parse_for_cstyle(Parser *p, int line, int col, bool has_parens) {
    AstNode *init = parse_for_init_decl(p);
    expect(p, TOK_SEMICOLON, "expected ';' after for-loop init");

    AstNode *cond = parse_expression(p);
    expect(p, TOK_SEMICOLON, "expected ';' after for-loop condition");

    AstNode *incr = parse_expression(p);

    if (has_parens) {
        expect(p, TOK_RPAREN, "expected ')' to close for-loop header");
    }

    AstNode *body = parse_block(p);
    return make_for_cstyle(init, cond, incr, body, line, col);
}

static AstNode* parse_for_range(Parser *p, int line, int col, bool has_parens) {
    AstNode *start = parse_binary(p, (Precedence)(PREC_SLICE + 1));

    AstNode *end = nullptr;
    if (match(p, TOK_SLICE)) {
        end = parse_binary(p, (Precedence)(PREC_SLICE + 1));
    }

    if (has_parens) {
        expect(p, TOK_RPAREN, "expected ')' to close for-loop header");
    }

    AstNode *body = parse_block(p);
    return make_for_range(start, end, body, line, col);
}

static AstNode* parse_for_in_single(Parser *p, int line, int col, bool has_parens) {
    expect(p, TOK_IDENT, "expected identifier in for-in loop");
    const char *name = p->previous.start;
    int name_len = p->previous.len;

    expect(p, TOK_IN, "expected 'in' in for-in loop");
    AstNode *iterable = parse_expression(p);

    if (has_parens) {
        expect(p, TOK_RPAREN, "expected ')' to close for-loop header");
    }

    AstNode *body = parse_block(p);
    return make_for_in(name, name_len, nullptr, 0, iterable, body, line, col);
}

static AstNode* parse_for_dispatch(Parser *p) {
    int line = p->current.line, col = p->current.col;
    parser_advance(p);

    bool has_parens = match(p, TOK_LPAREN);

    if (check(p, TOK_IDENT)) {
        Token after = peek_at(p, 1);
        if (after.type == TOK_COMMA) return parse_for_in(p, line, col, has_parens);
        if (after.type == TOK_MUT || after.type == TOK_IMMUT) return parse_for_cstyle(p, line, col, has_parens);
        if (after.type == TOK_IN) return parse_for_in_single(p, line, col, has_parens);
    }
    return parse_for_range(p, line, col, has_parens);
}

static AstNode* parse_return_stmt(Parser *p, int line, int col) {
    expect(p, TOK_RETURN, "expected 'return'");

    AstNode *value = nullptr;
    if (!(p->current.preceded_nl || check(p, TOK_RBRACE) || check(p, TOK_EOF))) {
        value = parse_expression(p);
        if (is_assign_op(p->current.type)) {
            TokenType op = p->current.type;
            parser_advance(p);
            AstNode *rhs = parse_expression(p);
            value = make_assign(value, op, rhs, line, col);
        } else if (check(p, TOK_COMMA)) {
            AstList items;
            ast_list_init(&items);
            ast_list_push(&items, value);
            while (match(p, TOK_COMMA)) {
                ast_list_push(&items, parse_expression(p));
            }
            int count;
            AstNode **elems = ast_list_finish(&items, &count);
            value = make_array_lit(elems, count, line, col);
        }
    }

    return make_return_stmt(value, line, col);
}

static AstNode* parse_defer_stmt(Parser *p, int line, int col) {
    expect(p, TOK_DEFER, "expected 'defer'");

    AstNode *value = nullptr;
    if (!(p->current.preceded_nl || check(p, TOK_EOF))) {
        value = parse_expression(p);
    }

    return make_defer_stmt(value, line, col);
}

static AstNode* parse_break_stmt(Parser *p, int line, int col) {
    expect(p, TOK_BREAK, "expected 'break'");
    return make_break_stmt(line, col);
}

static AstNode* parse_continue_stmt(Parser *p, int line, int col) {
    expect(p, TOK_CONTINUE, "expected 'continue'");
    return make_continue_stmt(line, col);
}

static AstNode* parse_fallthrough_stmt(Parser *p, int line, int col) {
    expect(p, TOK_FALLTHROUGH, "expected 'fallthrough'");
    return make_fallthrough_stmt(line, col);
}

static AstNode* parse_try_stmt(Parser *p) {
    int line = p->current.line, col = p->current.col;
    expect(p, TOK_TRY, "expected 'try'");

    AstNode *try_block = parse_block(p);

    expect(p, TOK_CATCH, "expected 'catch' after try block");
    expect(p, TOK_LPAREN, "expected '(' after 'catch'");
    expect(p, TOK_IDENT, "expected error identifier in catch clause");
    const char *catch_name = p->previous.start;
    int catch_len = p->previous.len;
    expect(p, TOK_RPAREN, "expected ')' after catch identifier");

    AstNode *catch_block = parse_block(p);

    return make_try_stmt(try_block, catch_name, catch_len, catch_block, line, col);
}

static AstNode* parse_import(Parser *p) {
    int line = p->current.line, col = p->current.col;
    expect(p, TOK_IMPORT, "expected '#import'");
    expect(p, TOK_STR_LIT, "expected import path string");
    const char *path = p->previous.start;
    int path_len = p->previous.len;

    const char *alias = nullptr;
    int alias_len = 0;
    if (match(p, TOK_AS)) {
        expect(p, TOK_IDENT, "expected alias name after 'as'");
        alias = p->previous.start;
        alias_len = p->previous.len;
    }

    if (match(p, TOK_SEMICOLON)) {
    } else if (p->current.preceded_nl || check(p, TOK_RBRACE) || check(p, TOK_EOF)) {
    } else {
        parser_error(p, "expected newline or ';' after import");
    }

    return make_import(path, path_len, alias, alias_len, line, col);
}

static AstNode* parse_proctime_stmt(Parser *p) {
    expect(p, TOK_PROCTIME, "expected '#proctime'");

    int line = p->current.line, col = p->current.col;
    expect(p, TOK_LBRACE, "expected '{' after '#proctime'");

    AstList stmts;
    ast_list_init(&stmts);
    while (!check(p, TOK_RBRACE) && !check(p, TOK_EOF)) {
        ast_list_push(&stmts, parse_statement(p));
    }
    expect(p, TOK_RBRACE, "expected '}' after #proctime block");

    AstNode *node = make_proctime_block(&stmts, line, col);
    node->is_proctime = true;
    return node;
}