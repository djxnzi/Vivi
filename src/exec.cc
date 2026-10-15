static AstNode* exec_stmt(AstNode *node) {
    current_line = node->line;
    current_col = node->col;

    switch (node->type) {
        case IMPORT: {
            std::string path(node->as.import.path, (size_t)node->as.import.path_len);

            std::string file_path;
            std::string default_alias;
            size_t colon = path.find(':');
            if (colon != std::string::npos) {
                std::string ns = path.substr(0, colon);
                std::string mod = path.substr(colon + 1);
                file_path = "./lib/" + ns + "/" + mod + ".vivi";
                default_alias = mod;
            } else {
                fs::path rel = fs::path(current_file).parent_path() / path;
                file_path = fs::exists(rel) ? rel.string() : path;
            }

            bool has_alias = node->as.import.alias != nullptr;
            bool is_flat_alias = has_alias && node->as.import.alias_len == 1 && node->as.import.alias[0] == '_';
            bool should_namespace = !is_flat_alias && (has_alias || !default_alias.empty());

            if (should_namespace) {
                const char *check_name = has_alias ? node->as.import.alias : default_alias.c_str();
                int check_len = has_alias ? node->as.import.alias_len : (int)default_alias.length();
                EnvEntry *existing = env_find_local(global_env, check_name, check_len);
                if (existing && existing->is_const) {
                    return nullptr;
                }
            }

            std::string source;
            if (!read_file(file_path.c_str(), source)) {
                print_err("cant_find_import", path.c_str());
                return nullptr;
            }

            char *heap_source = (char*)arena_alloc((int)source.length() + 1);
            memcpy(heap_source, source.c_str(), source.length() + 1);

            char *heap_path = (char*)arena_alloc((int)file_path.length() + 1);
            memcpy(heap_path, file_path.c_str(), file_path.length() + 1);

            const char *saved_file = current_file;
            current_file = heap_path;

            Lexer import_lx = { heap_source, heap_source, 1, 1 };
            Parser import_p = {};
            import_p.lx = &import_lx;
            import_p.had_error = false;
            import_p.lookahead_count = 0;
            parser_advance(&import_p);
            AstNode *import_program = parse_program(&import_p);
            if (import_p.had_error) {
                print_err("import_parse_err", path.c_str());
                return nullptr;
            }

            const char *saved_source = program_source;
            program_source = heap_source;

            if (!should_namespace) {
                for (int i = 0; i < import_program->as.program.stmt_count; i++)
                    exec_stmt(import_program->as.program.stmts[i]);
                current_file = saved_file;
                program_source = saved_source;
                return nullptr;
            }

            Env *saved = global_env;
            Env *module_env = env_new(nullptr);
            global_env = module_env;
            for (int i = 0; i < import_program->as.program.stmt_count; i++)
                exec_stmt(import_program->as.program.stmts[i]);
            for (int i = module_env->defer_count - 1; i >= 0; i--)
                eval_expr(module_env->defers[i]);
            global_env = saved;

            current_file = saved_file;
            program_source = saved_source;

            ObjStruct *ns = (ObjStruct*)arena_alloc(sizeof(ObjStruct));
            ns->obj.type = ValueType::Struct;
            ns->obj.refcount = 0;
            ns->type_name = nullptr;
            ns->type_name_len = 0;
            ns->fields = module_env->count ? (StructField*)arena_alloc(sizeof(StructField) * module_env->count) : nullptr;
            ns->count = ns->capacity = module_env->count;
            for (int i = 0; i < module_env->count; i++) {
                ns->fields[i].name = module_env->entries[i].name;
                ns->fields[i].len = module_env->entries[i].len;
                ns->fields[i].value = module_env->entries[i].value;
            }
            Value ns_val; ns_val.type = ValueType::Struct; ns_val.as.obj = (Obj*)ns;

            const char *final_alias;
            int final_alias_len;
            if (has_alias) {
                final_alias = node->as.import.alias;
                final_alias_len = node->as.import.alias_len;
            } else {
                char *heap = (char*)arena_alloc((int)default_alias.length());
                memcpy(heap, default_alias.data(), default_alias.length());
                final_alias = heap;
                final_alias_len = (int)default_alias.length();
            }

            env_define(global_env, final_alias, final_alias_len, ns_val, true);
            return nullptr;
        }

        case EXPR_STMT: eval_expr(node->as.expr_stmt.expr); return nullptr;

        case DESTRUCTURE_DECL: {
            if (node->is_proctime) return nullptr;
            Value rhs = eval_expr(node->as.destructure_decl.value);
            int target_count = node->as.destructure_decl.target_count;
            bool is_const = node->as.destructure_decl.is_immutable;
            bool check_redeclare = !node->is_static && (loop_stack_count <= global_env->loop_floor);

            if (rhs.type != ValueType::Array && rhs.type != ValueType::Struct) {
                print_err("cant_destructure");
                return nullptr;
            }

            ObjArray *a = rhs.type == ValueType::Array ? (ObjArray*)rhs.as.obj : nullptr;
            ObjStruct *s = rhs.type == ValueType::Struct ? (ObjStruct*)rhs.as.obj : nullptr;
            int value_count = a ? a->count : s->count;

            if (value_count != target_count) {
                print_err("destructure_val_mismatch", target_count, value_count);
                return nullptr;
            }

            for (int i = 0; i < target_count; i++) {
                AstNode *target = node->as.destructure_decl.targets[i];
                const char *name = target->as.field.name;
                int len = target->as.field.len;

                if (node->is_static && target->static_slot) {
                    ObjVar *box = (ObjVar*)target->static_slot;
                    env_define(global_env, name, len, box->value, is_const, is_const, false, target->static_slot);
                    env_set_type_annotation(global_env, name, len, target->as.field.type_expr);
                    continue;
                }

                if (check_redeclare && env_find_local(global_env, name, len)) {
                    print_err(is_const ? "immut_redecl" : "mut_redecl", len, name);
                    return nullptr;
                }

                Value v = a ? a->items[i] : s->fields[i].value;
                AstNode *type_annotation = target->as.field.type_expr;
                if (type_annotation) {
                    Value coerced;
                    if (!value_matches_type(type_annotation, v, &coerced)) {
                        print_err("type_mismatch", len, name);
                        return nullptr;
                    }
                    v = coerced;
                }

                void *box = nullptr;
                if (node->is_static) {
                    ObjVar *ov = (ObjVar*)arena_alloc(sizeof(ObjVar));
                    ov->obj.type = v.type;
                    ov->obj.refcount = 0;
                    ov->value = v;
                    target->static_slot = ov;
                    box = target->static_slot;
                }

                env_define(global_env, name, len, v, is_const, is_const, node->is_local, box);
                env_set_type_annotation(global_env, name, len, type_annotation);
            }
            return nullptr;
        }

        case IMMUT_DECL:
        case MUT_DECL: {
            if (node->is_proctime) return nullptr;
            bool is_const = (node->type == IMMUT_DECL);

            if (node->is_static && node->static_slot) {
                ObjVar *box = (ObjVar*)node->static_slot;
                env_define(global_env, node->as.decl.name, node->as.decl.len, box->value, is_const, is_const, false, node->static_slot);
                env_set_type_annotation(global_env, node->as.decl.name, node->as.decl.len,
                    node->as.decl.has_type_annotation ? node->as.decl.type_annotation : nullptr);
                return nullptr;
            }

            if (!node->is_static && loop_stack_count <= global_env->loop_floor) {
                EnvEntry *existing = env_find_local(global_env, node->as.decl.name, node->as.decl.len);
                if (existing) {
                    if (existing->value.type == ValueType::Fn && ((ObjFn*)existing->value.as.obj)->is_native) {
                        print_err("reserved", node->as.decl.len, node->as.decl.name);
                    } else {
                        print_err(is_const ? "immut_redecl" : "mut_redecl", node->as.decl.len, node->as.decl.name);
                    }
                    return nullptr;
                }
            }

            AstNode *type_annotation = node->as.decl.has_type_annotation ? node->as.decl.type_annotation : nullptr;
            Value v;
            if (!node->as.decl.value) {
                v = make_null();
            } else if (is_ext_annotation(type_annotation)) {
                v = make_ext_asset(node->as.decl.value);
            } else {
                v = eval_expr(node->as.decl.value);
            }

            if (type_annotation && !is_ext_annotation(type_annotation) && !node->as.decl.type_size) {
                Value coerced;
                if (!value_matches_type(type_annotation, v, &coerced)) {
                    print_err("type_mismatch", node->as.decl.len, node->as.decl.name);
                    return nullptr;
                }
                v = coerced;
            }

            if (node->as.decl.type_size) {
                Value sz = eval_expr(node->as.decl.type_size);
                if (sz.type != ValueType::Int || sz.as.i < 0) {
                    print_err("bad_array_size", node->as.decl.len, node->as.decl.name);
                    return nullptr;
                }
                int n = (int)sz.as.i;
                ObjArray *a = (ObjArray*)arena_alloc(sizeof(ObjArray));
                a->obj.type = ValueType::Array;
                a->obj.refcount = 0;
                a->keys  = n ? (Value*)arena_alloc(sizeof(Value) * n) : nullptr;
                a->items = n ? (Value*)arena_alloc(sizeof(Value) * n) : nullptr;
                a->count = a->capacity = n;
                for (int i = 0; i < n; i++) {
                    a->keys[i]  = make_null();
                    a->items[i] = value_copy(v);
                }
                v.type = ValueType::Array;
                v.as.obj = (Obj*)a;
            }

            void *box = nullptr;
            if (node->is_static) {
                ObjVar *ov = (ObjVar*)arena_alloc(sizeof(ObjVar));
                ov->obj.type = v.type;
                ov->obj.refcount = 0;
                ov->value = v;
                node->static_slot = ov;
                box = node->static_slot;
            }

            env_define(global_env, node->as.decl.name, node->as.decl.len, v, is_const, is_const, node->is_local, box);
            env_set_type_annotation(global_env, node->as.decl.name, node->as.decl.len, type_annotation);
            return nullptr;
        }
        
        case ASSIGN: {
            if (node->as.assign.target->type == FIELD_ACCESS) {
                AstNode *fa = node->as.assign.target;
                Value obj = auto_deref(fa->as.field_access.object, eval_expr(fa->as.field_access.object));
                if (obj.type != ValueType::Struct) {
                    print_err("field_assignment");
                    return nullptr;
                }
                ObjStruct *s = (ObjStruct*)obj.as.obj;
                const char *fname = fa->as.field_access.field;
                int flen = fa->as.field_access.len;
                TokenType op = node->as.assign.op;
                Value rhs = eval_expr(node->as.assign.value);

                for (int i = 0; i < s->count; i++) {
                    if (s->fields[i].len == flen && strncmp(s->fields[i].name, fname, flen) == 0) {
                        Value result = rhs;
                        if (op != TOK_ASSIGN) {
                            TokenType math_op = op == TOK_PLUSEQ    ? TOK_PLUS
                                            : op == TOK_MINUSEQ     ? TOK_MINUS
                                            : op == TOK_STAREQ      ? TOK_STAR
                                            : op == TOK_MODEQ       ? TOK_PERCENT
                                            :                       TOK_SLASH;
                            result = eval_math(math_op, s->fields[i].value, rhs);
                        }
                        s->fields[i].value = value_copy(result);
                        return nullptr;
                    }
                }
                print_err("no_field", flen, fname);
                return nullptr;
            }

            if (node->as.assign.target->type == INDEX) {
                AstNode *ix = node->as.assign.target;
                Value cur = eval_expr(ix->as.index.object);
                Value *slot = nullptr;
                bool fresh = false;
                int last = ix->as.index.index_count - 1;
                for (int d = 0; d <= last; d++) {
                    if (cur.type != ValueType::Array) { print_err("arr_index"); return nullptr; }
                    ObjArray *a = (ObjArray*)cur.as.obj;
                    Value idx = eval_expr(ix->as.index.indices[d]);
                    int pos = -1;
                    if (idx.type == ValueType::Int) {
                        pos = array_find_index(a, idx.as.i);
                    } else if (idx.type == ValueType::Str || idx.type == ValueType::Rune) {
                        for (int i = 0; i < a->count && pos < 0; i++) {
                            if (values_equal(a->keys[i], idx)) {
                                pos = i;
                            }
                        }
                    } else {
                        print_err("int_or_str");
                        return nullptr;
                    }
                    if (pos < 0 && d == last) {
                        array_needs_capacity(a, a->count + 1);
                        a->keys[a->count] = value_copy(idx);
                        a->items[a->count] = make_null();
                        pos = a->count++;
                        fresh = true;
                    }
                    if (pos < 0) { print_err("index_not_found"); return nullptr; }
                    slot = &a->items[pos];
                    cur = a->items[pos];
                }

                TokenType op = node->as.assign.op;
                Value rhs = eval_expr(node->as.assign.value), result = rhs;
                if (op != TOK_ASSIGN && !fresh) {
                    TokenType m = op == TOK_PLUSEQ ? TOK_PLUS : op == TOK_MINUSEQ ? TOK_MINUS
                                : op == TOK_STAREQ ? TOK_STAR : op == TOK_MODEQ ? TOK_PERCENT : TOK_SLASH;
                    result = eval_math(m, *slot, rhs);
                }
                *slot = value_copy(result);
                return nullptr;
            }

            if (node->as.assign.target->type == IDENT && is_ident(node->as.assign.target, "self")) {
                for (Env *e = global_env; e; e = e->parent) {
                    if (e->has_self) {
                        TokenType op = node->as.assign.op;

                        Value rhs = eval_expr(node->as.assign.value);
                        Value result = rhs;
                        if (op != TOK_ASSIGN) {
                            TokenType math_op = op == TOK_PLUSEQ    ? TOK_PLUS
                                            : op == TOK_MINUSEQ     ? TOK_MINUS
                                            : op == TOK_STAREQ      ? TOK_STAR
                                            : op == TOK_MODEQ       ? TOK_PERCENT
                                            :                          TOK_SLASH;
                            result = eval_math(math_op, e->self_binding, rhs);
                        }
                        e->self_binding = value_copy(result);
                        return nullptr;
                    }
                }
                print_err("self_outside_method");
                return nullptr;
            }

            if (node->as.assign.target->type == POSTFIX && node->as.assign.target->as.unary.op == TOK_STAR) {
                Value p = eval_expr(node->as.assign.target->as.unary.operand);
                if (p.type != ValueType::Int) { print_err("deref_type"); return nullptr; }
                Value *slot = (Value*)p.as.i;
                TokenType op = node->as.assign.op;
                Value rhs = eval_expr(node->as.assign.value), result = rhs;
                if (op != TOK_ASSIGN) {
                    TokenType m = op == TOK_PLUSEQ ? TOK_PLUS : op == TOK_MINUSEQ ? TOK_MINUS
                                : op == TOK_STAREQ ? TOK_STAR : op == TOK_MODEQ ? TOK_PERCENT : TOK_SLASH;
                    result = eval_math(m, *slot, rhs);
                }
                *slot = value_copy(result);
                return nullptr;
            }

            if (node->as.assign.target->type != IDENT) {
                print_err("ident_assignment");
                return nullptr;
            }
            const char *name = node->as.assign.target->as.ident.name;
            int len = node->as.assign.target->as.ident.len;
            TokenType op = node->as.assign.op;

            if (node->as.assign.is_method) {
                register_method(name, len);
            }
            if (!node->as.assign.is_method && find_method(name, len)) {
                unregister_method(name, len);
            }

            Value rhs = eval_expr(node->as.assign.value);
            Value result = rhs;

            if (op != TOK_ASSIGN) {
                Value old;
                if (!env_get(global_env, name, len, &old)) {
                    print_err("undefined", len, name);
                    return nullptr;
                }
                TokenType math_op = op == TOK_PLUSEQ    ? TOK_PLUS
                                : op == TOK_MINUSEQ     ? TOK_MINUS
                                : op == TOK_STAREQ      ? TOK_STAR
                                : op == TOK_MODEQ       ? TOK_PERCENT
                                :                       TOK_SLASH;
                result = eval_math(math_op, old, rhs);
            }

            EnvEntry *entry = env_find(global_env, name, len);
            if (entry && entry->type_annotation) {
                Value coerced;
                if (!value_matches_type(entry->type_annotation, result, &coerced)) {
                    print_err("type_mismatch", len, name);
                    return nullptr;
                }
                result = coerced;
            }

            env_assign(global_env, name, len, result);
            return nullptr;
        }

        case BLOCK: {
            Env *env = global_env;
            int count_before = env->count;
            AstNode *result = nullptr;

            for (int i = 0; i < node->as.block.stmt_count; i++) {
                int before = error_count;
                AstNode *signal = exec_stmt(node->as.block.stmts[i]);
                if (signal) { result = signal; break; }
                if (error_count > before) break;
            }

            int write = count_before;
            for (int i = count_before; i < env->count; i++) {
                if (!env->entries[i].is_local) env->entries[write++] = env->entries[i];
            }
            env->count = write;

            return result;
        }

        case TYPE_BLOCK: {
            for (int i = 0; i < node->as.type_block.decl_count; i++) {
                AstNode *signal = exec_stmt(node->as.type_block.decls[i]);
                if (signal) return signal;
            }
            return nullptr;
        }

        case IF_STMT: {
            if (is_truthy(eval_expr(node->as.if_stmt.cond)))
                return exec_stmt(node->as.if_stmt.then_branch);
            if (node->as.if_stmt.else_branch)
                return exec_stmt(node->as.if_stmt.else_branch);
            return nullptr;
        }

        case WHILE_STMT: {
            LoopFrame frame;
            frame.var_count = 0;
            loop_stack_push(frame);

            while (is_truthy(eval_expr(node->as.while_stmt.cond))) {
                AstNode *signal = exec_stmt(node->as.while_stmt.body);
                if (signal) {
                    if (signal->type == BREAK_STMT) break;
                    if (signal->type == CONTINUE_STMT) continue;
                    loop_stack_count--;
                    return signal;
                }
            }
            loop_stack_count--;
            return nullptr;
        }

        case FOR_CSTYLE: {
            LoopFrame frame;
            frame.var_count = 1;
            frame.vars[0].name = node->as.for_cstyle.init->as.decl.name;
            frame.vars[0].len  = node->as.for_cstyle.init->as.decl.len;
            loop_stack_push(frame);

            exec_stmt(node->as.for_cstyle.init);

            while (is_truthy(eval_expr(node->as.for_cstyle.cond))) {
                AstNode *signal = exec_stmt(node->as.for_cstyle.body);
                if (signal) {
                    if (signal->type == BREAK_STMT) break;
                    if (signal->type == CONTINUE_STMT) {
                        eval_expr(node->as.for_cstyle.incr);
                        continue;
                    }
                    loop_stack_count--;
                    return signal;
                }
                eval_expr(node->as.for_cstyle.incr);
            }
            loop_stack_count--;
            return nullptr;
        }

        case FOR_RANGE: {
            Value start_v = eval_expr(node->as.for_range.start);
            Value end_v = eval_expr(node->as.for_range.end);
            if (start_v.type != ValueType::Int || end_v.type != ValueType::Int) {
                print_err("for_range");
                return nullptr;
            }
            for (long i = start_v.as.i; i <= end_v.as.i; i++) {
                AstNode *signal = exec_stmt(node->as.for_range.body);
                if (signal) {
                    if (signal->type == BREAK_STMT) break;
                    if (signal->type == CONTINUE_STMT) continue;
                    return signal;
                }
            }
            return nullptr;
        }

        case FOR_IN: {
            bool single = (node->as.for_in.val_name == nullptr);

            LoopFrame frame;
            frame.var_count = single ? 1 : 2;
            frame.vars[0].name = node->as.for_in.key_name;
            frame.vars[0].len  = node->as.for_in.key_len;
            if (!single) {
                frame.vars[1].name = node->as.for_in.val_name;
                frame.vars[1].len  = node->as.for_in.val_len;
            }
            loop_stack_push(frame);

            if (single && node->as.for_in.iterable->type == BINARY &&
                node->as.for_in.iterable->as.binary.op == TOK_SLICE) {
                AstNode *range = node->as.for_in.iterable;
                Value start_v = eval_expr(range->as.binary.left);
                Value end_v   = eval_expr(range->as.binary.right);
                if (start_v.type != ValueType::Int || end_v.type != ValueType::Int) {
                    print_err("for_range");
                    loop_stack_count--;
                    return nullptr;
                }
                for (long i = start_v.as.i; i <= end_v.as.i; i++) {
                    env_define(global_env, node->as.for_in.key_name, node->as.for_in.key_len, make_int(i), false);
                    AstNode *signal = exec_stmt(node->as.for_in.body);
                    if (signal) {
                        if (signal->type == BREAK_STMT) break;
                        if (signal->type == CONTINUE_STMT) continue;
                        loop_stack_count--;
                        return signal;
                    }
                }
                loop_stack_count--;
                return nullptr;
            }

            Value iterable = eval_expr(node->as.for_in.iterable);

            if (iterable.type == ValueType::Struct) {
                ObjStruct *s = (ObjStruct*)iterable.as.obj;
                for (int i = 0; i < s->count; i++) {
                    if (single) {
                        env_define(global_env, node->as.for_in.key_name, node->as.for_in.key_len, s->fields[i].value, false);
                    } else {
                        Value key_val = make_str(s->fields[i].name, s->fields[i].len);
                        env_define(global_env, node->as.for_in.key_name, node->as.for_in.key_len, key_val, false);
                        env_define(global_env, node->as.for_in.val_name, node->as.for_in.val_len, s->fields[i].value, false);
                    }
                    AstNode *signal = exec_stmt(node->as.for_in.body);
                    if (signal) {
                        if (signal->type == BREAK_STMT) break;
                        if (signal->type == CONTINUE_STMT) continue;
                        loop_stack_count--;
                        return signal;
                    }
                }
                loop_stack_count--;
                return nullptr;
            }

            if (iterable.type != ValueType::Array) {
                print_err("for_in");
                loop_stack_count--;
                return nullptr;
            }
            ObjArray *a = (ObjArray*)iterable.as.obj;
            long seen = 0;
            for (int i = 0; i < a->count; i++) {
                if (single) {
                    env_define(global_env, node->as.for_in.key_name, node->as.for_in.key_len, a->items[i], false);
                } else {
                    Value key_val = (a->keys[i].type == ValueType::Str) ? a->keys[i] : make_int(seen++);
                    env_define(global_env, node->as.for_in.key_name, node->as.for_in.key_len, key_val, false);
                    env_define(global_env, node->as.for_in.val_name, node->as.for_in.val_len, a->items[i], false);
                }
                AstNode *signal = exec_stmt(node->as.for_in.body);
                if (signal) {
                    if (signal->type == BREAK_STMT) break;
                    if (signal->type == CONTINUE_STMT) continue;
                    loop_stack_count--;
                    return signal;
                }
            }
            loop_stack_count--;
            return nullptr;
        }

        case SWITCH_STMT: {
            Value scrutinee = eval_expr(node->as.switch_stmt.scrutinee);
            bool guard_ok = true;

            if (node->as.switch_stmt.guard) {
                Value g = eval_expr(node->as.switch_stmt.guard);
                if (g.type != ValueType::Array) {
                    print_err("rhs_in");
                    return nullptr;
                }
                ObjArray *a = (ObjArray*)g.as.obj;
                guard_ok = false;
                for (int i = 0; i < a->count; i++) {
                    if (values_equal(a->items[i], scrutinee)) { guard_ok = true; break; }
                }
            }

            int case_count = node->as.switch_stmt.case_count;
            int default_index = -1;
            int start_index = -1;

            for (int i = 0; i < case_count; i++) {
                AstNode *c = node->as.switch_stmt.cases[i];
                if (c->as.switch_case.label->type == DEFAULT_STMT) {
                    default_index = i;
                    continue;
                }
                if (guard_ok && label_matches(c->as.switch_case.label, scrutinee)) {
                    start_index = i;
                    break;
                }
            }
            if (start_index == -1) start_index = default_index;
            if (start_index == -1) return nullptr;

            for (int i = start_index; i < case_count; i++) {
                AstNode *c = node->as.switch_stmt.cases[i];
                AstNode *signal = exec_stmt(c->as.switch_case.body);
                if (signal && signal->type == FALLTHROUGH_STMT) continue;
                if (signal && signal->type == BREAK_STMT) return nullptr;
                return signal;
            }
            return nullptr;
        }

        case FN_DECL: {
            if (node->is_proctime) return nullptr;
            ObjFn *fn = (ObjFn*)arena_alloc(sizeof(ObjFn));
            fn->obj.type = ValueType::Fn;
            fn->is_native = false;
            fn->obj.refcount = 0;
            fn->decl = node;
            fn->closure = global_env;
            fn->file = current_file;
            fn->source = program_source;
            Value v; v.type = ValueType::Fn; v.as.obj = (Obj*)fn;
            env_define(global_env, node->as.fn_decl.name, node->as.fn_decl.len, v, true);
            return nullptr;
        }

        case PRIMITIVE: {
            register_primitive(node);
            return nullptr;
        }

        case METHOD: {
            EnvEntry *existing = env_find_local(global_env, node->as.method.name, node->as.method.len);
            if (existing) {
                if (find_method(node->as.method.name, node->as.method.len)) {
                    print_err("duplicate_method", node->as.method.len, node->as.method.name);
                } else {
                    print_err(node->as.method.is_mut ? "mut_redecl" : "immut_redecl", node->as.method.len, node->as.method.name);
                }
                return nullptr;
            }
            ObjFn *fn = (ObjFn*)arena_alloc(sizeof(ObjFn));
            fn->obj.type = ValueType::Fn;
            fn->obj.refcount = 0;
            fn->is_native = false;
            fn->decl = node;
            fn->closure = global_env;
            fn->file = current_file;
            fn->source = program_source;
            Value v; v.type = ValueType::Fn; v.as.obj = (Obj*)fn;
            env_define(global_env, node->as.method.name, node->as.method.len, v, !node->as.method.is_mut);
            register_method(node->as.method.name, node->as.method.len);
            return nullptr;
        }

        case STRUCT_DECL: {
            register_struct_decl(node);
            return nullptr;
        }

        case ENUM_DECL: {
            if (node->is_proctime) return nullptr;
            register_enum_decl(node);
            return nullptr;
        }

        case TRY_STMT: {
            if (node->is_proctime) return nullptr;
            Env *saved_env = global_env;
            int saved_loop_depth = loop_stack_count;
            int errors_before = error_count;
            int env_count_before = global_env->count;
            int defer_count_before = global_env->defer_count;
            try_depth++;
            AstNode *signal = exec_stmt(node->as.try_stmt.try_block);
            try_depth--;
            if (error_count > errors_before) {
                error_count = errors_before;
                global_env = saved_env;
                global_env->count = env_count_before;
                global_env->defer_count = defer_count_before;
                loop_stack_count = saved_loop_depth;
                env_define(global_env, node->as.try_stmt.catch_name, node->as.try_stmt.catch_len, last_error_value, false);
                return exec_stmt(node->as.try_stmt.catch_block);
            }
            return signal;
        }

        case BREAK_STMT:        return node;
        case CONTINUE_STMT:     return node;
        case FALLTHROUGH_STMT:  return node;

        case RETURN_STMT: {
            AstNode *val = node->as.return_stmt.value;
            if (val && val->type == ASSIGN) {
                exec_stmt(val);
                return_value = eval_expr(val->as.assign.target);
            } else {
                return_value = val ? eval_expr(val) : make_null();
            }
            return node;
        }

        case DEFER_STMT: {
            Env *env = global_env;
            if (env->defer_count == env->defer_capacity) {
                int new_cap = env->defer_capacity < 4 ? 4 : env->defer_capacity * 2;
                AstNode **new_defers = (AstNode**)arena_alloc(sizeof(AstNode*) * new_cap);
                for (int i = 0; i < env->defer_count; i++) new_defers[i] = env->defers[i];
                env->defers = new_defers;
                env->defer_capacity = new_cap;
            }
            env->defers[env->defer_count++] = node->as.defer_stmt.value;
            return nullptr;
        }

        default:
            print_err("unhandled_node_type", (int)node->type);
            return nullptr;
    }
}

void run_program(AstNode *program, int argc, const char **argv, const char *source) {
    script_argv = argv;
    program_source = source;
    program_output = open_memstream(&program_output_buf, &program_output_size);
    in_exec = true;

    for (int i = 0; i < program->as.program.stmt_count; i++)
        exec_stmt(program->as.program.stmts[i]);

    Value main_fn;
    if (env_get(global_env, "main", 4, &main_fn) && main_fn.type == ValueType::Fn) {
        ObjArray *argv_arr = (ObjArray*)arena_alloc(sizeof(ObjArray));
        argv_arr->obj.type = ValueType::Array;
        argv_arr->obj.refcount = 0;
        argv_arr->count = argv_arr->capacity = argc;
        argv_arr->keys  = (Value*)arena_alloc(sizeof(Value) * argc);
        argv_arr->items = (Value*)arena_alloc(sizeof(Value) * argc);
        for (int i = 0; i < argc; i++) {
            int len = (int)strlen(argv[i]);
            char *copy = (char*)arena_alloc(len);
            memcpy(copy, argv[i], len);
            argv_arr->keys[i] = make_null();
            argv_arr->items[i] = make_str(copy, len);
        }
        Value argv_val; argv_val.type = ValueType::Array; argv_val.as.obj = (Obj*)argv_arr;

        Value main_args[2] = { make_int(argc), argv_val };
        call_function((ObjFn*)main_fn.as.obj, main_args, 2);
    }

    for (int i = global_env->defer_count - 1; i >= 0; i--)
        eval_expr(global_env->defers[i]);

    fclose(program_output);
    fwrite(program_output_buf, 1, program_output_size, stdout);
    if (program_output_size > 0 && program_output_buf[program_output_size - 1] != '\n') fputc('\n', stdout);
    if (error_count > 0) exit(CODE_ERROR);
}