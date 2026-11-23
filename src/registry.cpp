// Words
static void register_keyword(const char *name, int len) {
    if (is_keyword(name, len)) return;
    new_keywords = (NewKeyword*)grow_array(new_keywords, new_keyword_count, &new_keyword_capacity, sizeof(NewKeyword), 8);
    new_keywords[new_keyword_count].name = name;
    new_keywords[new_keyword_count].len = len;
    new_keyword_count++;
}

static bool is_keyword(const char *name, int len) {
    for (int i = 0; i < new_keyword_count; i++)
        if (new_keywords[i].len == len && strncmp(new_keywords[i].name, name, len) == 0) return true;
    return false;
}

static void find_keywords(const char *src) {
    Lexer lx = { src, src, 1, 1 };
    Token t = next_token(&lx);
    while (t.type != TOK_EOF) {
        if (t.type == TOK_PHRASE) {
            Token name = next_token(&lx);
            Token cc   = next_token(&lx);
            if (name.type == TOK_IDENT && cc.type == TOK_IMMUT && next_token(&lx).type == TOK_LPAREN) {
                bool has_block = false;
                int depth = 1;
                while (depth > 0) {
                    Token q = next_token(&lx);
                    if (q.type == TOK_EOF) break;
                    if (q.type == TOK_LPAREN) depth++;
                    else if (q.type == TOK_RPAREN) depth--;
                    else if (q.type == TOK_IDENT && q.len == 5 && strncmp(q.start, "block", 5) == 0) has_block = true;
                }
                if (has_block) register_keyword(name.start, name.len);
            }
        }
        t = next_token(&lx);
    }
}

// Method
static void register_method(const char *name, int len) {
    if (find_method(name, len)) return;
    method_decls = (MethodName*)grow_array(method_decls, method_decl_count, &method_decl_capacity, sizeof(MethodName), 8);
    method_decls[method_decl_count].name = name;
    method_decls[method_decl_count].len = len;
    method_decl_count++;
}

static void unregister_method(const char *name, int len) {
    for (int i = 0; i < method_decl_count; i++) {
        if (method_decls[i].len == len && strncmp(method_decls[i].name, name, len) == 0) {
            method_decls[i] = method_decls[--method_decl_count];
            return;
        }
    }
}

static ObjFn* find_method(const char *name, int len, Value *ns) {
    bool registered = false;

    for (int i = 0; i < method_decl_count; i++) {
        if (method_decls[i].len == len && strncmp(method_decls[i].name, name, len) == 0) {
            registered = true;
            break;
        }
    }

    if (!registered) return nullptr;

    if (ns) {
        Value *slot = struct_field_slot(*ns, name, len);
        return slot && slot->type == ValueType::Fn ? (ObjFn*)slot->as.obj : nullptr;
    }

    Value v;
    if (env_get(global_env, name, len, &v) && v.type == ValueType::Fn) {
        return (ObjFn*)v.as.obj;
    }

    return nullptr;
}

// Structs
static void register_struct_decl(AstNode *decl) {
    AstNode *existing = find_struct_decl(decl->name, decl->len);
    if (existing) {
        if (existing != decl)
            print_err("duplicate_struct", decl->len, decl->name);
        return;
    }
    struct_decls = (AstNode**)grow_array(struct_decls, struct_decl_count, &struct_decl_capacity, sizeof(AstNode*), 8);
    struct_decls[struct_decl_count++] = decl;
}

static AstNode* find_struct_decl(const char *name, int len) {
    for (int i = 0; i < struct_decl_count; i++) {
        AstNode *d = struct_decls[i];
        if (d->len == len && strncmp(d->name, name, len) == 0) {
            return d;
        }
    }
    return nullptr;
}

static bool names_struct(const char *name, int len) {
    Value t;
    if (find_struct_decl(name, len)) return true;
    if (!env_get(global_env, name, len, &t) || t.type != ValueType::Struct) return false;
    ObjStruct *s = (ObjStruct*)t.as.obj;
    return (s->type_name_len == len && strncmp(s->type_name, name, len) == 0)
        || (s->type_name_len == 6 && strncmp(s->type_name, "#flags", 6) == 0);
}


static bool label_matches(AstNode *label, Value scrutinee, AstNode *expect) {
    if (label->type == BINARY && label->op == TOK_SLICE) {
        Value lo = eval_expr(label->left);
        Value hi = eval_expr(label->right);
        if (!is_numeric(scrutinee) || !is_numeric(lo) || !is_numeric(hi)) {
            print_err("non_numeric_range");
            return false;
        }
        return as_double(scrutinee) >= as_double(lo) && as_double(scrutinee) <= as_double(hi);
    }
    if (label->type == IDENT) {
        ValueType vt;
        if (ident_is_type_name(label->name, label->len, &vt)) {
            bool matches = (scrutinee.type == vt);
            if (matches && (vt == ValueType::Int || vt == ValueType::Float)) {
                unsigned char want = (unsigned char)(type_index(label->name, label->len) + 1);
                unsigned char have = scrutinee.ty ? scrutinee.ty : (unsigned char)(type_index(vt == ValueType::Int ? "i32" : "f32", 3) + 1);
                matches = (have == want);
            }
            return matches;
        }
        if (names_struct(label->name, label->len)) {
            if (scrutinee.type != ValueType::Struct) return false;
            ObjStruct *s = (ObjStruct*)scrutinee.as.obj;
            return s->type_name && s->type_name_len == label->len &&
                strncmp(s->type_name, label->name, label->len) == 0;
        }
    }
    return values_equal(eval_expected(label, expect), scrutinee);
}

// Enumerated structs
static bool enum_fields(AstNode *decl, ObjStruct *s) {
    AstNode *lit = decl->value;
    bool flags = (decl->type_tok == TOK_FLAGS);

    bool enum_body = flags || decl->type_tok == TOK_ENUM || (decl->has_type_annotation && is_ident(decl->type_annotation, "enum"));
    for (int i = 0; i < lit->count; i++) {
        if (!lit->fields[i]->value) enum_body = true;
    }

    for (int i = 0; i < lit->count; i++) {
        AstNode *field = lit->fields[i];
        Value v = s->fields[i].value;
        current_line = field->line;
        current_col = field->col;

        if (flags && field->value) {
            print_err("flags_value", decl->len, decl->name);
            return false;
        }

        if (!field->value && v.type == ValueType::Null) {
            print_err("enum_variant", field->len, field->name);
            return false;
        }

        if (enum_body && field->value && v.type != ValueType::Int) {
            print_err("type_mismatch", 4, "enum", value_type_label(v.type));
            return false;
        }
    }

    s->type_name = flags ? "#flags" : decl->name;
    s->type_name_len = flags ? 6 : decl->len;
    
    return true;
}