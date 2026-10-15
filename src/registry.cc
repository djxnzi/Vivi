// Primitive
static void register_primitive(AstNode *decl) {
    AstNode *existing = find_primitive(decl->as.primitive.name, decl->as.primitive.len);
    if (existing) {
        if (existing != decl)
            print_err("duplicate_primitive", decl->as.primitive.len, decl->as.primitive.name);
        return;
    }
    primitive_decls = (AstNode**)grow_array(primitive_decls, primitive_decl_count, &primitive_decl_capacity, sizeof(AstNode*), 8);
    primitive_decls[primitive_decl_count++] = decl;
    decl->as.primitive.resolved = resolve_axiom(decl);
}

static AstNode* find_primitive(const char *name, int len) {
    for (int i = 0; i < primitive_decl_count; i++) {
        AstNode *d = primitive_decls[i];
        if (d->as.primitive.file != current_file) continue;
        if (d->as.primitive.len == len && strncmp(d->as.primitive.name, name, len) == 0) {
            return d;
        }
    }
    return nullptr;
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

static ObjFn* find_method(const char *name, int len) {
    bool registered = false;

    for (int i = 0; i < method_decl_count; i++) {
        if (method_decls[i].len == len && strncmp(method_decls[i].name, name, len) == 0) {
            registered = true;
            break;
        }
    }

    if (!registered) return nullptr;

    Value v;
    if (env_get(global_env, name, len, &v) && v.type == ValueType::Fn) {
        return (ObjFn*)v.as.obj;
    }

    return nullptr;
}

// Structs
static void register_struct_decl(AstNode *decl) {
    AstNode *existing = find_struct_decl(decl->as.struct_decl.name, decl->as.struct_decl.len);
    if (existing) {
        if (existing != decl)
            print_err("duplicate_struct", decl->as.struct_decl.len, decl->as.struct_decl.name);
        return;
    }
    struct_decls = (AstNode**)grow_array(struct_decls, struct_decl_count, &struct_decl_capacity, sizeof(AstNode*), 8);
    struct_decls[struct_decl_count++] = decl;
}

static AstNode* find_struct_decl(const char *name, int len) {
    for (int i = 0; i < struct_decl_count; i++) {
        AstNode *d = struct_decls[i];
        if (d->as.struct_decl.len == len && strncmp(d->as.struct_decl.name, name, len) == 0) {
            return d;
        }
    }
    return nullptr;
}

static bool label_matches(AstNode *label, Value scrutinee) {
    if (label->type == BINARY && label->as.binary.op == TOK_SLICE) {
        Value lo = eval_expr(label->as.binary.left);
        Value hi = eval_expr(label->as.binary.right);
        if (!is_numeric(scrutinee) || !is_numeric(lo) || !is_numeric(hi)) {
            print_err("non_numeric_range");
            return false;
        }
        return as_double(scrutinee) >= as_double(lo) && as_double(scrutinee) <= as_double(hi);
    }
    if (label->type == IDENT) {
        ValueType vt;
        if (ident_is_type_name(label->as.ident.name, label->as.ident.len, &vt)) {
            return scrutinee.type == vt;
        }
        AstNode *struct_decl = find_struct_decl(label->as.ident.name, label->as.ident.len);
        if (struct_decl) {
            if (scrutinee.type != ValueType::Struct) return false;
            ObjStruct *s = (ObjStruct*)scrutinee.as.obj;
            return s->type_name && s->type_name_len == label->as.ident.len &&
                strncmp(s->type_name, label->as.ident.name, label->as.ident.len) == 0;
        }
    }
    return values_equal(eval_expr(label), scrutinee);
}

// Enums
static void register_enum_decl(AstNode *decl) {
    int n = decl->as.enum_decl.variant_count;
    Value *values = n ? (Value*)arena_alloc(sizeof(Value) * n) : nullptr;
    Value prev = make_int(-1);
    for (int i = 0; i < n; i++) {
        AstNode *variant = decl->as.enum_decl.variants[i];
        if (variant->as.field.value) {
            values[i] = eval_expr(variant->as.field.value);
        } else if (prev.type == ValueType::Int) {
            values[i] = make_int(prev.as.i + 1);
        } else {
            print_err("enum_variant",
                variant->as.field.len, variant->as.field.name);
            values[i] = make_null();
        }
        prev = values[i];
    }

    enum_decls = (Enum*)grow_array(enum_decls, enum_decl_count, &enum_decl_capacity, sizeof(Enum), 8);
    enum_decls[enum_decl_count].decl = decl;
    enum_decls[enum_decl_count].variant_values = values;
    enum_decl_count++;
}

static Enum* find_enum_decl(const char *name, int len) {
    for (int i = 0; i < enum_decl_count; i++) {
        AstNode *d = enum_decls[i].decl;
        if (d->as.enum_decl.len == len && strncmp(d->as.enum_decl.name, name, len) == 0) {
            return &enum_decls[i];
        }
    }
    return nullptr;
}