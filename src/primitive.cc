static void resolve_primitives(AstNode *program) {
    for (int i = 0; i < program->as.program.stmt_count; i++) {
        AstNode *stmt = program->as.program.stmts[i];
        if (stmt->type == PRIMITIVE) {
            register_primitive(stmt);
        }
    }
}

static Value axiom_error(AstNode *decl) {
    print_err("primitive_contract", decl->as.primitive.len, decl->as.primitive.name);
    return make_null();
}

static SigTag classify(AstNode *type_expr) {
    if (!type_expr) return SIG_ABSENT;
    if (type_expr->type == ARRAY_TYPE) return SIG_ARRAY;
    if (type_expr->type == UNARY && type_expr->as.unary.op == TOK_STAR) return SIG_PTR;

    ValueType vt;
    if (ident_is_type_name(type_expr->as.ident.name, type_expr->as.ident.len, &vt)) {
        if (vt == ValueType::Int) return type_expr->as.ident.name[0] == 'u' ? SIG_UINT : SIG_INT;
        if (vt == ValueType::Float) return SIG_FLOAT;
        if (vt == ValueType::Str || vt == ValueType::Rune) return SIG_STR;
        return SIG_GENERIC;
    }

    if (find_struct_decl(type_expr->as.ident.name, type_expr->as.ident.len)) return SIG_STRUCT;
    return SIG_GENERIC;
}

static Value axiom_len(AstNode *decl, Value *args, int arg_count) {
    (void)decl; (void)arg_count;
    switch (args[0].type) {
        case ValueType::Array:  return make_int(((ObjArray*)args[0].as.obj)->count);
        case ValueType::Str:
        case ValueType::Rune:   return make_int(((ObjStr*)args[0].as.obj)->len);
        case ValueType::Struct: return make_int(((ObjStruct*)args[0].as.obj)->count);
        default: return axiom_error(decl);
    }
}

static Value axiom_get(AstNode *decl, Value *args, int arg_count) {
    (void)arg_count;
    long idx = args[1].as.i;
    switch (args[0].type) {
        case ValueType::Array: {
            ObjArray *array = (ObjArray*)args[0].as.obj;
            if (idx < 0 || idx >= array->count) return axiom_error(decl);
            return array->items[idx];
        }
        case ValueType::Struct: {
            ObjStruct *object = (ObjStruct*)args[0].as.obj;
            if (idx < 0 || idx >= object->count) return axiom_error(decl);
            return object->fields[idx].value;
        }
        case ValueType::Str:
        case ValueType::Rune: {
            ObjStr *string = (ObjStr*)args[0].as.obj;
            if (idx < 0 || idx >= string->len) return axiom_error(decl);
            return make_int((unsigned char)string->chars[idx]);
        }
        default: return axiom_error(decl);
    }
}

static Value axiom_set(AstNode *decl, Value *args, int arg_count) {
    (void)arg_count;
    ObjArray *array = (ObjArray*)args[0].as.obj;
    long slot = args[1].as.i;
    if (slot < 0 || slot > 2147483647L) return axiom_error(decl);

    int index = (int)slot;
    array_needs_capacity(array, index + 1);
    for (int i = array->count; i <= index; i++) {
        array->keys[i] = make_null();
        array->items[i] = make_null();
    }
    if (index >= array->count) array->count = index + 1;
    array->items[index] = value_copy(args[2]);
    return make_null();
}

static Value axiom_truncate(AstNode *decl, Value *args, int arg_count) {
    (void)arg_count;
    ObjArray *array = (ObjArray*)args[0].as.obj;
    long length = args[1].as.i;
    if (length < 0 || length > array->count) return axiom_error(decl);
    array->count = (int)length;
    return make_null();
}

static Value axiom_type_name(AstNode *decl, Value *args, int arg_count) {
    (void)decl; (void)arg_count;
    ObjStruct *object = (ObjStruct*)args[0].as.obj;
    if (!object->type_name) return make_str("", 0);
    return make_str(object->type_name, object->type_name_len);
}

static Value axiom_field_name_at(AstNode *decl, Value *args, int arg_count) {
    (void)arg_count;
    ObjStruct *object = (ObjStruct*)args[0].as.obj;
    long idx = args[1].as.i;
    if (idx < 0 || idx >= object->count) return axiom_error(decl);
    StructField *field = &object->fields[idx];
    return make_str(field->name, field->len);
}

static Value axiom_load(AstNode *decl, Value *args, int arg_count) {
    (void)arg_count;
    if (args[0].type != ValueType::Int) return axiom_error(decl);
    return *(Value*)args[0].as.i;
}

static Value axiom_store(AstNode *decl, Value *args, int arg_count) {
    (void)arg_count;
    if (args[0].type != ValueType::Int) return axiom_error(decl);
    *(Value*)args[0].as.i = value_copy(args[1]);
    return make_null();
}

static Value axiom_ptr_add(AstNode *decl, Value *args, int arg_count) {
    (void)arg_count;
    if (args[0].type != ValueType::Int || args[1].type != ValueType::Int) return axiom_error(decl);
    Value *p = (Value*)args[0].as.i;
    return make_int((long)(p + args[1].as.i));
}

static Value axiom_write(AstNode *decl, Value *args, int arg_count) {
    (void)arg_count;
    if (args[0].type != ValueType::Int) return axiom_error(decl);
    if (args[1].type != ValueType::Str && args[1].type != ValueType::Rune) return axiom_error(decl);
    long fd = args[0].as.i;
    ObjStr *s = (ObjStr*)args[1].as.obj;
    FILE *dest = (program_output && fd == 1) ? program_output : (fd == 2 ? stderr : stdout);
    fwrite(s->chars, 1, (size_t)s->len, dest);
    fflush(dest);
    return make_int(s->len);
}

static Value axiom_exit(AstNode *decl, Value *args, int arg_count) {
    (void)arg_count;
    if (args[0].type != ValueType::Int) return axiom_error(decl);
    int code = (int)args[0].as.i;
    if (program_output) {
        fclose(program_output);
        if (program_output_buf) fwrite(program_output_buf, 1, program_output_size, stdout);
        program_output = nullptr;
    }
    if (code != CODE_OK && vivi.exit_code_debug) fprintf(stderr, "%s\n", exit_code(code));
    exit(code);
}

static Value axiom_clock(AstNode *decl, Value *args, int arg_count) {
    (void)decl; (void)args; (void)arg_count;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return make_float((double)ts.tv_sec + (double)ts.tv_nsec / 1e9);
}

static Value axiom_rand(AstNode *decl, Value *args, int arg_count) {
    (void)decl; (void)args; (void)arg_count;
    return make_int(rand());
}

static Value axiom_traceback(AstNode *decl, Value *args, int arg_count) {
    (void)decl; (void)args; (void)arg_count;
    char *buf = nullptr;
    size_t size = 0;
    FILE *cap = open_memstream(&buf, &size);
    print_traceback(current_line, current_col, cap);
    fclose(cap);
    char *owned = (char*)arena_alloc(size + 1);
    memcpy(owned, buf, size);
    owned[size] = '\0';
    free(buf);
    return make_str(owned, (int)size);
}

static Value axiom_cast(AstNode *decl, Value *args, int arg_count) {
    (void)arg_count;
    if (args[1].type != ValueType::Str && args[1].type != ValueType::Rune) return axiom_error(decl);
    ObjStr *t = (ObjStr*)args[1].as.obj;
    return eval_typecast(t->chars, t->len, args[0]);
}

static Value axiom_keys(AstNode *decl, Value *args, int arg_count) {
    (void)arg_count;
    if (args[0].type != ValueType::Array) return axiom_error(decl);
    ObjArray *src = (ObjArray*)args[0].as.obj;
    ObjArray *out = (ObjArray*)arena_alloc(sizeof(ObjArray));
    out->obj.type = ValueType::Array;
    out->obj.refcount = 0;
    out->count = out->capacity = src->count;
    out->keys  = src->count ? (Value*)arena_alloc(sizeof(Value) * src->count) : nullptr;
    out->items = src->count ? (Value*)arena_alloc(sizeof(Value) * src->count) : nullptr;
    for (int i = 0; i < src->count; i++) {
        out->keys[i]  = make_null();
        out->items[i] = src->keys[i];
    }
    Value v; v.type = ValueType::Array; v.as.obj = (Obj*)out;
    return v;
}

static Value axiom_key_set(AstNode *decl, Value *args, int arg_count) {
    (void)arg_count;
    if (args[0].type != ValueType::Array) return axiom_error(decl);
    ObjArray *array = (ObjArray*)args[0].as.obj;
    long slot = args[1].as.i;
    if (slot < 0 || slot >= array->count) return axiom_error(decl);
    Value old = array->keys[slot];
    array->keys[slot] = value_copy(args[2]);
    return old;
}

// Interpreter signature matching
static void* resolve_axiom_interpreter(AstNode *decl) {
    int n = decl->as.primitive.param_count;
    AstNode **p = decl->as.primitive.params;

    SigTag r0  = n > 0 ? classify(p[0]->as.field.type_expr) : SIG_ABSENT;
    SigTag a1  = n > 1 ? classify(p[1]->as.field.type_expr) : SIG_ABSENT;
    SigTag ret = classify(decl->as.primitive.return_type);
    bool receiver = r0 == SIG_ARRAY || r0 == SIG_STR || r0 == SIG_STRUCT || r0 == SIG_ABSENT;

    if (n == 1 && receiver && ret == SIG_UINT) return (void*)axiom_len;
    if (n == 2 && receiver && a1 == SIG_UINT && (ret == SIG_GENERIC || ret == SIG_INT)) return (void*)axiom_get;
    if (n == 3 && receiver && a1 == SIG_UINT && ret == SIG_ABSENT) return (void*)axiom_set;
    if (n == 2 && receiver && a1 == SIG_UINT && ret == SIG_ABSENT) return (void*)axiom_truncate;
    if (n == 1 && r0 == SIG_STRUCT && ret == SIG_STR) return (void*)axiom_type_name;
    if (n == 2 && r0 == SIG_STRUCT && a1 == SIG_UINT && ret == SIG_STR) return (void*)axiom_field_name_at;
    if (n == 1 && r0 == SIG_PTR && ret == SIG_GENERIC) return (void*)axiom_load;
    if (n == 2 && r0 == SIG_PTR && a1 == SIG_GENERIC && ret == SIG_ABSENT) return (void*)axiom_store;
    if (n == 2 && r0 == SIG_PTR && a1 == SIG_UINT && ret == SIG_PTR) return (void*)axiom_ptr_add;
    if (n == 2 && r0 == SIG_ABSENT && a1 == SIG_STR && ret == SIG_GENERIC) return (void*)axiom_cast;
    if (n == 2 && r0 == SIG_INT && a1 == SIG_STR && ret == SIG_INT) return (void*)axiom_write;
    if (n == 1 && r0 == SIG_INT && ret == SIG_ABSENT) return (void*)axiom_exit;
    if (n == 0 && ret == SIG_FLOAT) return (void*)axiom_clock;
    if (n == 0 && ret == SIG_INT) return (void*)axiom_rand;
    if (n == 0 && ret == SIG_STR) return (void*)axiom_traceback;
    if (n == 1 && receiver && ret == SIG_GENERIC) return (void*)axiom_keys;
    if (n == 3 && receiver && a1 == SIG_UINT && ret == SIG_GENERIC) return (void*)axiom_key_set;

    return nullptr;
}

// Target stub
static void* resolve_axiom_aot(AstNode *decl) {
    (void)decl;
    return nullptr;
}

static void* resolve_axiom(AstNode *decl) {
    switch (current_backend) {
        case BACKEND_INTERPRETER: return resolve_axiom_interpreter(decl);
        case BACKEND_AOT:         return resolve_axiom_aot(decl);
    }
    return nullptr;
}

static Value call_primitive(AstNode *decl, Value *args, int arg_count) {
    int expected = decl->as.primitive.param_count;

    for (int i = 0; i < expected; i++) {
        if (decl->as.primitive.params[i]->as.field.is_variadic) {
            print_err("primitive_variadic");
            return make_null();
        }
    }

    if (arg_count != expected) {
        print_err("primitive_arity", decl->as.primitive.len, decl->as.primitive.name, expected, arg_count);
        return make_null();
    }

    for (int i = 0; i < expected; i++) {
        AstNode *param = decl->as.primitive.params[i];
        Value checked = args[i];
        if (param->as.field.type_expr) {
            if (args[i].type == ValueType::Null) {
                print_err("primitive_null_arg", i + 1, decl->as.primitive.len, decl->as.primitive.name);
                return make_null();
            }
            if (!value_matches_type(param->as.field.type_expr, args[i], &checked)) {
                print_err("primitive_arg_type", i + 1, decl->as.primitive.len, decl->as.primitive.name);
                return make_null();
            }
        }
        args[i] = checked;
    }

    Axiom implementation = (Axiom)decl->as.primitive.resolved;
    if (!implementation) {
        print_err("primitive_unavailable", decl->as.primitive.len, decl->as.primitive.name);
        return make_null();
    }

    Value result = implementation(decl, args, arg_count);
    if (decl->as.primitive.return_type &&
        !return_type_matches(decl->as.primitive.return_type, result)) {
        print_err("primitive_return_type", decl->as.primitive.len, decl->as.primitive.name);
        return make_null();
    }
    return result;
}

static Value eval_typecast(const char *tname, int tlen, Value v) {
    auto is_ty = [&](const char *s) { int l = (int)strlen(s); return tlen == l && strncmp(tname, s, l) == 0; };

    if (is_ty("bool")) return make_bool(is_truthy(v));
    if (is_ty("string")) return make_str_from_value(v);

    if (is_ty("rune")) {
        if (v.type == ValueType::Rune) return v;
        if (v.type == ValueType::Str && ((ObjStr*)v.as.obj)->len == 1)
            return make_rune(((ObjStr*)v.as.obj)->chars, 1);
        if (v.type == ValueType::Int && v.as.i >= 0 && v.as.i <= 255) {
            char *c = (char*)arena_alloc(1);
            c[0] = (char)v.as.i;
            return make_rune(c, 1);
        }
        print_err("cast_mismatch", tlen, tname);
        return make_null();
    }

    bool to_int   = is_ty("i8") || is_ty("i16") || is_ty("i32") || is_ty("i64") ||
                    is_ty("u8") || is_ty("u16") || is_ty("u32") || is_ty("u64");
    bool to_float = is_ty("f32") || is_ty("f64");

    if (to_int || to_float) {
        if (v.type == ValueType::Rune) {
            long code = (long)(unsigned char)((ObjStr*)v.as.obj)->chars[0];
            if (to_int) return make_int(truncate_int(code, tname, tlen));
            return make_float((double)code);
        }
        char *tmp = nullptr;
        if (v.type == ValueType::Str) {
            ObjStr *s = (ObjStr*)v.as.obj;
            tmp = (char*)arena_alloc(s->len + 1);
            memcpy(tmp, s->chars, (size_t)s->len);
            tmp[s->len] = '\0';
        }
        if (to_int) {
            bool have_raw = true;
            long raw = 0;
            if (v.type == ValueType::Int)        raw = v.as.i;
            else if (v.type == ValueType::Float) raw = (long)v.as.f;
            else if (v.type == ValueType::Bool)  raw = v.as.b ? 1 : 0;
            else if (tmp)                        raw = strtol(tmp, nullptr, 10);
            else have_raw = false;
            if (have_raw) return make_int(truncate_int(raw, tname, tlen));
        } else {
            bool have_raw = true;
            double raw = 0;
            if (v.type == ValueType::Float)      raw = v.as.f;
            else if (v.type == ValueType::Int)   raw = (double)v.as.i;
            else if (v.type == ValueType::Bool)  raw = v.as.b ? 1.0 : 0.0;
            else if (tmp)                        raw = strtod(tmp, nullptr);
            else have_raw = false;
            if (have_raw) {
                if (is_ty("f32")) raw = (double)(float)raw;
                return make_float(raw);
            }
        }
    }

    print_err("cast_mismatch", tlen, tname);
    return make_null();
}