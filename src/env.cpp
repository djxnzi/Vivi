static Env* env_new(Env *parent) {
    Env *env = (Env*)value_alloc(sizeof(Env));
    env->entries = nullptr;
    env->count = 0;
    env->capacity = 0;
    env->parent = parent;
    env->defers = nullptr;
    env->defer_count = 0;
    env->defer_capacity = 0;
    env->uses = nullptr;
    env->use_count = 0;
    env->use_capacity = 0;
    env->caller = nullptr;
    env->call_name = nullptr;
    env->call_name_len = 0;
    env->call_line = 0;
    env->call_col = 0;
    env->call_file = nullptr;
    env->call_source = nullptr;
    env->loop_floor = 0;
    env->refcount = 1;
    if (parent) parent->refcount++;
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
        for (int i = 0; !entry && i < e->use_count; i++) entry = env_find_local(e->uses[i], name, len);
        if (entry) return entry;
    }
    return nullptr;
}

static Value value_copy(Value v, bool own) {
    auto alloc = [own](size_t n) { return own ? value_alloc(n) : arena_alloc(n); };
    switch (v.type) {
        case ValueType::Str:
        case ValueType::Rune: {
            ObjStr *src = (ObjStr*)v.as.obj;
            ObjStr *dst = (ObjStr*)alloc(sizeof(ObjStr));
            *dst = *src;
            dst->chars = (const char*)memcpy(alloc(src->len), src->chars, src->len);
            v.as.obj = (Obj*)dst;
            break;
        }
        case ValueType::Struct: {
            ObjStruct *src = (ObjStruct*)v.as.obj;
            ObjStruct *dst = (ObjStruct*)alloc(sizeof(ObjStruct));
            *dst = *src;
            dst->fields = (StructField*)alloc(sizeof(StructField) * src->capacity);
            for (int i = 0; i < src->count; i++) {
                dst->fields[i] = src->fields[i];
                dst->fields[i].value = value_copy(src->fields[i].value, own);
            }
            v.as.obj = (Obj*)dst;
            break;
        }
        case ValueType::Array: {
            ObjArray *src = (ObjArray*)v.as.obj;
            ObjArray *dst = (ObjArray*)alloc(sizeof(ObjArray));
            *dst = *src;
            dst->items = (Value*)alloc(sizeof(Value) * src->capacity);
            dst->keys  = (Value*)alloc(sizeof(Value) * src->capacity);
            for (int i = 0; i < src->count; i++) {
                dst->keys[i] = value_copy(src->keys[i], own);
                dst->items[i] = value_copy(src->items[i], own);
            }
            v.as.obj = (Obj*)dst;
            break;
        }
        case ValueType::Fn: {
            ObjFn *dst = (ObjFn*)memcpy(alloc(sizeof(ObjFn)), v.as.obj, sizeof(ObjFn));
            if (own && dst->closure) dst->closure->refcount++;
            v.as.obj = (Obj*)dst;
            break;
        }
        case ValueType::Phrase: v.as.obj = (Obj*)memcpy(alloc(sizeof(ObjPhrase)), v.as.obj, sizeof(ObjPhrase)); break;
        case ValueType::Frame: {
            ObjFrame *dst = (ObjFrame*)memcpy(alloc(sizeof(ObjFrame)), v.as.obj, sizeof(ObjFrame));
            dst->result = value_copy(dst->result, own);
            if (own) dst->frame->refcount++;
            v.as.obj = (Obj*)dst;
            break;
        }
        default: return v;
    }
    v.as.obj->refcount = own;
    v.as.obj->born = epoch;
    return v;
}

static void pending_push(void *p, size_t size, long born) {
    if (pending_count == pending_capacity) {
        pending_capacity = pending_capacity ? pending_capacity * 2 : 64;
        pending = (Pending*)realloc(pending, sizeof(Pending) * pending_capacity);
    }
    pending[pending_count++] = { p, size, born };
}

static void value_drop(Value v) {
    size_t size;

    switch (v.type) {
        case ValueType::Str:
        case ValueType::Rune:   size = sizeof(ObjStr); break;
        case ValueType::Array:  size = sizeof(ObjArray); break;
        case ValueType::Struct: size = sizeof(ObjStruct); break;
        case ValueType::Fn:     size = sizeof(ObjFn); break;
        case ValueType::Frame:  size = sizeof(ObjFrame); break;
        case ValueType::Phrase: size = sizeof(ObjPhrase); break;
        default: return;
    }

    if (!v.as.obj->refcount) return;

    long born = v.as.obj->born;
    v.as.obj->refcount = 0;

    pending_push(v.as.obj, size, born);

    if (v.type == ValueType::Str || v.type == ValueType::Rune) {
        ObjStr *s = (ObjStr*)v.as.obj;
        pending_push((void*)s->chars, s->len, born);
    } else if (v.type == ValueType::Array) {
        ObjArray *a = (ObjArray*)v.as.obj;
        for (int i = 0; i < a->count; i++) { value_drop(a->keys[i]); value_drop(a->items[i]); }
        pending_push(a->keys, sizeof(Value) * a->capacity, born);
        pending_push(a->items, sizeof(Value) * a->capacity, born);
    } else if (v.type == ValueType::Struct) {
        ObjStruct *s = (ObjStruct*)v.as.obj;
        for (int i = 0; i < s->count; i++) value_drop(s->fields[i].value);
        pending_push(s->fields, sizeof(StructField) * s->capacity, born);
    } else if (v.type == ValueType::Frame) {
        ObjFrame *f = (ObjFrame*)v.as.obj;
        value_drop(f->result);
        pending_push(f->frame, release, born);
    } else if (v.type == ValueType::Fn) {
        Env *closure = ((ObjFn*)v.as.obj)->closure;
        if (closure) pending_push(closure, release, born);
    }
}

static bool closes_over(Value v, Env *env) {
    return v.type == ValueType::Fn && ((ObjFn*)v.as.obj)->closure == env;
}

static void entry_drop(Env *env, EnvEntry *e) {
    if (e->borrowed || e->static_box) return;
    if (closes_over(e->value, env)) env->refcount++;
    value_drop(e->value);
}

static void env_release(Env *env) {
    if (--env->refcount == 0) env_drop(env);
}

static void env_drop(Env *env) {
    for (int i = 0; i < env->count; i++) {
        EnvEntry *e = &env->entries[i];
        if (e->borrowed || e->static_box) continue;
        if (closes_over(e->value, env)) ((ObjFn*)e->value.as.obj)->closure = nullptr;   // uncounted, and dies with env
        value_drop(e->value);
    }
    if (env->parent) pending_push(env->parent, release, epoch);
    if (env->entries) pending_push(env->entries, sizeof(EnvEntry) * env->capacity, epoch);
    if (env->defers) pending_push(env->defers, sizeof(AstNode*) * env->defer_capacity, epoch);
    pending_push(env, sizeof(Env), epoch);
}

static Scope scope_open() {
    return {
        temp_mark(),
        pending_count,
        ++epoch
    };
}

static void scope_reset(Scope *s) {
    int w = s->pending_base;

    for (int i = s->pending_base; i < pending_count; i++) {
        if (pending[i].born < s->start) {
            pending[w++] = pending[i];
        } else if (pending[i].size == release) {
            env_release((Env*)pending[i].p);
        } else {
            value_release(pending[i].p, pending[i].size);
        }
    }

    pending_count = w;
    temp_release(s->mark);
}

static bool env_define(Env *env, const char *name, int len, Value value, bool is_const, bool proctime_owned, bool is_local, void *static_box) {
    if (!static_box) {
        value = value_copy(value);
    }

    EnvEntry *existing = env_find_local(env, name, len);

    if (existing) {
        if (existing->is_const) {
            print_err("immut_redecl", len, name);
            return false;
        }

        entry_drop(env, existing);
        existing->borrowed = false;
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
    e->borrowed = false;

    if (!static_box && closes_over(value, env)) env->refcount--;

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
    EnvEntry *e = nullptr;

    while (env && !(e = env_find_local(env, name, len))) {
        for (int i = 0; !e && i < env->use_count; i++)
            if ((e = env_find_local(env->uses[i], name, len))) env = env->uses[i];
        if (!e) env = env->parent;
    }

    if (!e) {
        print_err("undefined", len, name);
        return false;
    }

    if (e->is_const) {
        print_err("immut_redecl", len, name);
        return false;
    }

    if (e->static_box) {
        value_drop(((ObjVar*)e->static_box)->value);
    } else {
        entry_drop(env, e);
    }

    e->borrowed = false;
    e->value = value;

    if (!e->static_box && closes_over(value, env)) env->refcount--;
    
    if (e->static_box) {
        ((ObjVar*)e->static_box)->value = value;
    }

    return true;
}

static Env* build_closure_env() {
    Env *base = (scope_env && !global_env->caller) ? scope_env : global_env;
    if (loop_stack_count <= global_env->loop_floor) {
        return base;
    }

    Env *snap = env_new(base);
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

    if (!captured_any) {
        base->refcount--;
        value_release(snap, sizeof(Env));
        return base;
    }

    pending_push(snap, release, epoch);

    return snap;
}

static void loop_stack_push(LoopFrame frame) {
    loop_stack = (LoopFrame*)grow_array(loop_stack, loop_stack_count, &loop_stack_capacity, sizeof(LoopFrame), 8);
    loop_stack[loop_stack_count++] = frame;
}