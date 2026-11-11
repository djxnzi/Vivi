static void bind_implicits(Env *env, Value *self, bool variadic, Value *tail, int count) {
    for (const Implicit &implicit : implicit_table) {
        Value v;
        if (!implicit.make(self, variadic, tail, count, &v)) continue;
        if (env == pt_check_env) {
            pt_declare_name(implicit.name, implicit.len);
            continue;
        }
        env_define(env, implicit.name, implicit.len, make_null(), false);
        env_find_local(env, implicit.name, implicit.len)->value = v;
    }
}

static Value get_implicit(const char *name) {
    int len = (int)strlen(name);
    for (const Implicit &implicit : implicit_table) {
        if (implicit.len != len || strncmp(implicit.name, name, len) != 0) continue;
        Value v;
        if (env_get(global_env, implicit.name, implicit.len, &v)) return v;
        break;
    }
    return make_null();
}

static bool implicit_self(Value *self, bool, Value *, int, Value *out) {
    if (!self) return false;
    *out = *self;
    return true;
}

static bool implicit_argc(Value *, bool variadic, Value *, int count, Value *out) {
    if (!variadic) return false;
    *out = make_int(count);
    return true;
}

static bool implicit_argv(Value *, bool variadic, Value *tail, int count, Value *out) {
    if (!variadic) return false;
    ObjArray *a = (ObjArray*)arena_alloc(sizeof(ObjArray));
    a->obj.type = ValueType::Array;
    a->obj.refcount = 0;
    a->count = a->capacity = count;
    a->keys  = (Value*)arena_alloc(sizeof(Value) * count);
    a->items = (Value*)arena_alloc(sizeof(Value) * count);
    for (int j = 0; j < count; j++) {
        a->keys[j]  = make_null();
        a->items[j] = tail[j];
    }
    out->type = ValueType::Array;
    out->as.obj = (Obj*)a;
    return true;
}