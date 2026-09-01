static Env* env_new(Env *parent) {
    Env *env = (Env*)arena_alloc(sizeof(Env));
    env->entries = nullptr;
    env->count = 0;
    env->capacity = 0;
    env->parent = parent;
    env->has_self = false;
    env->defers = nullptr;
    env->defer_count = 0;
    env->defer_capacity = 0;
    env->caller = nullptr;
    env->call_name = nullptr;
    env->call_name_len = 0;
    env->call_line = 0;
    env->call_col = 0;
    env->loop_floor = 0;
    return env;
}

static EnvEntry* env_find_local(Env *env, const char *name, int len) {
    for (int i = 0; i < env->count; i++) {
        EnvEntry *e = &env->entries[i];
        if (e->len == len && strncmp(e->name, name, (size_t)len) == 0) return e;
    }
    return nullptr;
}

static EnvEntry* env_find(Env *env, const char *name, int len) {
    for (Env *e = env; e; e = e->parent) {
        EnvEntry *entry = env_find_local(e, name, len);
        if (entry) return entry;
    }
    return nullptr;
}

static Value value_copy(Value v) {
    if (v.type == ValueType::Struct) {
        ObjStruct *src = (ObjStruct*)v.as.obj;
        ObjStruct *dst = (ObjStruct*)arena_alloc(sizeof(ObjStruct));
        *dst = *src;
        dst->fields = (StructField*)arena_alloc(sizeof(StructField) * src->capacity);
        for (int i = 0; i < src->count; i++) {
            dst->fields[i] = src->fields[i];
            dst->fields[i].value = value_copy(dst->fields[i].value);
        }
        v.as.obj = (Obj*)dst;
    } else if (v.type == ValueType::Array) {
        ObjArray *src = (ObjArray*)v.as.obj;
        ObjArray *dst = (ObjArray*)arena_alloc(sizeof(ObjArray));
        *dst = *src;
        dst->items = (Value*)arena_alloc(sizeof(Value) * src->capacity);
        dst->keys  = (Value*)arena_alloc(sizeof(Value) * src->capacity);
        for (int i = 0; i < src->count; i++) {
            dst->keys[i] = src->keys[i];
            dst->items[i] = value_copy(src->items[i]);
        }
        v.as.obj = (Obj*)dst;
    }
    return v;
}

static bool env_define(Env *env, const char *name, int len, Value value, bool is_const, bool proctime_owned, bool is_local, void *static_box) {
    value = value_copy(value);
    EnvEntry *existing = env_find_local(env, name, len);
    if (existing) {
        if (existing->is_const) {
            print_err("immut_redecl", len, name);
            return false;
        }
        existing->value = value;
        existing->is_const = is_const;
        existing->proctime_owned = proctime_owned;
        existing->is_local = is_local;
        existing->static_box = static_box;
        return true;
    }
    env->entries = (EnvEntry*)grow_array(env->entries, env->count, &env->capacity, sizeof(EnvEntry), 8);
    EnvEntry *e = &env->entries[env->count++];
    e->name = name;
    e->len = len;
    e->value = value;
    e->is_const = is_const;
    e->proctime_owned = proctime_owned;
    e->type_annotation = nullptr;
    e->is_local = is_local;
    e->static_box = static_box;
    return true;
}

static void env_set_type_annotation(Env *env, const char *name, int len, AstNode *type_annotation) {
    EnvEntry *e = env_find_local(env, name, len);
    if (e) e->type_annotation = type_annotation;
}

static bool env_get(Env *env, const char *name, int len, Value *out) {
    EnvEntry *e = env_find(env, name, len);
    if (!e) return false;
    *out = e->static_box ? ((ObjVar*)e->static_box)->value : e->value;
    return true;
}

static bool env_assign(Env *env, const char *name, int len, Value value) {
    value = value_copy(value);
    EnvEntry *e = env_find(env, name, len);
    if (!e) {
        print_err("undefined", len, name);
        return false;
    }
    if (e->is_const) {
        print_err("immut_redecl", len, name);
        return false;
    }
    e->value = value;
    if (e->static_box) ((ObjVar*)e->static_box)->value = value;
    return true;
}

static Env* build_closure_env() {
    if (loop_stack_count <= global_env->loop_floor) return global_env;

    Env *snap = env_new(global_env);
    bool captured_any = false;
    for (int f = global_env->loop_floor; f < loop_stack_count; f++) {
        LoopFrame *frame = &loop_stack[f];
        for (int v = 0; v < frame->var_count; v++) {
            Value val;
            if (env_get(global_env, frame->vars[v].name, frame->vars[v].len, &val)) {
                env_define(snap, frame->vars[v].name, frame->vars[v].len, val, false);
                captured_any = true;
            }
        }
    }
    return captured_any ? snap : global_env;
}

static void loop_stack_push(LoopFrame frame) {
    loop_stack = (LoopFrame*)grow_array(loop_stack, loop_stack_count, &loop_stack_capacity, sizeof(LoopFrame), 8);
    loop_stack[loop_stack_count++] = frame;
}