static Value* struct_field_slot(Value obj, const char *name, int len) {
    if (obj.type != ValueType::Struct) return nullptr;
    ObjStruct *s = (ObjStruct*)obj.as.obj;
    for (int i = 0; i < s->count; i++) {
        if (s->fields[i].len == len && strncmp(s->fields[i].name, name, len) == 0) return &s->fields[i].value;
    }
    return nullptr;
}

static bool array_locate(ObjArray *a, Value idx, int *pos) {
    *pos = -1;
    if (idx.type == ValueType::Int) {
        *pos = array_find_index(a, idx.as.i);
    } else if (idx.type == ValueType::Str || idx.type == ValueType::Rune) {
        for (int i = 0; i < a->count && *pos < 0; i++) {
            if (values_equal(a->keys[i], idx)) *pos = i;
        }
    } else {
        print_err("int_or_str");
        return false;
    }
    return true;
}

static Value* resolve_place(AstNode *node, bool create, bool *fresh) {
    if (fresh) *fresh = false;

    switch (node->type) {
        case IDENT: {
            EnvEntry *e = env_find(global_env, node->name, node->len);
            if (!e) { print_err("undefined", node->len, node->name); return nullptr; }
            return &e->value;
        }

        case FIELD_ACCESS: {
            Value obj = auto_deref(node->object, eval_expr(node->object));
            if (obj.type != ValueType::Struct) { print_err("field_assignment"); return nullptr; }
            Value *slot = struct_field_slot(obj, node->field, node->len);
            if (!slot) print_err("no_field", node->len, node->field);
            return slot;
        }

        case INDEX: {
            Value cur = eval_expr(node->object);
            Value *slot = nullptr;
            for (int d = 0; d < node->index_count; d++) {
                if (cur.type != ValueType::Array) { print_err("arr_index"); return nullptr; }
                ObjArray *a = (ObjArray*)cur.as.obj;
                Value idx = eval_expr(node->indices[d]);
                int pos;
                if (!array_locate(a, idx, &pos)) return nullptr;
                if (pos < 0 && create && d == node->index_count - 1) {
                    array_needs_capacity(a, a->count + 1);
                    a->keys[a->count] = value_copy(idx);
                    a->items[a->count] = make_null();
                    pos = a->count++;
                    if (fresh) *fresh = true;
                }
                if (pos < 0) { print_err("index_not_found"); return nullptr; }
                slot = &a->items[pos];
                cur = *slot;
            }
            return slot;
        }

        default:
            print_err("unary");
            return nullptr;
    }
}

static TokenType assign_op_to_math(TokenType op) {
    switch (op) {
        case TOK_PLUSEQ:  return TOK_PLUS;
        case TOK_MINUSEQ: return TOK_MINUS;
        case TOK_STAREQ:  return TOK_STAR;
        case TOK_MODEQ:   return TOK_PERCENT;
        default:          return TOK_SLASH;
    }
}

static void assign_apply(Value *slot, TokenType op, Value rhs, bool fresh) {
    if (op != TOK_ASSIGN && !fresh) rhs = eval_math(assign_op_to_math(op), *slot, rhs);
    *slot = value_copy(rhs);
}

static Value auto_deref(AstNode *obj_node, Value obj) {
    if (obj_node->type != IDENT || obj.type != ValueType::Int) return obj;
    EnvEntry *e = env_find(global_env, obj_node->name, obj_node->len);
    if (!e || !e->type_annotation) return obj;
    if (e->type_annotation->type != UNARY || e->type_annotation->op != TOK_STAR) return obj;
    return *(Value*)obj.as.i;
}

static Value eval_expr(AstNode *node) {
    current_line = node->line;
    current_col = node->col;

    switch (node->type) {
        case INT_LIT:   return make_int(node->int_val);
        case FLOAT_LIT: return make_float(node->float_val);
        case BOOL_LIT:  return make_bool(node->bool_val);
        case NULL_LIT:  return make_null();
        case STR_LIT:   return make_str(node->text, node->len);
        case RUNE_LIT:  return make_rune(node->text, node->len);
        
        case CALL: {

            if (node->callee->type == IDENT) {
                AstNode *primitive = find_primitive(node->callee->name, node->callee->len);

                if (primitive) {
                    Value *arg_values = nullptr;

                    if (node->arg_count > 0) {
                        arg_values = (Value*)arena_alloc(sizeof(Value) * node->arg_count);

                        for (int i = 0; i < node->arg_count; i++) {
                            arg_values[i] = eval_expr(node->args[i]);
                        }
                    }

                    return call_primitive(primitive, arg_values, node->arg_count);
                }
            }

            if (node->callee->type == IDENT) {
                Value maybe_builtin;
                if (env_get(global_env, node->callee->name, node->callee->len, &maybe_builtin) &&
                    maybe_builtin.type == ValueType::Fn && ((ObjFn*)maybe_builtin.as.obj)->is_native) {
                    Value *arg_values = nullptr;
                    if (node->arg_count > 0) {
                        arg_values = (Value*)arena_alloc(sizeof(Value) * node->arg_count);
                        for (int i = 0; i < node->arg_count; i++) arg_values[i] = eval_expr(node->args[i]);
                    }
                    return call_function((ObjFn*)maybe_builtin.as.obj, arg_values, node->arg_count, nullptr, node->line, node->col);
                }
            }

            if (node->callee->type == IDENT) {
                AstNode *struct_decl = find_struct_decl(node->callee->name, node->callee->len);
                if (struct_decl) {
                    ObjStruct *s = (ObjStruct*)arena_alloc(sizeof(ObjStruct));
                    s->obj.type = ValueType::Struct;
                    s->obj.refcount = 0;
                    s->type_name = struct_decl->name;
                    s->type_name_len = struct_decl->len;
                    int field_count = struct_decl->field_count;
                    s->fields = field_count ? (StructField*)arena_alloc(sizeof(StructField) * field_count) : nullptr;
                    s->count = s->capacity = field_count;
                    for (int i = 0; i < field_count; i++) {
                        AstNode *f = struct_decl->fields[i];
                        s->fields[i].name = f->name;
                        s->fields[i].len = f->len;
                        if (i < node->arg_count) {
                            s->fields[i].value = eval_expr(node->args[i]);
                        } else {
                            s->fields[i].value = f->value ? eval_expr(f->value) : make_null();
                        }
                    }
                    Value v; v.type = ValueType::Struct; v.as.obj = (Obj*)s;
                    return v;
                }
            }

            if (node->callee->type == FIELD_ACCESS) {
                AstNode *fa = node->callee;
                Value obj = eval_expr(fa->object);

                if (obj.type == ValueType::Frame) {
                    EnvEntry *entry = env_find_local(((ObjFrame*)obj.as.obj)->frame, fa->field, fa->len);
                    if (!entry || entry->value.type != ValueType::Fn || entry->is_const) {
                        print_err("no_field", fa->len, fa->field);
                        return make_null();
                    }
                    ObjFn *fn = (ObjFn*)entry->value.as.obj;
                    Value *arg_values = nullptr;
                    if (node->arg_count > 0) {
                        arg_values = (Value*)arena_alloc(sizeof(Value) * node->arg_count);
                        for (int j = 0; j < node->arg_count; j++) arg_values[j] = eval_expr(node->args[j]);
                    }
                    return call_function(fn, arg_values, node->arg_count, nullptr, node->line, node->col);
                }

                Value *field = struct_field_slot(obj, fa->field, fa->len);
                if (field) {
                    if (field->type != ValueType::Fn) {
                        print_err("not_function", fa->len, fa->field);
                        return make_null();
                    }
                    ObjFn *fn = (ObjFn*)field->as.obj;
                    Value *arg_values = nullptr;
                    if (node->arg_count > 0) {
                        arg_values = (Value*)arena_alloc(sizeof(Value) * node->arg_count);
                        for (int j = 0; j < node->arg_count; j++) arg_values[j] = eval_expr(node->args[j]);
                    }
                    return call_function(fn, arg_values, node->arg_count, &obj, node->line, node->col);
                }

                ObjFn *method_fn = find_method(fa->field, fa->len);
                if (method_fn) {
                    Value *arg_values = nullptr;
                    if (node->arg_count > 0) {
                        arg_values = (Value*)arena_alloc(sizeof(Value) * node->arg_count);
                        for (int i = 0; i < node->arg_count; i++) arg_values[i] = eval_expr(node->args[i]);
                    }
                    return call_method(method_fn, fa->object, obj, arg_values, node->arg_count, node->line, node->col);
                }
                if (obj.type == ValueType::Struct) {
                    print_err("not_method", fa->len, fa->field);
                    return make_null();
                }
                //return call_method_native(fa->field, fa->len, obj, arg_values, node->arg_count);
            }

            Value callee = eval_expr(node->callee);
            if (callee.type == ValueType::Str && node->arg_count == 1) {
                ObjStr *s = (ObjStr*)callee.as.obj;
                Value arg = eval_expr(node->args[0]);
                return eval_typecast(s->chars, s->len, arg);
            }
            if (callee.type != ValueType::Fn) {
                print_err("not_callable");
                return make_null();
            }
            ObjFn *fn = (ObjFn*)callee.as.obj;

            Value *arg_values = nullptr;
            if (node->arg_count > 0) {
                arg_values = (Value*)arena_alloc(sizeof(Value) * node->arg_count);
                FnShape shape = get_fn_shape(fn->decl);
                for (int i = 0; i < node->arg_count; i++) {
                    AstNode *arg = node->args[i];
                    AstNode *ptype = (i < shape.param_count) ? shape.params[i]->type_expr : nullptr;
                    if (arg->type == IDENT && ptype && ptype->type == UNARY && ptype->op == TOK_STAR) {
                        Value *slot = resolve_place(arg);
                        arg_values[i] = slot ? make_int((long)slot) : make_null();
                    } else {
                        arg_values[i] = eval_expr(arg);
                    }
                }
            }

            return call_function(fn, arg_values, node->arg_count, nullptr, node->line, node->col);
        }

        case UNARY: {
            if (node->op == TOK_AMP) {
                Value *target = resolve_place(node->operand);
                if (!target) return make_null();
                return make_int((long)target);
            }
            Value v = eval_expr(node->operand);
            switch (node->op) {
                case TOK_MINUS:
                    if (v.type == ValueType::Int)   return make_int(-v.as.i);
                    if (v.type == ValueType::Float) return make_float(-v.as.f);
                    print_err("unary");
                    return make_null();
                case TOK_TILDE:
                    if (v.type == ValueType::Int) return make_int(~v.as.i);
                    print_err("bitwise_int_only");
                    return make_null();
                case TOK_NOT:
                    return make_bool(!is_truthy(v));
                default:
                    print_err("unhandled", (int)node->op);
                    return make_null();
            }
        }

        case BINARY: {
            TokenType op = node->op;

            if (op == TOK_AND) {
                if (!is_truthy(eval_expr(node->left))) return make_bool(false);
                return make_bool(is_truthy(eval_expr(node->right)));
            }
            if (op == TOK_OR) {
                if (is_truthy(eval_expr(node->left))) return make_bool(true);
                return make_bool(is_truthy(eval_expr(node->right)));
            }

            if (op == TOK_PIPE) {
                Value l = eval_expr(node->left);
                if (l.type == ValueType::Int) {
                    Value r = eval_expr(node->right);
                    if (r.type == ValueType::Int) return make_int(l.as.i | r.as.i);
                    return l;
                }
                if (l.type != ValueType::Null) return l;
                return eval_expr(node->right);
            }

            if (op == TOK_EQ || op == TOK_NEQ) {
                AstNode *left = node->left, *right = node->right;
                ValueType vt;
                AstNode *type_operand = nullptr, *value_operand = nullptr;
                bool is_custom_type = false;

                if (left->type == IDENT && ident_is_type_name(left->name, left->len, &vt)) {
                    type_operand = left; value_operand = right;
                } else if (right->type == IDENT && ident_is_type_name(right->name, right->len, &vt)) {
                    type_operand = right; value_operand = left;
                } else if (left->type == IDENT && find_struct_decl(left->name, left->len)) {
                    type_operand = left; value_operand = right; is_custom_type = true;
                } else if (right->type == IDENT && find_struct_decl(right->name, right->len)) {
                    type_operand = right; value_operand = left; is_custom_type = true;
                }

                if (type_operand) {
                    Value v = eval_expr(value_operand);
                    bool matches;
                    if (is_custom_type) {
                        matches = false;
                        if (v.type == ValueType::Struct) {
                            ObjStruct *s = (ObjStruct*)v.as.obj;
                            matches = s->type_name && s->type_name_len == type_operand->len &&
                                strncmp(s->type_name, type_operand->name, type_operand->len) == 0;
                        }
                    } else {
                        matches = (v.type == vt);
                    }
                    return make_bool(op == TOK_EQ ? matches : !matches);
                }
            }

            Value l = eval_expr(node->left);
            Value r = eval_expr(node->right);

            if (op == TOK_EQ)  return make_bool(values_equal(l, r));
            if (op == TOK_NEQ) return make_bool(!values_equal(l, r));

            if (op == TOK_TILDE) {
                Value ls = make_str_from_value(l);
                Value rs = make_str_from_value(r);
                ObjStr *a = (ObjStr*)ls.as.obj, *b = (ObjStr*)rs.as.obj;
                int total = a->len + b->len;
                char *buf = (char*)arena_alloc(total);
                memcpy(buf, a->chars, a->len);
                memcpy(buf + a->len, b->chars, b->len);
                return make_str(buf, total);
            }

            if (op == TOK_IN) {
                if (r.type != ValueType::Array) {
                    print_err("rhs_in");
                    return make_null();
                }
                ObjArray *a = (ObjArray*)r.as.obj;
                for (int i = 0; i < a->count; i++) {
                    if (values_equal(a->items[i], l)) return make_bool(true);
                }
                return make_bool(false);
            }

            Value lc, rc;
            bool lok = coerce(l, &lc);
            bool rok = coerce(r, &rc);
            if (!lok || !rok) {
                if ((l.type == ValueType::Str && !lok) || (r.type == ValueType::Str && !rok)) {
                    print_err("coerce");
                } else {
                    print_err("only_num", (int)op);
                }
                return make_null();
            }

            switch (op) {
                case TOK_LT:  return make_bool(as_double(lc) <  as_double(rc));
                case TOK_LTE: return make_bool(as_double(lc) <= as_double(rc));
                case TOK_GT:  return make_bool(as_double(lc) >  as_double(rc));
                case TOK_GTE: return make_bool(as_double(lc) >= as_double(rc));
                default: return eval_math(op, lc, rc);
            }
        }

        case POSTFIX: {
            if (node->op == TOK_STAR) {
                Value p = eval_expr(node->operand);
                if (p.type != ValueType::Int) { print_err("deref_type"); return make_null(); }
                return *(Value*)p.as.i;
            }
            AstNode *op_node = node->operand;
            TokenType math_op = (node->op == TOK_PLUSPLUS) ? TOK_PLUS : TOK_MINUS;
            Value old;

            if (op_node->type == FIELD_ACCESS || op_node->type == INDEX) {
                Value *slot = resolve_place(op_node);
                if (!slot) return make_null();
                old = *slot;
                *slot = value_copy(eval_math(math_op, old, make_int(1)));
                return old;
            }
            if (op_node->type != IDENT) {
                old = eval_expr(op_node);
                return eval_math(math_op, old, make_int(1));
            }

            const char *name = op_node->name; int len = op_node->len;
            if (!env_get(global_env, name, len, &old)) {
                print_err("undefined", len, name);
                return make_null();
            }
            env_assign(global_env, name, len, eval_math(math_op, old, make_int(1)));
            return old;
        }

        case IDENT: {
            if (is_ident(node, "self")) {
                for (Env *e = global_env; e; e = e->parent) {
                    if (e->has_self) return e->self_binding;
                }
                print_err("self_outside_method");
                return make_null();
            }
            Value v;
            if (env_get(global_env, node->name, node->len, &v) &&
                !(v.type == ValueType::Fn && ((ObjFn*)v.as.obj)->is_native)) return v;

            ValueType vt;
            if (ident_is_type_name(node->name, node->len, &vt))
                return make_str(node->name, node->len);
            print_err("undefined", node->len, node->name);
            return make_null();
        }

        case TERNARY:
            return is_truthy(eval_expr(node->cond))
                ? eval_expr(node->then_expr)
                : eval_expr(node->else_expr);

        case ARRAY_LIT: {
            ObjArray *a = (ObjArray*)arena_alloc(sizeof(ObjArray));
            a->obj.type = ValueType::Array;
            a->obj.refcount = 0;
            int n = node->count;
            a->keys  = n ? (Value*)arena_alloc(sizeof(Value) * n) : nullptr;
            a->items = n ? (Value*)arena_alloc(sizeof(Value) * n) : nullptr;
            a->count = a->capacity = n;
            for (int i = 0; i < n; i++) {
                AstNode *elem = node->items[i];
                if (elem->type == ARRAY_ENTRY) {
                    a->keys[i]  = eval_expr(elem->key);
                    a->items[i] = eval_expr(elem->value);
                } else {
                    a->keys[i]  = make_null();
                    a->items[i] = eval_expr(elem);
                }
            }
            Value v; v.type = ValueType::Array; v.as.obj = (Obj*)a;
            return v;
        }

        case INDEX: {
            Value *slot = resolve_place(node);
            return slot ? *slot : make_null();
        }

        case FN_LIT: {
            ObjFn *fn = (ObjFn*)arena_alloc(sizeof(ObjFn));
            fn->is_native = false;
            fn->obj.type = ValueType::Fn;
            fn->obj.refcount = 0;
            fn->decl = node;
            fn->closure = build_closure_env();
            fn->file = current_file;
            fn->source = program_source;
            Value v; v.type = ValueType::Fn; v.as.obj = (Obj*)fn;
            return v;
        }

        case IF_STMT: {
            if (is_truthy(eval_expr(node->cond))) {
                return eval_block_as_expr(node->then_branch);
            }
            if (node->else_branch) {
                if (node->else_branch->type == IF_STMT) {
                    return eval_expr(node->else_branch);
                }
                return eval_block_as_expr(node->else_branch);
            }
            return make_null();
        }

        case STRUCT_LIT: {
            ObjStruct *s = (ObjStruct*)arena_alloc(sizeof(ObjStruct));
            s->obj.type = ValueType::Struct;
            s->obj.refcount = 0;
            s->type_name = nullptr;
            s->type_name_len = 0;
            int n = node->count;
            s->fields = n ? (StructField*)arena_alloc(sizeof(StructField) * n) : nullptr;
            s->count = s->capacity = n;
            for (int i = 0; i < n; i++) {
                AstNode *f = node->fields[i];
                s->fields[i].name = f->name;
                s->fields[i].len = f->len;
                s->fields[i].value = eval_expr(f->value);
            }
            Value v; v.type = ValueType::Struct; v.as.obj = (Obj*)s;
            return v;
        }

        case FIELD_ACCESS: {
            if (node->object->type == IDENT) {
                Enum *enum_ = find_enum_decl(node->object->name, node->object->len);
                if (enum_) {
                    for (int i = 0; i < enum_->decl->variant_count; i++) {
                        AstNode *variant = enum_->decl->variants[i];
                        if (variant->len == node->len && strncmp(variant->name, node->field, node->len) == 0) {
                            return enum_->variant_values[i];
                        }
                    }
                    print_err("enum_variant", node->object->len, node->object->name, node->len, node->field);
                    return make_null();
                }
            }

            Value obj = auto_deref(node->object, eval_expr(node->object));
            Value *slot = struct_field_slot(obj, node->field, node->len);
            if (slot) return *slot;

            ObjFn *method_fn = find_method(node->field, node->len);
            if (method_fn) {
                return call_method(method_fn, node->object, obj, nullptr, 0, node->line, node->col);
            }
            if (obj.type == ValueType::Struct) print_err("no_field", node->len, node->field);
            else                               print_err("field_access");
            return make_null();
        }

        default:
            print_err("unhandled_node_type", (int)node->type);
            return make_null();
    }
}

static Value eval_block_as_expr(AstNode *block) {
    int n = block->stmt_count;
    for (int i = 0; i < n - 1; i++) {
        exec_stmt(block->stmts[i]);
    }
    if (n > 0) {
        AstNode *last = block->stmts[n - 1];
        if (last->type == EXPR_STMT) {
            AstNode *expr = last->expr;
            if (expr->type == CALL && expr->callee->type == FN_LIT &&
                expr->arg_count == 0) {
                return eval_expr(expr->callee);
            }
            return eval_expr(expr);
        }
        exec_stmt(last);
    }
    return make_null();
}

static Value call_function(ObjFn *fn, Value *arg_values, int arg_count, Value *self, int call_line, int call_col, Value *out_self) {
    FnShape shape = get_fn_shape(fn->decl);
    AstNode **params = shape.params;
    int param_count = shape.param_count;

    Env *call_env = env_new(fn->closure);
    call_env->loop_floor = loop_stack_count;
    call_env->caller = global_env;
    call_env->call_name = shape.name;
    call_env->call_name_len = shape.len;
    call_env->call_line = call_line;
    call_env->call_col = call_col;
    if (self) {
        call_env->has_self = true;
        call_env->self_binding = *self;
    }

    Env *saved = global_env;
    global_env = call_env;

    const char *saved_file = current_file;
    const char *saved_source = program_source;
    current_file = fn->file;
    program_source = fn->source;

    for (int i = 0; i < param_count; i++) {
        AstNode *param = params[i];

        if (param->is_variadic) {
            int tail_count = (arg_count > i) ? arg_count - i : 0;

            ObjArray *argv_arr = (ObjArray*)arena_alloc(sizeof(ObjArray));
            argv_arr->obj.type = ValueType::Array;
            argv_arr->obj.refcount = 0;
            argv_arr->count = argv_arr->capacity = tail_count;
            argv_arr->keys  = (Value*)arena_alloc(sizeof(Value) * tail_count);
            argv_arr->items = (Value*)arena_alloc(sizeof(Value) * tail_count);

            for (int j = 0; j < tail_count; j++) {
                Value pv = arg_values[i + j];
                if (param->type_expr) {
                    if (pv.type == ValueType::Null) {
                        print_err("param_null", param->len, param->name);
                        current_file = saved_file;
                        program_source = saved_source;
                        global_env = saved;
                        return make_null();
                    }
                    Value coerced;
                    if (!value_matches_type(param->type_expr, pv, &coerced)) {
                        print_err("param_type_mismatch", param->len, param->name);
                        current_file = saved_file;
                        program_source = saved_source;
                        global_env = saved;
                        return make_null();
                    }
                    pv = coerced;
                }
                argv_arr->keys[j]  = make_null();
                argv_arr->items[j] = pv;
            }

            Value argv_val; argv_val.type = ValueType::Array; argv_val.as.obj = (Obj*)argv_arr;
            env_define(call_env, "argc", 4, make_int(tail_count), false);
            env_define(call_env, "argv", 4, argv_val, false);
            break;
        }

        Value pv;
        bool not_null = (i < arg_count) && arg_values[i].type != ValueType::Null;
        if (not_null) {
            pv = arg_values[i];
        } else if (param->value) {
            pv = eval_expr(param->value);
        } else if (i < arg_count) {
            pv = arg_values[i];
        } else {
            print_err("missing_arg", param->len, param->name);
            current_file = saved_file;
            program_source = saved_source;
            global_env = saved;
            return make_null();
        }

        if (param->type_expr) {
            Value coerced;
            if (!value_matches_type(param->type_expr, pv, &coerced)) {
                print_err("param_type_mismatch", param->len, param->name);
                return make_null();
            }
            pv = coerced;
        }

        env_define(call_env, param->name, param->len, pv, false);
    }
    AstNode *signal = exec_stmt(shape.body);

    bool returned_value = signal && signal->type == RETURN_STMT && signal->value;
    Value result = (signal && signal->type == RETURN_STMT) ? return_value : make_null();

    for (int i = call_env->defer_count - 1; i >= 0; i--) {
        eval_expr(call_env->defers[i]);
    }

    if (out_self && call_env->has_self) *out_self = call_env->self_binding;

    current_file = saved_file;
    program_source = saved_source;
    global_env = saved;

    if (shape.return_type) {
        if (!returned_value) {
            if (!type_is_void(shape.return_type)) {
                print_err("no_return_value");
                return make_null();
            }
        } else if (!return_type_matches(shape.return_type, result)) {
            if (shape.return_type->type == IDENT) {
                const char *tname = shape.return_type->name;
                int tlen = shape.return_type->len;
                print_err("wrong_return_type", tlen, tname);
            } else {
                print_err("return_type_mismatch");
            }
            return make_null();
        }
    }

    bool has_priv_fn = false;
    for (int i = 0; i < call_env->count; i++) {
        if (call_env->entries[i].value.type == ValueType::Fn && !call_env->entries[i].is_const) {
            has_priv_fn = true;
            break;
        }
    }
    if (has_priv_fn && !returned_value) {
        ObjFrame *frame = (ObjFrame*)arena_alloc(sizeof(ObjFrame));
        frame->obj.type = ValueType::Frame;
        frame->obj.refcount = 0;
        frame->frame = call_env;
        frame->result = result;
        Value v; v.type = ValueType::Frame; v.as.obj = (Obj*)frame;
        result = v;
    }

    return result;
}

static Value call_method(ObjFn *fn, AstNode *receiver_expr, Value obj, Value *arg_values, int arg_count, int line, int col) {
    Value out_self = obj;
    Value result = call_function(fn, arg_values, arg_count, &obj, line, col, &out_self);
    if (receiver_expr->type == IDENT) {
        env_assign(global_env, receiver_expr->name, receiver_expr->len, out_self);
    }
    return result;
}

static FnShape get_fn_shape(AstNode *decl) {
    if (decl->type == METHOD) {
        return {
            decl->params,
            decl->param_count,
            decl->name,
            decl->len,
            decl->return_type,
            decl->body
        };
    }
    if (decl->type == PRIMITIVE) {
        return {
            decl->params,
            decl->param_count,
            decl->name,
            decl->len,
            decl->return_type,
            decl->body
        };
    }
    return {
        decl->params,
        decl->param_count,
        decl->name,
        decl->len,
        decl->return_type,
        decl->body
    };
}