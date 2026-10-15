static void pt_declare_name(const char *name, int len, bool bleeds) {
    Value v;
    if ((env_get(global_env, name, len, &v) && v.type == ValueType::Fn && ((ObjFn*)v.as.obj)->is_native)) {
        print_err("reserved", len, name);
        return;
    }
    env_define(pt_check_env, name, len, make_null(), false);
    if (bleeds) env_define(pt_bleed_env, name, len, make_null(), false);
}

static bool pt_name_resolves(const char *name, int len) {
    Value dummy;
    if (env_get(pt_check_env, name, len, &dummy)) return true;
    if (env_get(pt_bleed_env, name, len, &dummy)) return true;
    if (find_struct_decl(name, len)) return true;
    if (find_enum_decl(name, len)) return true;
    if (find_primitive(name, len)) return true;
    return false;
}

static void pt_check_expr(AstNode *e) {
    if (!e) return;
    current_line = e->line;
    current_col = e->col;
    switch (e->type) {
        case IDENT: {
            if (e->len == 4 && strncmp(e->name, "self", 4) == 0) {
                if (pt_check_method_depth == 0) print_err("self_outside_method");
                break;
            }
            ValueType vt;
            if (ident_is_type_name(e->name, e->len, &vt)) break;
            Value v;
            if (env_get(global_env, e->name, e->len, &v) && v.type == ValueType::Fn && ((ObjFn*)v.as.obj)->is_native) {
                print_err("undefined", e->len, e->name);
                break;
            }
            if (!pt_name_resolves(e->name, e->len))
                print_err("undefined", e->len, e->name);
            break;
        }
        case BINARY:
            pt_check_expr(e->left);
            pt_check_expr(e->right);
            break;
        case UNARY:
            pt_check_expr(e->operand);
            break;
        case POSTFIX:
            pt_check_expr(e->operand);
            break;
        case TERNARY:
            pt_check_expr(e->cond);
            pt_check_expr(e->then_expr);
            pt_check_expr(e->else_expr);
            break;
        case CALL: {
            bool callee_is_native = false;
            if (e->callee->type == IDENT) {
                Value v;
                callee_is_native = env_get(global_env, e->callee->name, e->callee->len, &v) && v.type == ValueType::Fn && ((ObjFn*)v.as.obj)->is_native;
            }
            if (!callee_is_native) pt_check_expr(e->callee);
            for (int i = 0; i < e->arg_count; i++) pt_check_expr(e->args[i]);
            break;
        }
        case INDEX:
            pt_check_expr(e->object);
            for (int i = 0; i < e->index_count; i++) pt_check_expr(e->indices[i]);
            break;
        case FIELD_ACCESS:
            pt_check_expr(e->object);
            break;
        case ARRAY_LIT:
            for (int i = 0; i < e->count; i++) {
                AstNode *item = e->items[i];
                if (item->type == ARRAY_ENTRY) {
                    pt_check_expr(item->key);
                    pt_check_expr(item->value);
                } else {
                    pt_check_expr(item);
                }
            }
            break;
        case STRUCT_LIT:
            for (int i = 0; i < e->count; i++) {
                AstNode *f = e->fields[i];
                if (f->value && f->value->type == FN_LIT) {
                    pt_check_method_depth++;
                    pt_check_expr(f->value);
                    pt_check_method_depth--;
                } else {
                    pt_check_expr(f->value);
                }
            }
            break;
        case FN_LIT: {
            Env *saved = pt_check_env;
            pt_check_env = env_new(pt_check_env);
            for (int i = 0; i < e->param_count; i++) {
                AstNode *p = e->params[i];
                if (p->is_variadic) {
                    pt_declare_name("argc", 4);
                    pt_declare_name("argv", 4);
                    continue;
                }
                if (p->value) pt_check_expr(p->value);
                pt_declare_name(p->name, p->len);
            }
            pt_walk_stmt(e->body, nullptr, true);
            pt_check_env = saved;
            break;
        }
        default:
            break;
    }
}

static void pt_walk_stmt(AstNode *stmt, AstNode *owner_try, bool checking) {
    if (!stmt) return;
    if (checking) { current_line = stmt->line; current_col = stmt->col; }
    switch (stmt->type) {
        case IMMUT_DECL:
            if (checking) {
                pt_check_expr(stmt->value);
                pt_declare_name(stmt->name, stmt->len);
            } else {
                pt_add_node(stmt->name, stmt->len, owner_try ? owner_try : stmt, stmt->value);
            }
            break;

        case MUT_DECL:
            if (checking) {
                pt_check_expr(stmt->value);
                pt_declare_name(stmt->name, stmt->len, true);
            }
            break;

        case DESTRUCTURE_DECL:
            if (checking) pt_check_expr(stmt->value);
            for (int i = 0; i < stmt->target_count; i++) {
                AstNode *target = stmt->targets[i];
                if (!checking && stmt->is_immutable) {
                    pt_add_node(target->name, target->len, owner_try ? owner_try : stmt, stmt->value);
                }
                if (checking) pt_declare_name(target->name, target->len, !stmt->is_immutable);
            }
            break;

        case ENUM_DECL:
            if (!checking)
                pt_add_enum_node(stmt->name, stmt->len, stmt, owner_try ? owner_try : stmt);
            break;
            
        case ASSIGN:
            if (checking) {
                if (stmt->target->type != IDENT && stmt->target->type != FIELD_ACCESS && stmt->target->type != INDEX && stmt->target->type != POSTFIX) {
                    print_err("ident_assignment");
                } else {
                    pt_check_expr(stmt->target);
                }
                int saved_depth = pt_check_method_depth;
                if (stmt->is_method) pt_check_method_depth++;
                pt_check_expr(stmt->value);
                pt_check_method_depth = saved_depth;
            }
            break;

        case EXPR_STMT:
            if (checking) pt_check_expr(stmt->expr);
            break;

        case RETURN_STMT: if (checking) pt_check_expr(stmt->value); break;
        case DEFER_STMT:  if (checking) pt_check_expr(stmt->value); break;

        case FN_DECL: {
            if (!checking) {
                pt_add_node(stmt->name, stmt->len, owner_try ? owner_try : stmt, nullptr);
                break;
            }
            pt_declare_name(stmt->name, stmt->len);
            Env *saved = pt_check_env;
            pt_check_env = env_new(pt_check_env);
            for (int i = 0; i < stmt->param_count; i++) {
                AstNode *p = stmt->params[i];
                if (p->is_variadic) {
                    pt_declare_name("argc", 4);
                    pt_declare_name("argv", 4);
                    continue;
                }
                if (p->value) pt_check_expr(p->value);
                pt_declare_name(p->name, p->len);
            }
            pt_walk_stmt(stmt->body, owner_try, checking);
            pt_check_env = saved;
            break;
        }

        case BLOCK:
            for (int i = 0; i < stmt->stmt_count; i++)
                pt_walk_stmt(stmt->stmts[i], owner_try, checking);
            break;

        case IF_STMT:
            if (checking) pt_check_expr(stmt->cond);
            pt_walk_stmt(stmt->then_branch, owner_try, checking);
            pt_walk_stmt(stmt->else_branch, owner_try, checking);
            break;

        case WHILE_STMT:
            if (checking) pt_check_expr(stmt->cond);
            pt_walk_stmt(stmt->body, owner_try, checking);
            break;

        case FOR_CSTYLE: {
            Env *saved = pt_check_env;
            if (checking) {
                pt_check_env = env_new(pt_check_env);
                pt_check_expr(stmt->init->value);
                pt_declare_name(stmt->init->name, stmt->init->len);
                pt_check_expr(stmt->cond);
                pt_check_expr(stmt->incr);
            }
            pt_walk_stmt(stmt->body, owner_try, checking);
            if (checking) pt_check_env = saved;
            break;
        }

        case FOR_RANGE:
            if (checking) {
                pt_check_expr(stmt->start);
                pt_check_expr(stmt->end);
            }
            pt_walk_stmt(stmt->body, owner_try, checking);
            break;

        case FOR_IN: {
            Env *saved = pt_check_env;
            if (checking) {
                pt_check_expr(stmt->iterable);
                pt_check_env = env_new(pt_check_env);
                pt_declare_name(stmt->key_name, stmt->key_len);
                if (stmt->val_name) pt_declare_name(stmt->val_name, stmt->val_len);
            }
            pt_walk_stmt(stmt->body, owner_try, checking);
            if (checking) pt_check_env = saved;
            break;
        }

        case SWITCH_STMT:
            if (checking) pt_check_expr(stmt->scrutinee);
            if (checking && stmt->guard) pt_check_expr(stmt->guard);
            for (int i = 0; i < stmt->case_count; i++) {
                AstNode *c = stmt->cases[i];
                if (checking && c->label->type != DEFAULT_STMT) pt_check_expr(c->label);
                pt_walk_stmt(c->body, owner_try, checking);
            }
            break;

        case TRY_STMT: {
            pt_walk_stmt(stmt->try_block, stmt, checking);
            Env *saved = pt_check_env;
            if (checking) {
                pt_check_env = env_new(pt_check_env);
                pt_declare_name(stmt->catch_name, stmt->catch_len);
            }
            pt_walk_stmt(stmt->catch_block, stmt, checking);
            if (checking) pt_check_env = saved;
            break;
        }

        case TYPE_BLOCK:
            for (int i = 0; i < stmt->decl_count; i++)
                pt_walk_stmt(stmt->decls[i], owner_try, checking);
            break;

        case IMPORT: {
            if (!checking) break;
            std::string path(stmt->path, (size_t)stmt->path_len);
            size_t colon = path.find(':');
            bool has_alias = stmt->alias != nullptr;

            if (!has_alias && colon == std::string::npos) {
                fs::path rel = fs::path(current_file).parent_path() / path;
                std::string source;
                std::string import_path = fs::exists(rel) ? rel.string() : path;
                if (!read_file(import_path.c_str(), source)) break;
                char *heap_source = (char*)arena_alloc((int)source.length() + 1);
                memcpy(heap_source, source.c_str(), source.length() + 1);

                Lexer import_lx = { heap_source, heap_source, 1, 1 };
                Parser import_p = {};
                import_p.lx = &import_lx;
                import_p.had_error = false;
                import_p.lookahead_count = 0;
                parser_advance(&import_p);
                AstNode *import_program = parse_program(&import_p);
                if (import_p.had_error) break;

                for (int i = 0; i < import_program->stmt_count; i++) {
                    AstNode *inner = import_program->stmts[i];
                    const char *name = nullptr; int len = 0;
                    switch (inner->type) {
                        case IMMUT_DECL: case MUT_DECL: name = inner->name; len = inner->len; break;
                        case FN_DECL:     name = inner->name; len = inner->len; break;
                        case STRUCT_DECL: name = inner->name; len = inner->len; break;
                        case ENUM_DECL:   name = inner->name; len = inner->len; break;
                        default: break;
                    }
                    if (name) pt_declare_name(name, len);
                }
                break;
            }

            const char *alias; int alias_len;
            if (has_alias) {
                alias = stmt->alias; alias_len = stmt->alias_len;
            } else {
                std::string mod = path.substr(colon + 1);
                char *heap = (char*)arena_alloc((int)mod.length());
                memcpy(heap, mod.data(), mod.length());
                alias = heap; alias_len = (int)mod.length();
            }
            pt_declare_name(alias, alias_len);
            break;
        }

        case METHOD:
            if (checking) pt_declare_name(stmt->name, stmt->len);
            break;

        case STRUCT_DECL:
            if (checking && !find_struct_decl(stmt->name, stmt->len))
                register_struct_decl(stmt);
            break;

        case PRIMITIVE:
            if (!checking && stmt->body)
                pt_add_node(stmt->name, stmt->len, stmt, nullptr);
            break;

        case PROCTIME_BLOCK:
            break;

        default:
            break;
    }
}

static void pt_collect_decls(AstNode *program) {
    for (int i = 0; i < program->stmt_count; i++)
        pt_walk_stmt(program->stmts[i], nullptr, false);
}

static void pt_check_structure(AstNode *program) {
    pt_check_env = env_new(global_env);
    pt_bleed_env = env_new(global_env);
    pt_check_method_depth = 0;

    for (int i = 0; i < program->stmt_count; i++) {
        AstNode *stmt = program->stmts[i];
        if (stmt->is_proctime) continue;
        pt_walk_stmt(stmt, nullptr, true);
    }
}

static bool pt_mark_owned(AstNode *stmt) {
    if (!stmt) return true;
    switch (stmt->type) {
        case IMMUT_DECL: case ENUM_DECL: stmt->is_proctime = true; return true;
        case DESTRUCTURE_DECL:
            if (stmt->is_immutable) { stmt->is_proctime = true; return true; }
            return false;
        case BLOCK: {
            bool all = true;
            for (int i = 0; i < stmt->stmt_count; i++)
                if (!pt_mark_owned(stmt->stmts[i])) all = false;
            return all;
        }
        case IF_STMT: {
            bool a = pt_mark_owned(stmt->then_branch);
            bool b = pt_mark_owned(stmt->else_branch);
            return a && b;
        }
        case WHILE_STMT:   return pt_mark_owned(stmt->body);
        case FOR_CSTYLE:   return pt_mark_owned(stmt->body);
        case FOR_RANGE:    return pt_mark_owned(stmt->body);
        case FOR_IN:       return pt_mark_owned(stmt->body);
        case SWITCH_STMT: {
            bool all = true;
            for (int i = 0; i < stmt->case_count; i++)
                if (!pt_mark_owned(stmt->cases[i]->body)) all = false;
            return all;
        }
        case TRY_STMT: {
            bool try_fully_resolved = pt_mark_owned(stmt->try_block);
            pt_mark_owned(stmt->catch_block);
            return try_fully_resolved;
        }
        default: return false;
    }
}

static void run_proctime(AstNode *program) {
    if (!global_env) global_env = env_new(nullptr);
    pt_collect_decls(program);
    AstNode **order;
    int order_count;
    if (!pt_toposort(&order, &order_count)) return;

    FILE *kept = open_memstream(&proctime_kept_buf, &proctime_kept_size);
    for (int i = 0; i < order_count; i++) {
        char *entry_buf = nullptr;
        size_t entry_size = 0;
        program_output = open_memstream(&entry_buf, &entry_size);
        int errors_before = error_count;
        exec_stmt(order[i]);
        bool keep;
        if (order[i]->type == TRY_STMT) {
            keep = error_count == errors_before;
            if (keep) order[i]->is_proctime = true;
        } else {
            order[i]->is_proctime = true;
            keep = true;
        }
        fclose(program_output);
        if (keep) fwrite(entry_buf, 1, entry_size, kept);
        free(entry_buf);
    }
    fclose(kept);
    program_output = nullptr;
}