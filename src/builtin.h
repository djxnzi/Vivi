static void register_builtins(Env *env);
static Value eval_typecast(const char *name, int len, Value v, char mode = 0);
static Env *builtin_env = nullptr;

static Value builtin_error(ObjFn *fn);
static bool is_text(Value v);
static char* builtin_cstr(ObjStr *s);

static Value builtin_write(ObjFn *fn, Value *args);
static Value builtin_read(ObjFn *fn, Value *args);
static Value builtin_open(ObjFn *fn, Value *args);
static Value builtin_close(ObjFn *fn, Value *args);
static Value builtin_run(ObjFn *fn, Value *args);
static Value builtin_pipe(ObjFn *fn, Value *args);
static Value builtin_spawn(ObjFn *fn, Value *args);
static Value builtin_wait(ObjFn *fn, Value *args);
static Value builtin_seed(ObjFn *fn, Value *args);
static Value builtin_exit(ObjFn *fn, Value *args);
static Value builtin_traceback(ObjFn *fn, Value *args);
static Value builtin_clock(ObjFn *fn, Value *args);
static Value builtin_rand(ObjFn *fn, Value *args);
static Value builtin_len(ObjFn *fn, Value *args);
static Value builtin_get(ObjFn *fn, Value *args);
static Value builtin_set(ObjFn *fn, Value *args);
static Value builtin_trunc(ObjFn *fn, Value *args);
static Value builtin_cast(ObjFn *fn, Value *args);
static Value builtin_mmap(ObjFn *fn, Value *args);
static Value builtin_munmap(ObjFn *fn, Value *args);
static Value builtin_peek(ObjFn *fn, Value *args);
static Value builtin_poke(ObjFn *fn, Value *args);
static Value builtin_memcpy(ObjFn *fn, Value *args);
static Value builtin_sleep(ObjFn *fn, Value *args);

struct Builtin { const char *name; int arity; NativeFn fn; };

static const Builtin builtin_table[] = {
    { "_mmap",      1, builtin_mmap },
    { "_munmap",    2, builtin_munmap },
    { "_peek",      1, builtin_peek },
    { "_poke",      2, builtin_poke },
    { "_memcpy",    3, builtin_memcpy },
    { "_write",     2, builtin_write },
    { "_exit",      1, builtin_exit },
    { "_traceback", 0, builtin_traceback },
    { "_clock",     0, builtin_clock },
    { "_rand",      0, builtin_rand },
    { "_len",       1, builtin_len },
    { "_get",       2, builtin_get },
    { "_set",       3, builtin_set },
    { "_trunc",     2, builtin_trunc },
    { "_cast",      3, builtin_cast },
    { "_read",      2, builtin_read },
    { "_open",      2, builtin_open },
    { "_close",     1, builtin_close },
    { "_run",       1, builtin_run },
    { "_seed",      1, builtin_seed },
    { "_pipe",      0, builtin_pipe },
    { "_spawn",     4, builtin_spawn },
    { "_wait",      1, builtin_wait },
    { "_sleep",     1, builtin_sleep }
};