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
            if (e->as.ident.len == 4 && strncmp(e->as.ident.name, "self", 4) == 0) {
                if (pt_check_method_depth == 0) print_err("self_outside_method");
                break;
            }
            ValueType vt;
            if (ident_is_type_name(e->as.ident.name, e->as.ident.len, &vt)) break;
            Value v;
            if (env_get(global_env, e->as.ident.name, e->as.ident.len, &v) && v.type == ValueType::Fn && ((ObjFn*)v.as.obj)->is_native) {
                print_err("undefined", e->as.ident.len, e->as.ident.name);
                break;
            }
            if (!pt_name_resolves(e->as.ident.name, e->as.ident.len))
                print_err("undefined", e->as.ident.len, e->as.ident.name);
            break;
        }
        case BINARY:
            pt_check_expr(e->as.binary.left);
            pt_check_expr(e->as.binary.right);
            break;
        case UNARY:
            pt_check_expr(e->as.unary.operand);
            break;
        case POSTFIX:
            pt_check_expr(e->as.unary.operand);
            break;
        case TERNARY:
            pt_check_expr(e->as.ternary.cond);
            pt_check_expr(e->as.ternary.then_expr);
            pt_check_expr(e->as.ternary.else_expr);
            break;
        case CALL: {
            bool callee_is_native = false;
            if (e->as.call.callee->type == IDENT) {
                Value v;
                callee_is_native = env_get(global_env, e->as.call.callee->as.ident.name, e->as.call.callee->as.ident.len, &v)
                                    && v.type == ValueType::Fn && ((ObjFn*)v.as.obj)->is_native;
            }
            if (!callee_is_native) pt_check_expr(e->as.call.callee);
            for (int i = 0; i < e->as.call.arg_count; i++) pt_check_expr(e->as.call.args[i]);
            break;
        }
        case INDEX:
            pt_check_expr(e->as.index.object);
            for (int i = 0; i < e->as.index.index_count; i++) pt_check_expr(e->as.index.indices[i]);
            break;
        case FIELD_ACCESS:
            pt_check_expr(e->as.field_access.object);
            break;
        case ARRAY_LIT:
            for (int i = 0; i < e->as.array_lit.count; i++) {
                AstNode *item = e->as.array_lit.items[i];
                if (item->type == ARRAY_ENTRY) {
                    pt_check_expr(item->as.array_entry.key);
                    pt_check_expr(item->as.array_entry.value);
                } else {
                    pt_check_expr(item);
                }
            }
            break;
        case STRUCT_LIT:
            for (int i = 0; i < e->as.struct_lit.count; i++) {
                AstNode *f = e->as.struct_lit.fields[i];
                if (f->as.field.value && f->as.field.value->type == FN_LIT) {
                    pt_check_method_depth++;
                    pt_check_expr(f->as.field.value);
                    pt_check_method_depth--;
                } else {
                    pt_check_expr(f->as.field.value);
                }
            }
            break;
        case FN_LIT: {
            Env *saved = pt_check_env;
            pt_check_env = env_new(pt_check_env);
            for (int i = 0; i < e->as.fn_decl.param_count; i++) {
                AstNode *p = e->as.fn_decl.params[i];
                if (p->as.field.is_variadic) {
                    pt_declare_name("argc", 4);
                    pt_declare_name("argv", 4);
                    continue;
                }
                if (p->as.field.value) pt_check_expr(p->as.field.value);
                pt_declare_name(p->as.field.name, p->as.field.len);
            }
            pt_walk_stmt(e->as.fn_decl.body, nullptr, true);
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
                pt_check_expr(stmt->as.decl.value);
                pt_declare_name(stmt->as.decl.name, stmt->as.decl.len);
            } else {
                pt_add_node(stmt->as.decl.name, stmt->as.decl.len, owner_try ? owner_try : stmt, stmt->as.decl.value);
            }
            break;

        case MUT_DECL:
            if (checking) {
                pt_check_expr(stmt->as.decl.value);
                pt_declare_name(stmt->as.decl.name, stmt->as.decl.len, true);
            }
            break;

        case DESTRUCTURE_DECL:
            if (checking) pt_check_expr(stmt->as.destructure_decl.value);
            for (int i = 0; i < stmt->as.destructure_decl.target_count; i++) {
                AstNode *target = stmt->as.destructure_decl.targets[i];
                if (!checking && stmt->as.destructure_decl.is_immutable) {
                    pt_add_node(target->as.field.name, target->as.field.len,
                                owner_try ? owner_try : stmt, stmt->as.destructure_decl.value);
                }
                if (checking) pt_declare_name(target->as.field.name, target->as.field.len, !stmt->as.destructure_decl.is_immutable);
            }
            break;

        case ENUM_DECL:
            if (!checking)
                pt_add_enum_node(stmt->as.enum_decl.name, stmt->as.enum_decl.len, stmt, owner_try ? owner_try : stmt);
            break;
            
        case ASSIGN:
            if (checking) {
                if (stmt->as.assign.target->type != IDENT && stmt->as.assign.target->type != FIELD_ACCESS && stmt->as.assign.target->type != INDEX && stmt->as.assign.target->type != POSTFIX) {
                    print_err("ident_assignment");
                } else {
                    pt_check_expr(stmt->as.assign.target);
                }
                int saved_depth = pt_check_method_depth;
                if (stmt->as.assign.is_method) pt_check_method_depth++;
                pt_check_expr(stmt->as.assign.value);
                pt_check_method_depth = saved_depth;
            }
            break;

        case EXPR_STMT:
            if (checking) pt_check_expr(stmt->as.expr_stmt.expr);
            break;

        case RETURN_STMT: if (checking) pt_check_expr(stmt->as.return_stmt.value); break;
        case DEFER_STMT:  if (checking) pt_check_expr(stmt->as.defer_stmt.value); break;

        case FN_DECL: {
            if (!checking) {
                pt_add_node(stmt->as.fn_decl.name, stmt->as.fn_decl.len, owner_try ? owner_try : stmt, nullptr);
                break;
            }
            pt_declare_name(stmt->as.fn_decl.name, stmt->as.fn_decl.len);
            Env *saved = pt_check_env;
            pt_check_env = env_new(pt_check_env);
            for (int i = 0; i < stmt->as.fn_decl.param_count; i++) {
                AstNode *p = stmt->as.fn_decl.params[i];
                if (p->as.field.is_variadic) {
                    pt_declare_name("argc", 4);
                    pt_declare_name("argv", 4);
                    continue;
                }
                if (p->as.field.value) pt_check_expr(p->as.field.value);
                pt_declare_name(p->as.field.name, p->as.field.len);
            }
            pt_walk_stmt(stmt->as.fn_decl.body, owner_try, checking);
            pt_check_env = saved;
            break;
        }

        case BLOCK:
            for (int i = 0; i < stmt->as.block.stmt_count; i++)
                pt_walk_stmt(stmt->as.block.stmts[i], owner_try, checking);
            break;

        case IF_STMT:
            if (checking) pt_check_expr(stmt->as.if_stmt.cond);
            pt_walk_stmt(stmt->as.if_stmt.then_branch, owner_try, checking);
            pt_walk_stmt(stmt->as.if_stmt.else_branch, owner_try, checking);
            break;

        case WHILE_STMT:
            if (checking) pt_check_expr(stmt->as.while_stmt.cond);
            pt_walk_stmt(stmt->as.while_stmt.body, owner_try, checking);
            break;

        case FOR_CSTYLE: {
            Env *saved = pt_check_env;
            if (checking) {
                pt_check_env = env_new(pt_check_env);
                pt_check_expr(stmt->as.for_cstyle.init->as.decl.value);
                pt_declare_name(stmt->as.for_cstyle.init->as.decl.name, stmt->as.for_cstyle.init->as.decl.len);
                pt_check_expr(stmt->as.for_cstyle.cond);
                pt_check_expr(stmt->as.for_cstyle.incr);
            }
            pt_walk_stmt(stmt->as.for_cstyle.body, owner_try, checking);
            if (checking) pt_check_env = saved;
            break;
        }

        case FOR_RANGE:
            if (checking) {
                pt_check_expr(stmt->as.for_range.start);
                pt_check_expr(stmt->as.for_range.end);
            }
            pt_walk_stmt(stmt->as.for_range.body, owner_try, checking);
            break;

        case FOR_IN: {
            Env *saved = pt_check_env;
            if (checking) {
                pt_check_expr(stmt->as.for_in.iterable);
                pt_check_env = env_new(pt_check_env);
                pt_declare_name(stmt->as.for_in.key_name, stmt->as.for_in.key_len);
                if (stmt->as.for_in.val_name) pt_declare_name(stmt->as.for_in.val_name, stmt->as.for_in.val_len);
            }
            pt_walk_stmt(stmt->as.for_in.body, owner_try, checking);
            if (checking) pt_check_env = saved;
            break;
        }

        case SWITCH_STMT:
            if (checking) pt_check_expr(stmt->as.switch_stmt.scrutinee);
            if (checking && stmt->as.switch_stmt.guard) pt_check_expr(stmt->as.switch_stmt.guard);
            for (int i = 0; i < stmt->as.switch_stmt.case_count; i++) {
                AstNode *c = stmt->as.switch_stmt.cases[i];
                if (checking && c->as.switch_case.label->type != DEFAULT_STMT) pt_check_expr(c->as.switch_case.label);
                pt_walk_stmt(c->as.switch_case.body, owner_try, checking);
            }
            break;

        case TRY_STMT: {
            pt_walk_stmt(stmt->as.try_stmt.try_block, stmt, checking);
            Env *saved = pt_check_env;
            if (checking) {
                pt_check_env = env_new(pt_check_env);
                pt_declare_name(stmt->as.try_stmt.catch_name, stmt->as.try_stmt.catch_len);
            }
            pt_walk_stmt(stmt->as.try_stmt.catch_block, stmt, checking);
            if (checking) pt_check_env = saved;
            break;
        }

        case TYPE_BLOCK:
            for (int i = 0; i < stmt->as.type_block.decl_count; i++)
                pt_walk_stmt(stmt->as.type_block.decls[i], owner_try, checking);
            break;

        case IMPORT: {
            if (!checking) break;
            std::string path(stmt->as.import.path, (size_t)stmt->as.import.path_len);
            size_t colon = path.find(':');
            bool has_alias = stmt->as.import.alias != nullptr;

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

                for (int i = 0; i < import_program->as.program.stmt_count; i++) {
                    AstNode *inner = import_program->as.program.stmts[i];
                    const char *name = nullptr; int len = 0;
                    switch (inner->type) {
                        case IMMUT_DECL: case MUT_DECL: name = inner->as.decl.name; len = inner->as.decl.len; break;
                        case FN_DECL:     name = inner->as.fn_decl.name; len = inner->as.fn_decl.len; break;
                        case STRUCT_DECL: name = inner->as.struct_decl.name; len = inner->as.struct_decl.len; break;
                        case ENUM_DECL:   name = inner->as.enum_decl.name; len = inner->as.enum_decl.len; break;
                        default: break;
                    }
                    if (name) pt_declare_name(name, len);
                }
                break;
            }

            const char *alias; int alias_len;
            if (has_alias) {
                alias = stmt->as.import.alias; alias_len = stmt->as.import.alias_len;
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
            if (checking) pt_declare_name(stmt->as.method.name, stmt->as.method.len);
            break;

        case STRUCT_DECL:
            if (checking && !find_struct_decl(stmt->as.struct_decl.name, stmt->as.struct_decl.len))
                register_struct_decl(stmt);
            break;

        case PROCTIME_BLOCK:
            break;

        default:
            break;
    }
}

static void pt_collect_decls(AstNode *program) {
    for (int i = 0; i < program->as.program.stmt_count; i++)
        pt_walk_stmt(program->as.program.stmts[i], nullptr, false);
}

static void pt_check_structure(AstNode *program) {
    pt_check_env = env_new(global_env);
    pt_bleed_env = env_new(global_env);
    pt_check_method_depth = 0;

    for (int i = 0; i < program->as.program.stmt_count; i++) {
        AstNode *stmt = program->as.program.stmts[i];
        if (stmt->is_proctime) continue;
        pt_walk_stmt(stmt, nullptr, true);
    }
}

static bool pt_mark_owned(AstNode *stmt) {
    if (!stmt) return true;
    switch (stmt->type) {
        case IMMUT_DECL: case ENUM_DECL: stmt->is_proctime = true; return true;
        case DESTRUCTURE_DECL:
            if (stmt->as.destructure_decl.is_immutable) { stmt->is_proctime = true; return true; }
            return false;
        case BLOCK: {
            bool all = true;
            for (int i = 0; i < stmt->as.block.stmt_count; i++)
                if (!pt_mark_owned(stmt->as.block.stmts[i])) all = false;
            return all;
        }
        case IF_STMT: {
            bool a = pt_mark_owned(stmt->as.if_stmt.then_branch);
            bool b = pt_mark_owned(stmt->as.if_stmt.else_branch);
            return a && b;
        }
        case WHILE_STMT:   return pt_mark_owned(stmt->as.while_stmt.body);
        case FOR_CSTYLE:   return pt_mark_owned(stmt->as.for_cstyle.body);
        case FOR_RANGE:    return pt_mark_owned(stmt->as.for_range.body);
        case FOR_IN:       return pt_mark_owned(stmt->as.for_in.body);
        case SWITCH_STMT: {
            bool all = true;
            for (int i = 0; i < stmt->as.switch_stmt.case_count; i++)
                if (!pt_mark_owned(stmt->as.switch_stmt.cases[i]->as.switch_case.body)) all = false;
            return all;
        }
        case TRY_STMT: {
            bool try_fully_resolved = pt_mark_owned(stmt->as.try_stmt.try_block);
            pt_mark_owned(stmt->as.try_stmt.catch_block);
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