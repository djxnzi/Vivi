#include <set>
#include <map>
static std::set<std::string> pt_import_done;
static std::map<AstNode*, ImportUnit*> pt_node_unit;

static void pt_declare_name(const char *name, int len, bool bleeds) {
    EnvEntry *existing = env_find(pt_check_env, name, len);
    if (existing && existing->value.type == ValueType::Fn && ((ObjFn*)existing->value.as.obj)->is_native) {
        print_err("reserved", len, name);
        return;
    }
    if (existing && existing->is_const) {
        print_err("immut_redecl", len, name);
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

            AstNode *a = e->arg_count == 1 ? e->args[0] : nullptr;
            if (a && a->type == UNARY && a->op == TOK_MINUS) a = a->operand;
            ValueType vt;
            if (a && (a->type == INT_LIT || a->type == FLOAT_LIT) && e->callee->type == IDENT
                && ident_is_type_name(e->callee->name, e->callee->len, &vt)) {
                current_line = e->line; current_col = e->col;
                eval_typecast(e->callee->name, e->callee->len, eval_expr(e->args[0]));
            }

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
        case STRUCT_LIT: {
            pt_check_env = env_new(pt_check_env);
            for (int i = 0; i < e->count; i++) {
                AstNode *f = e->fields[i];
                if (f->value && f->value->type == FN_LIT) {
                    pt_check_method_depth++;
                    pt_check_expr(f->value);
                    pt_check_method_depth--;
                } else {
                    pt_check_expr(f->value);
                }
                env_define(pt_check_env, f->name, f->len, make_null(), false);
            }
            pt_check_env = pt_check_env->parent;
            break;
        }
        case FN_LIT: {
            Env *saved = pt_check_env;
            pt_check_env = env_new(pt_check_env);
            bind_implicits(pt_check_env, nullptr, true, nullptr, 0);
            for (int i = 0; i < e->param_count; i++) {
                AstNode *p = e->params[i];
                if (p->is_variadic) continue;
                if (p->value) pt_check_expr(p->value);
                if (!is_flagged(p)) pt_declare_name(p->name, p->len);
            }
            pt_walk_stmt(e->body, nullptr, true);
            pt_check_env = saved;
            break;
        }
        default:
            break;
    }
}

static std::set<std::string> pt_check_import_active;

static void pt_declare_import(AstNode *imp) {
    if (imp->alias) { pt_declare_name(imp->alias, imp->alias_len); return; }
    std::string path(imp->path, (size_t)imp->path_len);
    std::string file_path = resolve_import(path);
    if (pt_check_import_active.count(file_path)) return;
    ImportUnit *u = load_import_unit(path, file_path, false);
    if (!u) return;
    pt_check_import_active.insert(file_path);
    const char *sf = current_file, *ss = program_source;
    current_file = u->path;
    program_source = u->source;
    for (int i = 0; i < u->program->stmt_count; i++) {
        AstNode *inner = u->program->stmts[i];
        if (inner->is_proctime) continue;   // already defined by run_proctime
        switch (inner->type) {
            case IMMUT_DECL: case MUT_DECL: case FN_DECL: case STRUCT_DECL:
                pt_declare_name(inner->name, inner->len); break;
            case IMPORT: pt_declare_import(inner); break;
            case SCOPE:
                if (inner->is_local) break;
                for (int j = 0; j < inner->stmt_count; j++) {
                    AstNode *s = inner->stmts[j];
                    if (s->is_proctime) continue;
                    if (s->type == IMPORT) pt_declare_import(s);
                    else if (s->type == IMMUT_DECL || s->type == MUT_DECL || s->type == FN_DECL || s->type == STRUCT_DECL) pt_declare_name(s->name, s->len);
                }
                break;
            default: break;
        }
    }
    current_file = sf;
    program_source = ss;
    pt_check_import_active.erase(file_path);
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
                ValueType vt;
                if (ident_is_type_name(stmt->name, stmt->len, &vt)) {
                    current_line = stmt->line; current_col = stmt->col;
                    print_err("reserved_type", stmt->len, stmt->name);
                    break;
                }
                if (stmt->type_tok == TOK_FLAGS && stmt->value->type == STRUCT_LIT) {
                    AstNode *first = nullptr;
                    for (int i = 0; i < stmt->value->count; i++) {
                        AstNode *f = stmt->value->fields[i];
                        if (!first && f->value) first = f;
                        f->value = nullptr;
                    }
                    if (first) {
                        current_line = first->line; current_col = first->col;
                        print_err("flags_value", stmt->len, stmt->name);
                    }
                }
                pt_add_node(stmt->name, stmt->len, owner_try ? owner_try : stmt, stmt->value);
                if (stmt->value && stmt->value->type == STRUCT_LIT)
                    pt_add_field_nodes(stmt->name, stmt->len, stmt->value, owner_try ? owner_try : stmt);
            }
            break;

        case MUT_DECL:
            if (checking) {
                if (stmt->value && stmt->value->type == FN_LIT) {
                    pt_declare_name(stmt->name, stmt->len, true);
                    pt_check_expr(stmt->value);
                } else {
                    pt_check_expr(stmt->value);
                    pt_declare_name(stmt->name, stmt->len, true);
                }
            }
            break;

        case DESTRUCTURE_DECL:
            if (checking) pt_check_expr(stmt->value);
            for (int i = 0; i < stmt->target_count; i++) {
                AstNode *target = stmt->targets[i];
                if (!checking && stmt->is_immutable) {
                    pt_add_node(target->name, target->len, owner_try ? owner_try : stmt, stmt->value);
                }
                if (checking && stmt->type_tok == TOK_ASSIGN) {
                    if (!pt_name_resolves(target->name, target->len))
                        print_err("undefined", target->len, target->name);
                } else if (checking) {
                    pt_declare_name(target->name, target->len, !stmt->is_immutable);
                }
            }
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
            bind_implicits(pt_check_env, nullptr, true, nullptr, 0);
            for (int i = 0; i < stmt->param_count; i++) {
                AstNode *p = stmt->params[i];
                if (p->is_variadic) continue;
                if (p->value) pt_check_expr(p->value);
                if (!is_flagged(p)) pt_declare_name(p->name, p->len);
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

        case FOR_RANGE: {
            Env *saved = pt_check_env;
            if (checking) {
                pt_check_expr(stmt->start);
                pt_check_expr(stmt->end);
                pt_check_env = env_new(pt_check_env);
                Value none = make_null();
                bind_implicits(pt_check_env, nullptr, false, &none, 0);
            }
            pt_walk_stmt(stmt->body, owner_try, checking);
            if (checking) pt_check_env = saved;
            break;
        }

        case FOR_IN: {
            Env *saved = pt_check_env;
            if (checking) {
                pt_check_expr(stmt->iterable);
                pt_check_env = env_new(pt_check_env);
                if (stmt->key_name) {
                    pt_declare_name(stmt->key_name, stmt->key_len);
                } else {
                    Value none = make_null();
                    bind_implicits(pt_check_env, nullptr, false, &none, 0);
                }
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

        case SCOPE: {
            auto uit = pt_node_unit.find(stmt);
            bool unit_local = !checking && stmt->is_local && uit != pt_node_unit.end();
            for (int i = 0; i < stmt->stmt_count; i++) {
                AstNode *inner = stmt->stmts[i];
                if (unit_local) {
                    if (inner->type != IMPORT) pt_walk_stmt(inner, owner_try ? owner_try : stmt, checking);
                    continue;
                }
                if (uit != pt_node_unit.end()) pt_node_unit[inner] = uit->second;
                pt_walk_stmt(inner, owner_try, checking);
            }
            break;
        }

        case IMPORT: {
            if (!checking) {
                if (stmt->alias) {
                    pt_add_node(stmt->alias, stmt->alias_len, owner_try ? owner_try : stmt, nullptr);
                    break;
                }
                std::string path(stmt->path, (size_t)stmt->path_len);
                std::string file_path = resolve_import(path);
                if (pt_import_done.count(file_path)) break;
                pt_import_done.insert(file_path);

                ImportUnit *u = load_import_unit(path, file_path, false);
                if (!u) break;
                const char *sf = current_file, *ss = program_source;
                current_file = u->path;
                program_source = u->source;
                for (int i = 0; i < u->program->stmt_count; i++) {
                    pt_node_unit[u->program->stmts[i]] = u;
                    pt_walk_stmt(u->program->stmts[i], nullptr, false);
                }
                current_file = sf;
                program_source = ss;
                break;
            }

            if (stmt->alias) {
                pt_declare_name(stmt->alias, stmt->alias_len);
                break;
            }

            pt_declare_import(stmt);
            break;
        }

        case METHOD:
            if (checking) {
                pt_declare_name(stmt->name, stmt->len);
            } else if (!stmt->is_mut) {
                pt_add_node(stmt->name, stmt->len, owner_try ? owner_try : stmt, nullptr);
            }
            break;

        case STRUCT_DECL:
            if (checking && !find_struct_decl(stmt->name, stmt->len))
                register_struct_decl(stmt);
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
        case IMMUT_DECL: stmt->is_proctime = true; return true;
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
    pt_toposort(&order, &order_count);

    FILE *kept = open_memstream(&proctime_kept_buf, &proctime_kept_size);
    for (int i = 0; i < order_count; i++) {
        char *entry_buf = nullptr;
        size_t entry_size = 0;
        program_output = open_memstream(&entry_buf, &entry_size);
        int errors_before = error_count;
        const char *sf = current_file, *ss = program_source;
        auto uit = pt_node_unit.find(order[i]);
        Env *saved_scope = scope_env;
        if (uit != pt_node_unit.end()) {
            current_file = uit->second->path;
            program_source = uit->second->source;
            if (!uit->second->env) uit->second->env = env_new(global_env);
            scope_env = uit->second->env;
            global_env->uses = (Env**)grow_array(global_env->uses, global_env->use_count, &global_env->use_capacity, sizeof(Env*), 4);
            global_env->uses[global_env->use_count++] = scope_env;
        }
        exec_stmt(order[i]);
        for (int k = 0; uit != pt_node_unit.end() && k < global_env->use_count; k++) {
            if (global_env->uses[k] == scope_env) {
                global_env->uses[k] = global_env->uses[--global_env->use_count];
                break;
            }
        }
        scope_env = saved_scope;
        current_file = sf;
        program_source = ss;
        bool keep;
        if (order[i]->type == TRY_STMT || order[i]->type == IMPORT || order[i]->type == SCOPE) {
            keep = error_count == errors_before;
            if (keep) order[i]->is_proctime = true;
        } else {
            order[i]->is_proctime = true;
            keep = true;
            if (error_count != errors_before && order[i]->type == IMMUT_DECL && !env_find(global_env, order[i]->name, order[i]->len)) {
                env_define(global_env, order[i]->name, order[i]->len, make_null(), true);
            }
        }
        
        fclose(program_output);
        if (keep) fwrite(entry_buf, 1, entry_size, kept);
        free(entry_buf);
    }
    fclose(kept);
    program_output = nullptr;
}