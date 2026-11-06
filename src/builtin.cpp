#include <sys/mman.h>
#include <cstdint>

//Added these two imports as they are required for Malloc and free functionality.

static Value builtin_error(ObjFn *fn) {
    print_err("builtin_contract", fn->native_name_len, fn->native_name);
    return make_null();
}

static bool is_text(Value v) {
    return v.type == ValueType::Str || v.type == ValueType::Rune;
}

static char* builtin_cstr(ObjStr *s) {
    char *c = (char*)arena_alloc(s->len + 1);
    memcpy(c, s->chars, (size_t)s->len);
    c[s->len] = '\0';
    return c;
}

static Value builtin_write(ObjFn *fn, Value *args) {
    if (args[0].type != ValueType::Int || !is_text(args[1])) return builtin_error(fn);
    int fd = (int)args[0].as.i;
    ObjStr *s = (ObjStr*)args[1].as.obj;
    if (fd == 1 || fd == 2) {
        FILE *dest = fd == 2 ? stderr : (program_output ? program_output : stdout);
        fwrite(s->chars, 1, (size_t)s->len, dest);
        fflush(dest);
        if (dest == stdout && s->len > 0) stdout_open_line = s->chars[s->len - 1] != '\n';
        return make_int(s->len);
    }
    ssize_t n = write(fd, s->chars, (size_t)s->len);
    if (n < 0) { print_err("io_write", fd, strerror(errno)); return make_null(); }
    return make_int((long)n);
}

static Value builtin_read(ObjFn *fn, Value *args) {
    if (args[0].type != ValueType::Int || args[1].type != ValueType::Int) return builtin_error(fn);
    int fd = (int)args[0].as.i;
    long max = args[1].as.i;
    if (max <= 0 || max > 2147483647L) return builtin_error(fn);
    char *buf = (char*)malloc((size_t)max);
    if (!buf) return builtin_error(fn);
    ssize_t n = read(fd, buf, (size_t)max);
    if (n < 0) {
        int err = errno;
        free(buf);
        print_err("io_read", fd, strerror(err));
        return make_null();
    }
    char *owned = (char*)arena_alloc((int)n + 1);
    memcpy(owned, buf, (size_t)n);
    owned[n] = '\0';
    free(buf);
    return make_str(owned, (int)n);
}

static Value builtin_open(ObjFn *fn, Value *args) {
    if (!is_text(args[0]) || !is_text(args[1])) return builtin_error(fn);
    ObjStr *path = (ObjStr*)args[0].as.obj;
    ObjStr *mode = (ObjStr*)args[1].as.obj;
    auto is_mode = [&](const char *m) { int l = (int)strlen(m); return mode->len == l && strncmp(mode->chars, m, l) == 0; };
    int flags;
    if (is_mode("r"))       flags = O_RDONLY;
    else if (is_mode("w"))  flags = O_WRONLY | O_CREAT | O_TRUNC;
    else if (is_mode("a"))  flags = O_WRONLY | O_CREAT | O_APPEND;
    else if (is_mode("rw")) flags = O_RDWR | O_CREAT;
    else return builtin_error(fn);
    int fd = open(builtin_cstr(path), flags | O_CLOEXEC, 0644);
    if (fd < 0) { print_err("io_open", path->len, path->chars, strerror(errno)); return make_null(); }
    return make_int(fd);
}

static Value builtin_close(ObjFn *fn, Value *args) {
    if (args[0].type != ValueType::Int || args[0].as.i <= 2) return builtin_error(fn);
    int fd = (int)args[0].as.i;
    if (close(fd) < 0) print_err("io_close", fd, strerror(errno));
    return make_null();
}

static Value builtin_run(ObjFn *fn, Value *args) {
    if (!is_text(args[0])) return builtin_error(fn);
    ObjStr *cmd = (ObjStr*)args[0].as.obj;
    fflush(stdout);
    fflush(stderr);
    int status = system(builtin_cstr(cmd));
    if (status < 0) { print_err("io_run", cmd->len, cmd->chars, strerror(errno)); return make_null(); }
    if (WIFEXITED(status)) return make_int(WEXITSTATUS(status));
    if (WIFSIGNALED(status)) return make_int(128 + WTERMSIG(status));
    return make_int(status);
}

static Value builtin_pipe(ObjFn *fn, Value *args) {
    (void)fn; (void)args;
    int fds[2];
    if (pipe2(fds, O_CLOEXEC) < 0) { print_err("io_pipe", strerror(errno)); return make_null(); }
    ObjArray *arr = (ObjArray*)arena_alloc(sizeof(ObjArray));
    arr->obj.type = ValueType::Array;
    arr->obj.refcount = 0;
    arr->count = arr->capacity = 2;
    arr->keys  = (Value*)arena_alloc(sizeof(Value) * 2);
    arr->items = (Value*)arena_alloc(sizeof(Value) * 2);
    arr->keys[0] = make_null();
    arr->keys[1] = make_null();
    arr->items[0] = make_int(fds[0]);
    arr->items[1] = make_int(fds[1]);
    Value v; v.type = ValueType::Array; v.as.obj = (Obj*)arr;
    return v;
}

static Value builtin_spawn(ObjFn *fn, Value *args) {
    if (!is_text(args[0]) || args[1].type != ValueType::Int || args[2].type != ValueType::Int
        || args[3].type != ValueType::Int) return builtin_error(fn);
    ObjStr *cmd = (ObjStr*)args[0].as.obj;
    char *line = builtin_cstr(cmd);
    int in = (int)args[1].as.i, out = (int)args[2].as.i, err = (int)args[3].as.i;
    fflush(stdout);
    fflush(stderr);
    pid_t pid = fork();
    if (pid < 0) { print_err("io_run", cmd->len, cmd->chars, strerror(errno)); return make_null(); }
    if (pid == 0) {
        if (in != 0) dup2(in, 0);
        if (out != 1) dup2(out, 1);
        if (err != 2) dup2(err, 2);
        execl("/bin/sh", "sh", "-c", line, (char*)nullptr);
        _exit(127);
    }
    return make_int(pid);
}

static Value builtin_wait(ObjFn *fn, Value *args) {
    if (args[0].type != ValueType::Int) return builtin_error(fn);
    int status;
    if (waitpid((pid_t)args[0].as.i, &status, 0) < 0) {
        print_err("io_wait", (int)args[0].as.i, strerror(errno));
        return make_null();
    }
    if (WIFEXITED(status)) return make_int(WEXITSTATUS(status));
    if (WIFSIGNALED(status)) return make_int(128 + WTERMSIG(status));
    return make_int(status);
}

static Value builtin_seed(ObjFn *fn, Value *args) {
    if (args[0].type != ValueType::Int) return builtin_error(fn);
    srand((unsigned int)args[0].as.i);
    return make_null();
}

static Value builtin_exit(ObjFn *fn, Value *args) {
    if (args[0].type != ValueType::Int) return builtin_error(fn);
    int code = (int)args[0].as.i;
    fflush(stdout);
    if (code != CODE_OK && vivi.exit_code_debug) fprintf(stderr, "%s\n", exit_code(code));
    exit(code);
}

static Value builtin_traceback(ObjFn *fn, Value *args) {
    (void)fn; (void)args;
    Env *frame = global_env;
    const char *saved_file = current_file;
    const char *saved_source = program_source;
    int line = current_line, col = current_col;
    if (frame->caller && frame->call_line > 0) {
        line = frame->call_line;
        col = frame->call_col;
        current_file = frame->call_file;
        program_source = frame->call_source;
        global_env = frame->caller;
    }
    char *buf = nullptr;
    size_t size = 0;
    FILE *cap = open_memstream(&buf, &size);
    print_traceback(line, col, cap);
    fclose(cap);
    current_file = saved_file;
    program_source = saved_source;
    global_env = frame;
    char *owned = (char*)arena_alloc(size + 1);
    memcpy(owned, buf, size);
    owned[size] = '\0';
    free(buf);
    return make_str(owned, (int)size);
}

static Value builtin_clock(ObjFn *fn, Value *args) {
    (void)fn; (void)args;
    struct timespec ts;
    clock_gettime(CLOCK_REALTIME, &ts);
    return make_float((double)ts.tv_sec + (double)ts.tv_nsec / 1e9);
}

static Value builtin_rand(ObjFn *fn, Value *args) {
    (void)fn; (void)args;
    return make_int(rand());
}

static Value builtin_len(ObjFn *fn, Value *args) {
    switch (args[0].type) {
        case ValueType::Array:  return make_int(((ObjArray*)args[0].as.obj)->count);
        case ValueType::Str:
        case ValueType::Rune:   return make_int(((ObjStr*)args[0].as.obj)->len);
        case ValueType::Struct: return make_int(((ObjStruct*)args[0].as.obj)->count);
        default: return builtin_error(fn);
    }
}

static Value builtin_get(ObjFn *fn, Value *args) {
    if (args[1].type != ValueType::Int) return builtin_error(fn);
    long idx = args[1].as.i;
    switch (args[0].type) {
        case ValueType::Array: {
            ObjArray *array = (ObjArray*)args[0].as.obj;
            if (idx < 0 || idx >= array->count) return builtin_error(fn);
            return array->items[idx];
        }
        case ValueType::Struct: {
            ObjStruct *object = (ObjStruct*)args[0].as.obj;
            if (idx < 0 || idx >= object->count) return builtin_error(fn);
            return object->fields[idx].value;
        }
        case ValueType::Str:
        case ValueType::Rune: {
            ObjStr *string = (ObjStr*)args[0].as.obj;
            if (idx < 0 || idx >= string->len) return builtin_error(fn);
            return make_int((unsigned char)string->chars[idx]);
        }
        default: return builtin_error(fn);
    }
}

static Value builtin_set(ObjFn *fn, Value *args) {
    if (args[0].type != ValueType::Array || args[1].type != ValueType::Int) return builtin_error(fn);
    ObjArray *array = (ObjArray*)args[0].as.obj;
    long slot = args[1].as.i;
    if (slot < 0 || slot > 2147483647L) return builtin_error(fn);

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

static Value builtin_trunc(ObjFn *fn, Value *args) {
    if (args[0].type != ValueType::Array || args[1].type != ValueType::Int) return builtin_error(fn);
    ObjArray *array = (ObjArray*)args[0].as.obj;
    long length = args[1].as.i;
    if (length < 0 || length > array->count) return builtin_error(fn);
    array->count = (int)length;
    return make_null();
}

static Value builtin_cast(ObjFn *fn, Value *args) {
    if (!is_text(args[1])) return builtin_error(fn);
    ObjStr *t = (ObjStr*)args[1].as.obj;
    return eval_typecast(t->chars, t->len, args[0]);
}

static Value builtin_mmap(ObjFn *fn, Value *args) {
    if (args[0].type != ValueType::Int || args[0].as.i <= 0) return builtin_error(fn);
    void *p = mmap(nullptr, (size_t)args[0].as.i, PROT_READ | PROT_WRITE,
                   MAP_PRIVATE | MAP_ANONYMOUS, -1, 0);
    if (p == MAP_FAILED) return make_null();
    return make_int((long)(uintptr_t)p);
}

static Value builtin_munmap(ObjFn *fn, Value *args) {
    if (args[0].type != ValueType::Int || args[1].type != ValueType::Int
        || args[0].as.i == 0 || args[1].as.i <= 0) return builtin_error(fn);
    munmap((void*)(uintptr_t)args[0].as.i, (size_t)args[1].as.i);
    return make_null();
}

static Value builtin_peek(ObjFn *fn, Value *args) {
    if (args[0].type != ValueType::Int || args[0].as.i == 0) return builtin_error(fn);
    return make_int(*(unsigned char*)(uintptr_t)args[0].as.i);
}

static Value builtin_poke(ObjFn *fn, Value *args) {
    if (args[0].type != ValueType::Int || args[1].type != ValueType::Int
        || args[0].as.i == 0) return builtin_error(fn);
    *(unsigned char*)(uintptr_t)args[0].as.i = (unsigned char)args[1].as.i;
    return make_null();
}

static Value builtin_memcpy(ObjFn *fn, Value *args) {
    if (args[0].type != ValueType::Int || args[1].type != ValueType::Int
        || args[2].type != ValueType::Int || args[0].as.i == 0 || args[1].as.i == 0
        || args[2].as.i < 0) return builtin_error(fn);
    memmove((void*)(uintptr_t)args[0].as.i, (void*)(uintptr_t)args[1].as.i,
            (size_t)args[2].as.i);
    return make_null();
}

struct Builtin { const char *name; int arity; NativeFn fn; };

static const Builtin builtin_table[] = {
    { "_mmap",   1, builtin_mmap },
    { "_munmap", 2, builtin_munmap },
    { "_peek",   1, builtin_peek },
    { "_poke",   2, builtin_poke },
    { "_memcpy", 3, builtin_memcpy },
    { "_write",     2, builtin_write },
    { "_exit",      1, builtin_exit },
    { "_traceback", 0, builtin_traceback },
    { "_clock",     0, builtin_clock },
    { "_rand",      0, builtin_rand },
    { "_len",       1, builtin_len },
    { "_get",       2, builtin_get },
    { "_set",       3, builtin_set },
    { "_trunc",     2, builtin_trunc },
    { "_cast",      2, builtin_cast },
    { "_read",      2, builtin_read },
    { "_open",      2, builtin_open },
    { "_close",     1, builtin_close },
    { "_run",       1, builtin_run },
    { "_seed",      1, builtin_seed },
    { "_pipe",      0, builtin_pipe },
    { "_spawn",     4, builtin_spawn },
    { "_wait",      1, builtin_wait },
};

static void register_builtins(Env *env) {
    for (const Builtin &b : builtin_table) {
        ObjFn *fn = (ObjFn*)arena_alloc(sizeof(ObjFn));
        fn->obj.type = ValueType::Fn;
        fn->obj.refcount = 0;
        fn->decl = nullptr;
        fn->closure = nullptr;
        fn->file = nullptr;
        fn->source = nullptr;
        fn->is_native = true;
        fn->native_name = b.name;
        fn->native_name_len = (int)strlen(b.name);
        fn->native_arity = b.arity;
        fn->native = b.fn;
        Value v; v.type = ValueType::Fn; v.as.obj = (Obj*)fn;
        env_define(env, b.name, fn->native_name_len, v, true);
    }
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