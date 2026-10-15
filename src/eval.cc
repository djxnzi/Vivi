static Value* get_address(AstNode *node) {
    if (node->type == IDENT) {
        EnvEntry *e = env_find(global_env, node->as.ident.name, node->as.ident.len);
        if (!e) { print_err("undefined", node->as.ident.len, node->as.ident.name); return nullptr; }
        return &e->value;
    }
    if (node->type == INDEX) {
        Value cur = eval_expr(node->as.index.object);
        Value *slot = nullptr;
        int last = node->as.index.index_count - 1;
        for (int d = 0; d <= last; d++) {
            if (cur.type != ValueType::Array) { print_err("arr_index"); return nullptr; }
            ObjArray *a = (ObjArray*)cur.as.obj;
            Value idx = eval_expr(node->as.index.indices[d]);
            int pos = -1;
            if (idx.type == ValueType::Int) {
                pos = array_find_index(a, idx.as.i);
            } else if (idx.type == ValueType::Str || idx.type == ValueType::Rune) {
                for (int i = 0; i < a->count && pos < 0; i++) {
                    if (values_equal(a->keys[i], idx)) pos = i;
                }
            } else {
                print_err("int_or_str");
                return nullptr;
            }
            if (pos < 0) { print_err("index_not_found"); return nullptr; }
            slot = &a->items[pos];
            cur = a->items[pos];
        }
        return slot;
    }
    if (node->type == FIELD_ACCESS) {
        Value obj = eval_expr(node->as.field_access.object);
        if (obj.type != ValueType::Struct) { print_err("field_assignment"); return nullptr; }
        ObjStruct *s = (ObjStruct*)obj.as.obj;
        const char *fn = node->as.field_access.field; int fl = node->as.field_access.len;
        for (int i = 0; i < s->count; i++) {
            if (s->fields[i].len == fl && strncmp(s->fields[i].name, fn, fl) == 0) return &s->fields[i].value;
        }
        print_err("no_field", fl, fn);
        return nullptr;
    }
    print_err("unary");
    return nullptr;
}

static Value auto_deref(AstNode *obj_node, Value obj) {
    if (obj_node->type != IDENT || obj.type != ValueType::Int) return obj;
    EnvEntry *e = env_find(global_env, obj_node->as.ident.name, obj_node->as.ident.len);
    if (!e || !e->type_annotation) return obj;
    if (e->type_annotation->type != UNARY || e->type_annotation->as.unary.op != TOK_STAR) return obj;
    return *(Value*)obj.as.i;
}

static Value eval_expr(AstNode *node) {
    current_line = node->line;
    current_col = node->col;

    switch (node->type) {
        case INT_LIT:   return make_int(node->as.int_lit.value);
        case FLOAT_LIT: return make_float(node->as.float_lit.value);
        case BOOL_LIT:  return make_bool(node->as.bool_lit.value);
        case NULL_LIT:  return make_null();
        case STR_LIT:   return make_str(node->as.str_lit.start, node->as.str_lit.len);
        case RUNE_LIT:  return make_rune(node->as.rune_lit.start, node->as.rune_lit.len);
        
        case CALL: {

            if (node->as.call.callee->type == IDENT) {
                AstNode *primitive = find_primitive(node->as.call.callee->as.ident.name, node->as.call.callee->as.ident.len);

                if (primitive) {
                    Value *arg_values = nullptr;

                    if (node->as.call.arg_count > 0) {
                        arg_values = (Value*)arena_alloc(sizeof(Value) * node->as.call.arg_count);

                        for (int i = 0; i < node->as.call.arg_count; i++) {
                            arg_values[i] = eval_expr(node->as.call.args[i]);
                        }
                    }

                    return call_primitive(primitive, arg_values, node->as.call.arg_count);
                }
            }

            if (node->as.call.callee->type == IDENT) {
                Value maybe_builtin;
                if (env_get(global_env, node->as.call.callee->as.ident.name, node->as.call.callee->as.ident.len, &maybe_builtin) &&
                    maybe_builtin.type == ValueType::Fn && ((ObjFn*)maybe_builtin.as.obj)->is_native) {
                    Value *arg_values = nullptr;
                    if (node->as.call.arg_count > 0) {
                        arg_values = (Value*)arena_alloc(sizeof(Value) * node->as.call.arg_count);
                        for (int i = 0; i < node->as.call.arg_count; i++) arg_values[i] = eval_expr(node->as.call.args[i]);
                    }
                    return call_function((ObjFn*)maybe_builtin.as.obj, arg_values, node->as.call.arg_count, nullptr, node->line, node->col);
                }
            }

            if (node->as.call.callee->type == IDENT) {
                AstNode *struct_decl = find_struct_decl(node->as.call.callee->as.ident.name, node->as.call.callee->as.ident.len);
                if (struct_decl) {
                    ObjStruct *s = (ObjStruct*)arena_alloc(sizeof(ObjStruct));
                    s->obj.type = ValueType::Struct;
                    s->obj.refcount = 0;
                    s->type_name = struct_decl->as.struct_decl.name;
                    s->type_name_len = struct_decl->as.struct_decl.len;
                    int field_count = struct_decl->as.struct_decl.field_count;
                    s->fields = field_count ? (StructField*)arena_alloc(sizeof(StructField) * field_count) : nullptr;
                    s->count = s->capacity = field_count;
                    for (int i = 0; i < field_count; i++) {
                        AstNode *f = struct_decl->as.struct_decl.fields[i];
                        s->fields[i].name = f->as.field.name;
                        s->fields[i].len = f->as.field.len;
                        if (i < node->as.call.arg_count) {
                            s->fields[i].value = eval_expr(node->as.call.args[i]);
                        } else {
                            s->fields[i].value = f->as.field.value ? eval_expr(f->as.field.value) : make_null();
                        }
                    }
                    Value v; v.type = ValueType::Struct; v.as.obj = (Obj*)s;
                    return v;
                }
            }

            if (node->as.call.callee->type == FIELD_ACCESS) {
                AstNode *fa = node->as.call.callee;
                Value obj = eval_expr(fa->as.field_access.object);

                if (obj.type == ValueType::Frame) {
                    ObjFrame *frm = (ObjFrame*)obj.as.obj;
                    const char *fname = fa->as.field_access.field;
                    int flen = fa->as.field_access.len;
                    EnvEntry *entry = env_find_local(frm->frame, fname, flen);
                    if (!entry || entry->value.type != ValueType::Fn || entry->is_const) {
                        print_err("no_field", flen, fname);
                        return make_null();
                    }
                    ObjFn *fn = (ObjFn*)entry->value.as.obj;
                    Value *arg_values = nullptr;
                    if (node->as.call.arg_count > 0) {
                        arg_values = (Value*)arena_alloc(sizeof(Value) * node->as.call.arg_count);
                        for (int j = 0; j < node->as.call.arg_count; j++) arg_values[j] = eval_expr(node->as.call.args[j]);
                    }
                    return call_function(fn, arg_values, node->as.call.arg_count, nullptr, node->line, node->col);
                }

                if (obj.type == ValueType::Struct) {
                    ObjStruct *s = (ObjStruct*)obj.as.obj;
                    const char *fname = fa->as.field_access.field;
                    int flen = fa->as.field_access.len;
                    for (int i = 0; i < s->count; i++) {
                        if (s->fields[i].len == flen && strncmp(s->fields[i].name, fname, flen) == 0) {
                            if (s->fields[i].value.type != ValueType::Fn) {
                                print_err("not_function", flen, fname);
                                return make_null();
                            }
                            ObjFn *fn = (ObjFn*)s->fields[i].value.as.obj;
                            Value *arg_values = nullptr;
                            if (node->as.call.arg_count > 0) {
                                arg_values = (Value*)arena_alloc(sizeof(Value) * node->as.call.arg_count);
                                for (int j = 0; j < node->as.call.arg_count; j++) arg_values[j] = eval_expr(node->as.call.args[j]);
                            }
                            return call_function(fn, arg_values, node->as.call.arg_count, &obj, node->line, node->col);
                        }
                    }
                    ObjFn *method_fn = find_method(fname, flen);
                    if (method_fn) {
                        Value *arg_values = nullptr;
                        if (node->as.call.arg_count > 0) {
                            arg_values = (Value*)arena_alloc(sizeof(Value) * node->as.call.arg_count);
                            for (int j = 0; j < node->as.call.arg_count; j++) arg_values[j] = eval_expr(node->as.call.args[j]);
                        }
                        return call_method(method_fn, fa->as.field_access.object, obj, arg_values, node->as.call.arg_count, node->line, node->col);
                    }
                    print_err("not_method", flen, fname);
                    return make_null();
                }
                Value *arg_values = nullptr;
                if (node->as.call.arg_count > 0) {
                    arg_values = (Value*)arena_alloc(sizeof(Value) * node->as.call.arg_count);
                    for (int i = 0; i < node->as.call.arg_count; i++) arg_values[i] = eval_expr(node->as.call.args[i]);
                }
                ObjFn *method_fn = find_method(fa->as.field_access.field, fa->as.field_access.len);
                if (method_fn) {
                    return call_method(method_fn, fa->as.field_access.object, obj, arg_values, node->as.call.arg_count, node->line, node->col);
                }
                //return call_method_native(fa->as.field_access.field, fa->as.field_access.len, obj, arg_values, node->as.call.arg_count);
            }

            Value callee = eval_expr(node->as.call.callee);
            if (callee.type == ValueType::Str && node->as.call.arg_count == 1) {
                ObjStr *s = (ObjStr*)callee.as.obj;
                Value arg = eval_expr(node->as.call.args[0]);
                return eval_typecast(s->chars, s->len, arg);
            }
            if (callee.type != ValueType::Fn) {
                print_err("not_callable");
                return make_null();
            }
            ObjFn *fn = (ObjFn*)callee.as.obj;

            Value *arg_values = nullptr;
            if (node->as.call.arg_count > 0) {
                arg_values = (Value*)arena_alloc(sizeof(Value) * node->as.call.arg_count);
                FnShape shape = get_fn_shape(fn->decl);
                for (int i = 0; i < node->as.call.arg_count; i++) {
                    AstNode *arg = node->as.call.args[i];
                    AstNode *ptype = (i < shape.param_count) ? shape.params[i]->as.field.type_expr : nullptr;
                    if (arg->type == IDENT && ptype && ptype->type == UNARY && ptype->as.unary.op == TOK_STAR) {
                        Value *slot = get_address(arg);
                        arg_values[i] = slot ? make_int((long)slot) : make_null();
                    } else {
                        arg_values[i] = eval_expr(arg);
                    }
                }
            }

            return call_function(fn, arg_values, node->as.call.arg_count, nullptr, node->line, node->col);
        }

        case UNARY: {
            if (node->as.unary.op == TOK_AMP) {
                Value *target = get_address(node->as.unary.operand);
                if (!target) return make_null();
                return make_int((long)target);
            }
            Value v = eval_expr(node->as.unary.operand);
            switch (node->as.unary.op) {
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
                    print_err("unhandled", (int)node->as.unary.op);
                    return make_null();
            }
        }

        case BINARY: {
            TokenType op = node->as.binary.op;

            if (op == TOK_AND) {
                if (!is_truthy(eval_expr(node->as.binary.left))) return make_bool(false);
                return make_bool(is_truthy(eval_expr(node->as.binary.right)));
            }
            if (op == TOK_OR) {
                if (is_truthy(eval_expr(node->as.binary.left))) return make_bool(true);
                return make_bool(is_truthy(eval_expr(node->as.binary.right)));
            }

            if (op == TOK_PIPE) {
                Value l = eval_expr(node->as.binary.left);
                if (l.type == ValueType::Int) {
                    Value r = eval_expr(node->as.binary.right);
                    if (r.type == ValueType::Int) return make_int(l.as.i | r.as.i);
                    return l;
                }
                if (l.type != ValueType::Null) return l;
                return eval_expr(node->as.binary.right);
            }

            if (op == TOK_EQ || op == TOK_NEQ) {
                AstNode *left = node->as.binary.left, *right = node->as.binary.right;
                ValueType vt;
                AstNode *type_operand = nullptr, *value_operand = nullptr;
                bool is_custom_type = false;

                if (left->type == IDENT && ident_is_type_name(left->as.ident.name, left->as.ident.len, &vt)) {
                    type_operand = left; value_operand = right;
                } else if (right->type == IDENT && ident_is_type_name(right->as.ident.name, right->as.ident.len, &vt)) {
                    type_operand = right; value_operand = left;
                } else if (left->type == IDENT && find_struct_decl(left->as.ident.name, left->as.ident.len)) {
                    type_operand = left; value_operand = right; is_custom_type = true;
                } else if (right->type == IDENT && find_struct_decl(right->as.ident.name, right->as.ident.len)) {
                    type_operand = right; value_operand = left; is_custom_type = true;
                }

                if (type_operand) {
                    Value v = eval_expr(value_operand);
                    bool matches;
                    if (is_custom_type) {
                        matches = false;
                        if (v.type == ValueType::Struct) {
                            ObjStruct *s = (ObjStruct*)v.as.obj;
                            matches = s->type_name && s->type_name_len == type_operand->as.ident.len &&
                                strncmp(s->type_name, type_operand->as.ident.name, type_operand->as.ident.len) == 0;
                        }
                    } else {
                        matches = (v.type == vt);
                    }
                    return make_bool(op == TOK_EQ ? matches : !matches);
                }
            }

            Value l = eval_expr(node->as.binary.left);
            Value r = eval_expr(node->as.binary.right);

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
            if (node->as.unary.op == TOK_STAR) {
                Value p = eval_expr(node->as.unary.operand);
                if (p.type != ValueType::Int) { print_err("deref_type"); return make_null(); }
                return *(Value*)p.as.i;
            }
            AstNode *op_node = node->as.unary.operand;
            TokenType math_op = (node->as.unary.op == TOK_PLUSPLUS) ? TOK_PLUS : TOK_MINUS;
            Value *slot = nullptr;
            Value old;

            if (op_node->type == FIELD_ACCESS) {
                Value obj = eval_expr(op_node->as.field_access.object);
                if (obj.type != ValueType::Struct) {
                    print_err("field_assignment");
                    return make_null();
                }

                ObjStruct *s = (ObjStruct*)obj.as.obj;
                const char *fn = op_node->as.field_access.field; int fl = op_node->as.field_access.len;
                for (int i = 0; i < s->count && !slot; i++) {
                    if (s->fields[i].len == fl && strncmp(s->fields[i].name, fn, fl) == 0) {
                        slot = &s->fields[i].value;
                    }
                }
                if (!slot) {
                    print_err("no_field", fl, fn);
                    return make_null();
                }
            } else if (op_node->type == INDEX) {
                Value cur = eval_expr(op_node->as.index.object);
                for (int d = 0; d < op_node->as.index.index_count; d++) {
                    if (cur.type != ValueType::Array) {
                        print_err("arr_index");
                        return make_null();
                    }

                    ObjArray *a = (ObjArray*)cur.as.obj;
                    Value idx = eval_expr(op_node->as.index.indices[d]);
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
                        return make_null();
                    }

                    if (pos < 0) {
                        print_err("index_not_found");
                        return make_null();
                    }
                    slot = &a->items[pos];
                    cur = a->items[pos];
                }
            }

            if (slot) {
                old = *slot;
                *slot = value_copy(eval_math(math_op, old, make_int(1)));
                return old;
            }
            if (op_node->type != IDENT) {
                old = eval_expr(op_node);
                return eval_math(math_op, old, make_int(1));
            }

            const char *name = op_node->as.ident.name; int len = op_node->as.ident.len;
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
            if (env_get(global_env, node->as.ident.name, node->as.ident.len, &v) &&
                !(v.type == ValueType::Fn && ((ObjFn*)v.as.obj)->is_native)) return v;

            ValueType vt;
            if (ident_is_type_name(node->as.ident.name, node->as.ident.len, &vt))
                return make_str(node->as.ident.name, node->as.ident.len);
            print_err("undefined", node->as.ident.len, node->as.ident.name);
            return make_null();
        }

        case TERNARY:
            return is_truthy(eval_expr(node->as.ternary.cond))
                ? eval_expr(node->as.ternary.then_expr)
                : eval_expr(node->as.ternary.else_expr);

        case ARRAY_LIT: {
            ObjArray *a = (ObjArray*)arena_alloc(sizeof(ObjArray));
            a->obj.type = ValueType::Array;
            a->obj.refcount = 0;
            int n = node->as.array_lit.count;
            a->keys  = n ? (Value*)arena_alloc(sizeof(Value) * n) : nullptr;
            a->items = n ? (Value*)arena_alloc(sizeof(Value) * n) : nullptr;
            a->count = a->capacity = n;
            for (int i = 0; i < n; i++) {
                AstNode *elem = node->as.array_lit.items[i];
                if (elem->type == ARRAY_ENTRY) {
                    a->keys[i]  = eval_expr(elem->as.array_entry.key);
                    a->items[i] = eval_expr(elem->as.array_entry.value);
                } else {
                    a->keys[i]  = make_null();
                    a->items[i] = eval_expr(elem);
                }
            }
            Value v; v.type = ValueType::Array; v.as.obj = (Obj*)a;
            return v;
        }

        case INDEX: {
            Value cur = eval_expr(node->as.index.object);
            for (int d = 0; d < node->as.index.index_count; d++) {
                Value idx = eval_expr(node->as.index.indices[d]);
                if (cur.type != ValueType::Array) {
                    print_err("arr_index");
                    return make_null();
                }
                ObjArray *a = (ObjArray*)cur.as.obj;
                Value next = make_null();
                bool found = false;
                if (idx.type == ValueType::Str || idx.type == ValueType::Rune) {
                    for (int i = 0; i < a->count; i++) {
                        if (values_equal(a->keys[i], idx)) {
                            next = a->items[i]; found = true; break;
                        }
                    }
                } else if (idx.type == ValueType::Int) {
                    int named = array_find_index(a, idx.as.i);
                    if (named >= 0) {
                        next = a->items[named]; found = true;
                    }
                } else {
                    print_err("int_or_str");
                    return make_null();
                }
                if (!found) { print_err("index_not_found"); return make_null(); }
                cur = next;
            }
            return cur;
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
            if (is_truthy(eval_expr(node->as.if_stmt.cond))) {
                return eval_block_as_expr(node->as.if_stmt.then_branch);
            }
            if (node->as.if_stmt.else_branch) {
                if (node->as.if_stmt.else_branch->type == IF_STMT) {
                    return eval_expr(node->as.if_stmt.else_branch);
                }
                return eval_block_as_expr(node->as.if_stmt.else_branch);
            }
            return make_null();
        }

        case STRUCT_LIT: {
            ObjStruct *s = (ObjStruct*)arena_alloc(sizeof(ObjStruct));
            s->obj.type = ValueType::Struct;
            s->obj.refcount = 0;
            s->type_name = nullptr;
            s->type_name_len = 0;
            int n = node->as.struct_lit.count;
            s->fields = n ? (StructField*)arena_alloc(sizeof(StructField) * n) : nullptr;
            s->count = s->capacity = n;
            for (int i = 0; i < n; i++) {
                AstNode *f = node->as.struct_lit.fields[i];
                s->fields[i].name = f->as.field.name;
                s->fields[i].len = f->as.field.len;
                s->fields[i].value = eval_expr(f->as.field.value);
            }
            Value v; v.type = ValueType::Struct; v.as.obj = (Obj*)s;
            return v;
        }

        case FIELD_ACCESS: {
            if (node->as.field_access.object->type == IDENT) {
                AstNode *obj_ident = node->as.field_access.object;
                Enum *enum_ = find_enum_decl(obj_ident->as.ident.name, obj_ident->as.ident.len);
                if (enum_) {
                    const char *fname = node->as.field_access.field;
                    int flen = node->as.field_access.len;
                    for (int i = 0; i < enum_->decl->as.enum_decl.variant_count; i++) {
                        AstNode *variant = enum_->decl->as.enum_decl.variants[i];
                        if (variant->as.field.len == flen && strncmp(variant->as.field.name, fname, flen) == 0) {
                            return enum_->variant_values[i];
                        }
                    }
                    print_err("enum_variant",
                        obj_ident->as.ident.len, obj_ident->as.ident.name, flen, fname);
                    return make_null();
                }
            }

            Value obj = auto_deref(node->as.field_access.object, eval_expr(node->as.field_access.object));
            if (obj.type != ValueType::Struct) {
                ObjFn *method_fn = find_method(node->as.field_access.field, node->as.field_access.len);
                if (method_fn) {
                    return call_method(method_fn, node->as.field_access.object, obj, nullptr, 0, node->line, node->col);
                }
                if (obj.type == ValueType::Array || obj.type == ValueType::Str || obj.type == ValueType::Rune) {
                    //return call_method_native(node->as.field_access.field, node->as.field_access.len, obj, nullptr, 0);
                }
                print_err("field_access");
                return make_null();
            }
            ObjStruct *s = (ObjStruct*)obj.as.obj;
            const char *fname = node->as.field_access.field;
            int flen = node->as.field_access.len;
            for (int i = 0; i < s->count; i++) {
                if (s->fields[i].len == flen && strncmp(s->fields[i].name, fname, flen) == 0) {
                    return s->fields[i].value;
                }
            }
            ObjFn *method_fn = find_method(fname, flen);
            if (method_fn) {
                return call_method(method_fn, node->as.field_access.object, obj, nullptr, 0, node->line, node->col);
            }
            print_err("no_field", flen, fname);
            return make_null();
        }

        default:
            print_err("unhandled_node_type", (int)node->type);
            return make_null();
    }
}

static Value eval_block_as_expr(AstNode *block) {
    int n = block->as.block.stmt_count;
    for (int i = 0; i < n - 1; i++) {
        exec_stmt(block->as.block.stmts[i]);
    }
    if (n > 0) {
        AstNode *last = block->as.block.stmts[n - 1];
        if (last->type == EXPR_STMT) {
            AstNode *expr = last->as.expr_stmt.expr;
            if (expr->type == CALL && expr->as.call.callee->type == FN_LIT &&
                expr->as.call.arg_count == 0) {
                return eval_expr(expr->as.call.callee);
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

        if (param->as.field.is_variadic) {
            int tail_count = (arg_count > i) ? arg_count - i : 0;

            ObjArray *argv_arr = (ObjArray*)arena_alloc(sizeof(ObjArray));
            argv_arr->obj.type = ValueType::Array;
            argv_arr->obj.refcount = 0;
            argv_arr->count = argv_arr->capacity = tail_count;
            argv_arr->keys  = (Value*)arena_alloc(sizeof(Value) * tail_count);
            argv_arr->items = (Value*)arena_alloc(sizeof(Value) * tail_count);

            for (int j = 0; j < tail_count; j++) {
                Value pv = arg_values[i + j];
                if (param->as.field.type_expr) {
                    if (pv.type == ValueType::Null) {
                        print_err("param_null", param->as.field.len, param->as.field.name);
                        current_file = saved_file;
                        program_source = saved_source;
                        global_env = saved;
                        return make_null();
                    }
                    Value coerced;
                    if (!value_matches_type(param->as.field.type_expr, pv, &coerced)) {
                        print_err("param_type_mismatch", param->as.field.len, param->as.field.name);
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
        } else if (param->as.field.value) {
            pv = eval_expr(param->as.field.value);
        } else if (i < arg_count) {
            pv = arg_values[i];
        } else {
            print_err("missing_arg", param->as.field.len, param->as.field.name);
            current_file = saved_file;
            program_source = saved_source;
            global_env = saved;
            return make_null();
        }

        if (param->as.field.type_expr) {
            Value coerced;
            if (!value_matches_type(param->as.field.type_expr, pv, &coerced)) {
                print_err("param_type_mismatch", param->as.field.len, param->as.field.name);
                return make_null();
            }
            pv = coerced;
        }

        env_define(call_env, param->as.field.name, param->as.field.len, pv, false);
    }
    AstNode *signal = exec_stmt(shape.body);

    bool returned_value = signal && signal->type == RETURN_STMT && signal->as.return_stmt.value;
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
            print_err("no_return_value");
            return make_null();
        }
        if (!return_type_matches(shape.return_type, result)) {
            if (shape.return_type->type == IDENT) {
                const char *tname = shape.return_type->as.ident.name;
                int tlen = shape.return_type->as.ident.len;
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
        env_assign(global_env, receiver_expr->as.ident.name, receiver_expr->as.ident.len, out_self);
    }
    return result;
}

static FnShape get_fn_shape(AstNode *decl) {
    if (decl->type == METHOD) {
        return {
            decl->as.method.params,
            decl->as.method.param_count,
            decl->as.method.name,
            decl->as.method.len,
            decl->as.method.return_type,
            decl->as.method.body
        };
    }
    return {
        decl->as.fn_decl.params,
        decl->as.fn_decl.param_count,
        decl->as.fn_decl.name,
        decl->as.fn_decl.len,
        decl->as.fn_decl.return_type,
        decl->as.fn_decl.body
    };
}