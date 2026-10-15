static bool is_numeric(Value v) {
    return v.type == ValueType::Int || v.type == ValueType::Float;
}

static double as_double(Value v) {
    return v.type == ValueType::Float ? v.as.f : (double)v.as.i;
}

static bool is_truthy(Value v) {
    switch (v.type) {
        case ValueType::Null:  return false;
        case ValueType::Bool:  return v.as.b;
        case ValueType::Int:   return v.as.i != 0;
        case ValueType::Float: return v.as.f != 0.0;
        default: return true;
    }
}

static bool is_ident(AstNode *node, const char *name) {
    int len = (int)strlen(name);
    return node->type == IDENT && node->as.ident.len == len
        && strncmp(node->as.ident.name, name, (size_t)len) == 0;
}

static bool is_ext_annotation(AstNode *type_annotation) {
    return type_annotation && type_annotation->type == IDENT &&
        type_annotation->as.ident.len == 3 && strncmp(type_annotation->as.ident.name, "ext", 3) == 0;
}

static bool ident_is_type_name(const char *name, int len, ValueType *out_vt) {
    static const struct { const char *name; int len; ValueType vt; } type_names[] = {
        {"string", 6, ValueType::Str},  {"rune", 4, ValueType::Rune}, {"bool", 4, ValueType::Bool},
        {"i8", 2, ValueType::Int},  {"i16", 3, ValueType::Int}, {"i32", 3, ValueType::Int}, {"i64", 3, ValueType::Int},
        {"u8", 2, ValueType::Int},  {"u16", 3, ValueType::Int}, {"u32", 3, ValueType::Int}, {"u64", 3, ValueType::Int},
        {"f32", 3, ValueType::Float}, {"f64", 3, ValueType::Float},
        {"array", 5, ValueType::Array},  {"struct", 6, ValueType::Struct},
    };
    for (auto &t : type_names) {
        if (len == t.len && strncmp(name, t.name, t.len) == 0) {
            *out_vt = t.vt;
            return true;
        }
    }
    return false;
}

static bool values_equal(Value a, Value b) {
    bool a_text = (a.type == ValueType::Str || a.type == ValueType::Rune);
    bool b_text = (b.type == ValueType::Str || b.type == ValueType::Rune);
    if (a_text && b_text) {
        ObjStr *sa = (ObjStr*)a.as.obj, *sb = (ObjStr*)b.as.obj;
        return sa->len == sb->len && memcmp(sa->chars, sb->chars, (size_t)sa->len) == 0;
    }
    if (a.type != b.type) {
        if (is_numeric(a) && is_numeric(b)) return as_double(a) == as_double(b);
        return false;
    }
    switch (a.type) {
        case ValueType::Null:  return true;
        case ValueType::Bool:  return a.as.b == b.as.b;
        case ValueType::Int:   return a.as.i == b.as.i;
        case ValueType::Float: return a.as.f == b.as.f;
        default: return false;
    }
}

static bool coerce(Value v, Value *out) {
    if (is_numeric(v)) { *out = v; return true; }
    if (v.type != ValueType::Str && v.type != ValueType::Rune) return false;

    ObjStr *s = (ObjStr*)v.as.obj;
    char *tmp = (char*)arena_alloc(s->len + 1);
    memcpy(tmp, s->chars, (size_t)s->len);
    tmp[s->len] = '\0';

    char *end;
    long i = strtol(tmp, &end, 10);
    if (end != tmp && end == tmp + s->len) {
        *out = make_int(i);
        return true;
    }

    double f = strtod(tmp, &end);
    if (end != tmp && end == tmp + s->len) {
        *out = make_float(f);
        return true;
    }

    return false;
}

static long truncate_int(long v, const char *tname, int tlen) {
    auto is_ty = [&](const char *s) { int l = (int)strlen(s); return tlen == l && strncmp(tname, s, l) == 0; };
    unsigned long uv = (unsigned long)v;

    auto sign_extend = [](unsigned long bits, unsigned long mask, unsigned long sign_bit) -> long {
        bits &= mask;
        if (bits & sign_bit) bits |= ~mask;
        return (long)bits;
    };

    if (is_ty("i8"))  return sign_extend(uv, 0xFFul,       0x80ul);
    if (is_ty("i16")) return sign_extend(uv, 0xFFFFul,     0x8000ul);
    if (is_ty("i32")) return sign_extend(uv, 0xFFFFFFFFul, 0x80000000ul);
    if (is_ty("u8"))  return (long)(uv & 0xFFul);
    if (is_ty("u16")) return (long)(uv & 0xFFFFul);
    if (is_ty("u32")) return (long)(uv & 0xFFFFFFFFul);
    return v;
}

static bool value_matches_type(AstNode *type_annotation, Value v, Value *out) {
    if (!type_annotation) { *out = v; return true; }

    if (type_annotation->type == ARRAY_TYPE) {
        if (v.type != ValueType::Array) return false;

        AstNode *element_type = type_annotation->as.array_type.element_type;

        if (is_ident(element_type, "value")) {
            *out = v;
            return true;
        }

        ObjArray *array = (ObjArray*)v.as.obj;
        for (int i = 0; i < array->count; i++) {
            Value ignored;
            if (!value_matches_type(element_type, array->items[i], &ignored)) {
                return false;
            }
        }

        *out = v;
        return true;
    }

    if (is_ext_annotation(type_annotation)) { *out = v; return true; }
    if (v.type == ValueType::Null) { *out = v; return true; }

    if (type_annotation->type == UNARY && type_annotation->as.unary.op == TOK_STAR) {
        if (v.type != ValueType::Int) return false;
        AstNode *pointee = type_annotation->as.unary.operand;
        if (pointee) {
            Value ignored;
            if (!value_matches_type(pointee, *(Value*)v.as.i, &ignored)) return false;
        }
        *out = v;
        return true;
    }

    ValueType expected_vt;
    if (ident_is_type_name(type_annotation->as.ident.name, type_annotation->as.ident.len, &expected_vt)) {
        const char *tname = type_annotation->as.ident.name;
        int tlen = type_annotation->as.ident.len;
        if (expected_vt == ValueType::Int) {
            Value c;
            if (!coerce(v, &c)) return false;
            long raw = (c.type == ValueType::Float) ? (long)c.as.f : c.as.i;
            *out = make_int(truncate_int(raw, tname, tlen));
            return true;
        }
        if (expected_vt == ValueType::Float) {
            Value c;
            if (!coerce(v, &c)) return false;
            double d = as_double(c);
            if (tlen == 3 && strncmp(tname, "f32", 3) == 0) d = (double)(float)d;
            *out = make_float(d);
            return true;
        }
        if ((expected_vt == ValueType::Str || expected_vt == ValueType::Rune) &&
            (v.type == ValueType::Str || v.type == ValueType::Rune)) {
            ObjStr *s = (ObjStr*)v.as.obj;
            if (expected_vt == ValueType::Rune && s->len != 1) return false;
            Value coerced; coerced.type = expected_vt; coerced.as.obj = v.as.obj;
            *out = coerced;
            return true;
        }
        if (v.type != expected_vt) return false;
        *out = v;
        return true;
    }

    AstNode *struct_decl = find_struct_decl(type_annotation->as.ident.name, type_annotation->as.ident.len);
    if (struct_decl) {
        if (v.type != ValueType::Struct) return false;
        ObjStruct *s = (ObjStruct*)v.as.obj;
        bool ok = s->type_name && s->type_name_len == type_annotation->as.ident.len &&
            strncmp(s->type_name, type_annotation->as.ident.name, type_annotation->as.ident.len) == 0;
        *out = v;
        return ok;
    }

    Value alias;
    if (env_get(global_env, type_annotation->as.ident.name, type_annotation->as.ident.len, &alias) &&
        alias.type == ValueType::Str) {
        ObjStr *s = (ObjStr*)alias.as.obj;
        AstNode tmp = *type_annotation;
        tmp.as.ident.name = s->chars;
        tmp.as.ident.len = s->len;
        return value_matches_type(&tmp, v, out);
    }

    *out = v;
    return true;
}

static bool return_type_matches(AstNode *return_type, Value result) {
    if (return_type->type == ARRAY_TYPE) {
        if (result.type != ValueType::Array) return false;

        AstNode *element_type = return_type->as.array_type.element_type;
        if (is_ident(element_type, "value")) return true;

        ObjArray *array = (ObjArray*)result.as.obj;
        for (int i = 0; i < array->count; i++) {
            if (!return_type_matches(element_type, array->items[i])) {
                return false;
            }
        }
        return true;
    }

    if (return_type->type == BINARY && return_type->as.binary.op == TOK_PIPE) {
        return return_type_matches(return_type->as.binary.left, result)
            || return_type_matches(return_type->as.binary.right, result);
    }
    if (return_type->type == RETURN_TYPE_LIST) {
        if (result.type != ValueType::Array) return false;
        ObjArray *a = (ObjArray*)result.as.obj;
        if (a->count != return_type->as.return_type_list.count) return false;
        for (int i = 0; i < a->count; i++) {
            if (!return_type_matches(return_type->as.return_type_list.types[i], a->items[i])) return false;
        }
        return true;
    }

    if (return_type->type == UNARY && return_type->as.unary.op == TOK_STAR) {
        return result.type == ValueType::Int;
    }

    ValueType expected_vt;
    if (ident_is_type_name(return_type->as.ident.name, return_type->as.ident.len, &expected_vt)) {
        return result.type == expected_vt;
    }
    AstNode *struct_decl = find_struct_decl(return_type->as.ident.name, return_type->as.ident.len);
    if (struct_decl) {
        if (result.type != ValueType::Struct) return false;
        ObjStruct *s = (ObjStruct*)result.as.obj;
        return s->type_name && s->type_name_len == return_type->as.ident.len &&
            strncmp(s->type_name, return_type->as.ident.name, return_type->as.ident.len) == 0;
    }
    return true;
}

static Value eval_math(TokenType op, Value l, Value r) {
    Value lc, rc;
    bool lok = coerce(l, &lc);
    bool rok = coerce(r, &rc);
    if (!lok || !rok) {
        if ((l.type == ValueType::Str && !lok) || (r.type == ValueType::Str && !rok)) {
            print_err("type_mismatch");
        } else {
            print_err("math_gap", (int)op);
        }
        return make_null();
    }
    bool both_int = (lc.type == ValueType::Int && rc.type == ValueType::Int);
    switch (op) {
        case TOK_PLUS:
            return both_int ? make_int(lc.as.i + rc.as.i) : make_float(as_double(lc) + as_double(rc));
        case TOK_MINUS:
            return both_int ? make_int(lc.as.i - rc.as.i) : make_float(as_double(lc) - as_double(rc));
        case TOK_STAR:
            return both_int ? make_int(lc.as.i * rc.as.i) : make_float(as_double(lc) * as_double(rc));
        case TOK_SLASH:
            if (as_double(rc) == 0.0) return make_float(as_double(lc) / as_double(rc));
            return both_int ? make_int(lc.as.i / rc.as.i) : make_float(as_double(lc) / as_double(rc));
        case TOK_PERCENT:
            if (as_double(rc) == 0.0) return make_float(fmod(as_double(lc), as_double(rc)));
            return both_int ? make_int(lc.as.i % rc.as.i) : make_float(fmod(as_double(lc), as_double(rc)));
        case TOK_AMP:
            if (!both_int) { print_err("bitwise_int_only"); return make_null(); }
            return make_int(lc.as.i & rc.as.i);
        case TOK_CARET:
            if (!both_int) { print_err("bitwise_int_only"); return make_null(); }
            return make_int(lc.as.i ^ rc.as.i);
        case TOK_SHL:
            if (!both_int) { print_err("bitwise_int_only"); return make_null(); }
            return make_int(lc.as.i << rc.as.i);
        case TOK_SHR:
            if (!both_int) { print_err("bitwise_int_only"); return make_null(); }
            return make_int(lc.as.i >> rc.as.i);

        default:
            print_err("unhandled_math_op", (int)op);
            return make_null();
    }
}

static void array_needs_capacity(ObjArray *a, int needed) {
    if (needed <= a->capacity) return;
    int new_cap = a->capacity < 8 ? 8 : a->capacity * 2;
    if (new_cap < needed) new_cap = needed;
    Value *new_items = (Value*)arena_alloc(sizeof(Value) * new_cap);
    Value *new_keys  = (Value*)arena_alloc(sizeof(Value) * new_cap);
    for (int i = 0; i < a->count; i++) { new_items[i] = a->items[i]; new_keys[i] = a->keys[i]; }
    a->items = new_items;
    a->keys = new_keys;
    a->capacity = new_cap;
}

static void* grow_array(void *arr, int count, int *capacity, size_t elem_size, int min_cap) {
    if (count < *capacity) return arr;
    int new_cap = *capacity < min_cap ? min_cap : *capacity * 2;
    void *new_arr = arena_alloc(elem_size * new_cap);
    memcpy(new_arr, arr, elem_size * (size_t)count);
    *capacity = new_cap;
    return new_arr;
}

static int array_find_index(ObjArray *a, long target) {
    for (int i = 0; i < a->count; i++)
        if (a->keys[i].type == ValueType::Int && a->keys[i].as.i == target) return i;
    long seen = 0;
    for (int i = 0; i < a->count; i++) {
        if (a->keys[i].type == ValueType::Str || a->keys[i].type == ValueType::Rune ||
            a->keys[i].type == ValueType::Int) continue;
        if (seen == target) return i;
        seen++;
    }
    return -1;
}

void value_print(Value v, FILE *out) {
    switch (v.type) {
        case ValueType::Null:  fprintf(out, "null"); break;
        case ValueType::Bool:  fprintf(out, v.as.b ? "true" : "false"); break;
        case ValueType::Int:   fprintf(out, "%ld", v.as.i); break;
        case ValueType::Float: fprintf(out, "%g", v.as.f); break;
        case ValueType::Str: {
            ObjStr *s = (ObjStr*)v.as.obj;
            fwrite(s->chars, 1, (size_t)s->len, out);
            break;
        }
        case ValueType::Rune: {
            ObjStr *s = (ObjStr*)v.as.obj;
            fwrite(s->chars, 1, (size_t)s->len, out);
            break;
        }
        case ValueType::Struct: {
            ObjStruct *s = (ObjStruct*)v.as.obj;
            if (s->type_name) fwrite(s->type_name, 1, (size_t)s->type_name_len, out);
            fprintf(out, "{");
            for (int i = 0; i < s->count; i++) {
                if (i > 0) fprintf(out, ", ");
                fwrite(s->fields[i].name, 1, (size_t)s->fields[i].len, out);
                fprintf(out, ": ");
                if (s->fields[i].value.type == ValueType::Str) fprintf(out, "\"");
                value_print(s->fields[i].value, out);
                if (s->fields[i].value.type == ValueType::Str) fprintf(out, "\"");
            }
            fprintf(out, "}");
            break;
        }
        case ValueType::Array: {
            ObjArray *a = (ObjArray*)v.as.obj;
            fprintf(out, "[");
            for (int i = 0; i < a->count; i++) {
                if (i > 0) fprintf(out, ", ");
                if (a->keys[i].type == ValueType::Str) {
                    fprintf(out, "\"");
                    value_print(a->keys[i], out);
                    fprintf(out, "\": ");
                }
                if (a->items[i].type == ValueType::Str) fprintf(out, "\"");
                value_print(a->items[i], out);
                if (a->items[i].type == ValueType::Str) fprintf(out, "\"");
            }
            fprintf(out, "]");
            break;
        }
        default: print_err("unprintable", (int)v.type); break;
    }
}

static Value make_null() {
    Value v; v.type = ValueType::Null; v.as.i = 0; return v;
}
static Value make_bool(bool b) {
    Value v; v.type = ValueType::Bool; v.as.b = b; return v;
}
static Value make_int(long i) {
    Value v; v.type = ValueType::Int; v.as.i = i; return v;
}
static Value make_float(double f) {
    Value v; v.type = ValueType::Float; v.as.f = f; return v;
}
static Value make_str(const char *start, int len) {
    ObjStr *s = (ObjStr*)arena_alloc(sizeof(ObjStr));
    s->obj.type = ValueType::Str;
    s->obj.refcount = 0;
    s->chars = start;
    s->len = len;
    Value v; v.type = ValueType::Str; v.as.obj = (Obj*)s;
    return v;
}
static Value make_rune(const char *start, int len) {
    ObjStr *s = (ObjStr*)arena_alloc(sizeof(ObjStr));
    s->obj.type = ValueType::Rune;
    s->obj.refcount = 0;
    s->chars = start;
    s->len = len;
    Value v; v.type = ValueType::Rune; v.as.obj = (Obj*)s;
    return v;
}

static Value make_str_from_value(Value v) {
    char buf[64];
    switch (v.type) {
        case ValueType::Null:  return make_str("null", 4);
        case ValueType::Bool:  return v.as.b ? make_str("true", 4) : make_str("false", 5);
        case ValueType::Int: {
            int n = snprintf(buf, sizeof(buf), "%ld", v.as.i);
            char *heap = (char*)arena_alloc(n);
            memcpy(heap, buf, n);
            return make_str(heap, n);
        }
        case ValueType::Float: {
            int n = snprintf(buf, sizeof(buf), "%g", v.as.f);
            char *heap = (char*)arena_alloc(n);
            memcpy(heap, buf, n);
            return make_str(heap, n);
        }
        case ValueType::Str:
            return v;
        case ValueType::Rune: {
            ObjStr *s = (ObjStr*)v.as.obj;
            return make_str(s->chars, s->len);
        }
        case ValueType::Array:
        case ValueType::Struct: {
            char *buf = nullptr;
            size_t size = 0;
            FILE *stream = open_memstream(&buf, &size);
            value_print(v, stream);
            fclose(stream);
            char *heap = (char*)arena_alloc((int)size);
            memcpy(heap, buf, size);
            free(buf);
            return make_str(heap, (int)size);
        }
        default:
            print_err("string_cast");
            return make_null();
    }
}

static Value make_ext_asset(AstNode *path_expr) {
    Value path_val = eval_expr(path_expr);
    if (path_val.type != ValueType::Str) {
        print_err("ext_not_str");
        return make_null();
    }
    ObjStr *ps = (ObjStr*)path_val.as.obj;
    std::string rel(ps->chars, (size_t)ps->len);
    fs::path full = fs::path(current_file).parent_path() / rel;

    bool exists = fs::exists(full);
    bool is_file = exists && fs::is_regular_file(full);
    long size = is_file ? (long)fs::file_size(full) : 0;
    std::string full_str = full.string();

    char *path_copy = (char*)arena_alloc((int)full_str.length());
    memcpy(path_copy, full_str.data(), full_str.length());

    ObjStruct *s = (ObjStruct*)arena_alloc(sizeof(ObjStruct));
    s->obj.type = ValueType::Struct;
    s->obj.refcount = 0;
    s->type_name = "ext";
    s->type_name_len = 3;
    s->fields = (StructField*)arena_alloc(sizeof(StructField) * 3);
    s->count = s->capacity = 3;
    s->fields[0].name = "path";   s->fields[0].len = 4; s->fields[0].value = make_str(path_copy, (int)full_str.length());
    s->fields[1].name = "exists"; s->fields[1].len = 6; s->fields[1].value = make_bool(exists);
    s->fields[2].name = "size";   s->fields[2].len = 4; s->fields[2].value = make_int(size);

    Value v; v.type = ValueType::Struct; v.as.obj = (Obj*)s;
    return v;
}