static AstNode* parse_primary(Parser *p) {
    int line = p->current.line, col = p->current.col;

    if (match(p, TOK_INT_LIT)) {
        return make_int_lit(p->previous.int_val, line, col);
    }
    if (match(p, TOK_FLOAT_LIT)) {
        return make_float_lit(p->previous.float_val, line, col);
    }
    if (match(p, TOK_STR_LIT)) {
        bool has_brace = false;
        for (int k = 0; k < p->previous.len; k++) {
            if (p->previous.start[k] == '{') { has_brace = true; break; }
        }
        if (has_brace) {
            return parse_interpolated_string(p->previous.start, p->previous.len, line, col);
        }
        if (p->previous.len == 1) {
            return make_rune_lit(p->previous.start, p->previous.len, line, col);
        }
        return make_str_lit(p->previous.start, p->previous.len, line, col);
    }
    if (match(p, TOK_RUNE_LIT)) {
        if (p->previous.len != 1) {
            return make_str_lit(p->previous.start, p->previous.len, line, col);
        }
        return make_rune_lit(p->previous.start, p->previous.len, line, col);
    }
    if (match(p, TOK_TRUE)) {
        return make_bool_lit(true, line, col);
    }
    if (match(p, TOK_FALSE)) {
        return make_bool_lit(false, line, col);
    }
    if (match(p, TOK_NULL)) {
        return make_null_lit(line, col);
    }
    if (match(p, TOK_IDENT)) {
        return make_ident(p->previous.start, p->previous.len, line, col);
    }
    if (check(p, TOK_FN)) {
        int line2 = p->current.line, col2 = p->current.col;
        parser_advance(p);
        return parse_fn_lit(p, line2, col2);
    }
    if (is_type_keyword(p->current.type)) {
        parser_advance(p);
        return make_ident(p->previous.start, p->previous.len, line, col);
    }
    if (match(p, TOK_LPAREN)) {
        AstNode *expr = parse_expression(p);
        expect(p, TOK_RPAREN, "expected ')' after expression");
        return expr;
    }
    if (check(p, TOK_LBRACKET)) {
        return parse_bracket_literal(p);
    }
    if (check(p, TOK_LBRACE)) {
        return parse_brace_literal(p);
    }
    if (check(p, TOK_IF)) {
        return parse_if_stmt(p);
    }

    parser_error(p, "expected expression");
    parser_advance(p);
    return make_null_lit(line, col);
}

static AstNode* parse_expression(Parser *p) {
    AstNode *left = parse_union(p);
    return left;
}

static AstNode* parse_expression_no_union(Parser *p) {
    AstNode *left = parse_ternary(p);
    return left;
}


static AstNode* parse_postfix(Parser *p) {
    AstNode *expr = parse_primary(p);

    for (;;) {
        int line = p->current.line, col = p->current.col;

        if (match(p, TOK_LPAREN)) {
            AstList args;
            ast_list_init(&args);
            if (!check(p, TOK_RPAREN)) {
                do {
                    ast_list_push(&args, parse_expression(p));
                } while (match(p, TOK_COMMA));
            }
            expect(p, TOK_RPAREN, "expected ')' after arguments");

            expr = make_call(expr, &args, line, col);
        } else if (match(p, TOK_LBRACKET)) {
            AstList indices;
            ast_list_init(&indices);
            do {
                ast_list_push(&indices, parse_expression(p));
            } while (match(p, TOK_COMMA));
            expect(p, TOK_RBRACKET, "expected ']' after index");
            int idx_count;
            AstNode **idx_items = ast_list_finish(&indices, &idx_count);
            expr = make_index(expr, idx_items, idx_count, line, col);
        } else if (match(p, TOK_DOT)) {
            expect(p, TOK_IDENT, "expected field name after '.'");
            expr = make_field_access(expr, p->previous.start, p->previous.len, line, col);
        } else if (match(p, TOK_PLUSPLUS)) {
            expr = make_postfix(TOK_PLUSPLUS, expr, line, col);
        } else if (match(p, TOK_MINUSMINUS)) {
            expr = make_postfix(TOK_MINUSMINUS, expr, line, col);
        } else if (check(p, TOK_STAR) && (peek_at(p, 1).preceded_nl || !starts_expression(peek_at(p, 1).type))) {
            parser_advance(p);
            expr = make_postfix(TOK_STAR, expr, line, col);
        } else {
            break;
        }
    }

    return expr;
}

static AstNode* parse_unary(Parser *p) {
    int line = p->current.line, col = p->current.col;

    if (check(p, TOK_NOT) || check(p, TOK_MINUS) || check(p, TOK_AMP) || check(p, TOK_TILDE)) {
        TokenType op = p->current.type;
        parser_advance(p);
        AstNode *operand = parse_unary(p);
        return make_unary(op, operand, line, col);
    }

    return parse_postfix(p);
}

static AstNode* parse_binary(Parser *p, Precedence min_prec) {
    AstNode *left = parse_unary(p);

    for (;;) {
        TokenType op = p->current.type;

        if (!is_binary_op(op)) break;

        Precedence prec = binary_precedence(op);
        if (prec < min_prec) break;

        int line = p->current.line, col = p->current.col;
        parser_advance(p);

        AstNode *right = parse_binary(p, (Precedence)(prec + 1));
        left = make_binary(op, left, right, line, col);
    }

    return left;
}

static AstNode* parse_ternary(Parser *p) {
    AstNode *cond = parse_binary(p, PREC_OR);

    if (match(p, TOK_QUESTION)) {
        int line = p->previous.line, col = p->previous.col;
        AstNode *then_expr = parse_expression_no_union(p);
        expect(p, TOK_COLON, "expected ':' in ternary expression");
        AstNode *else_expr = parse_expression_no_union(p);
        return make_ternary(cond, then_expr, else_expr, line, col);
    }

    return cond;
}

static AstNode* parse_union(Parser *p) {
    AstNode *left = parse_ternary(p);

    while (match(p, TOK_PIPE)) {
        int line = p->previous.line, col = p->previous.col;
        AstNode *right = parse_ternary(p);
        left = make_binary(TOK_PIPE, left, right, line, col);
    }

    return left;
}

static AstNode* parse_bracket_literal(Parser *p) {
    int line = p->current.line, col = p->current.col;
    expect(p, TOK_LBRACKET, "expected '['");

    if (match(p, TOK_RBRACKET)) {
        return make_array_lit(nullptr, 0, line, col);
    }

    AstList elems;
    ast_list_init(&elems);
    do {
        int elem_line = p->current.line, elem_col = p->current.col;
        AstNode *first = parse_expression(p);

        if (match(p, TOK_COLON)) {
            AstNode *value = parse_expression(p);
            ast_list_push(&elems, make_array_entry(first, value, elem_line, elem_col));
        } else {
            ast_list_push(&elems, first);
        }
    } while (match(p, TOK_COMMA));
    expect(p, TOK_RBRACKET, "expected ']' after array elements");

    int count;
    AstNode **items = ast_list_finish(&elems, &count);
    return make_array_lit(items, count, line, col);
}

static AstNode* parse_brace_literal(Parser *p) {
    int line = p->current.line, col = p->current.col;
    expect(p, TOK_LBRACE, "expected '{'");

    AstList fields;
    ast_list_init(&fields);

    if (!check(p, TOK_RBRACE)) {
        do {
            int field_line = p->current.line, field_col = p->current.col;
            expect(p, TOK_IDENT, "expected field name in struct literal");
            const char *name = p->previous.start;
            int name_len = p->previous.len;

            expect(p, TOK_COLON, "expected ':' after field name");
            AstNode *value = parse_expression(p);

            ast_list_push(&fields, make_field(name, name_len, nullptr, value, field_line, field_col));
        } while (match(p, TOK_COMMA));
    }

    expect(p, TOK_RBRACE, "expected '}' after struct literal");

    int count;
    AstNode **items = ast_list_finish(&fields, &count);
    return make_struct_lit(items, count, line, col);
}

static AstNode* parse_interpolated_string(const char *text, int len, int line, int col) {
    AstNode *result = nullptr;
    int i = 0;
    int seg_start = 0;

    while (i < len) {
        if (text[i] == '{') {
            if (i > seg_start) {
                AstNode *lit = make_str_lit(text + seg_start, i - seg_start, line, col);
                result = result ? make_binary(TOK_TILDE, result, lit, line, col) : lit;
            }
            int expr_start = i + 1;
            int depth = 1;
            int j = expr_start;
            while (j < len && depth > 0) {
                if (text[j] == '{') depth++;
                else if (text[j] == '}') depth--;
                if (depth > 0) j++;
            }

            int expr_len = j - expr_start;
            char *expr_buf = (char*)arena_alloc(expr_len + 1);
            memcpy(expr_buf, text + expr_start, expr_len);
            expr_buf[expr_len] = '\0';

            Lexer sub_lx = { expr_buf, expr_buf, line, col };
            Parser sub_p = {};
            sub_p.lx = &sub_lx;
            sub_p.had_error = false;
            sub_p.lookahead_count = 0;
            parser_advance(&sub_p);
            AstNode *expr = parse_expression(&sub_p);
            if (sub_p.had_error) {
                print_err("interpolation");
            }

            result = result ? make_binary(TOK_TILDE, result, expr, line, col) : expr;

            i = j + 1;
            seg_start = i;
        } else {
            i++;
        }
    }
    
    if (seg_start > len) seg_start = len;
    if (seg_start < len || result == nullptr || result->type != STR_LIT) {
        AstNode *lit = make_str_lit(text + seg_start, len - seg_start, line, col);
        result = result ? make_binary(TOK_TILDE, result, lit, line, col) : lit;
    }

    return result;
}