// Implicits
struct Implicit {
    const char *name; int len;
    bool (*make)(Value *self, bool variadic, Value *tail, int count, Value *out);
};

static void bind_implicits(Env *env, Value *self, bool variadic, Value *tail, int count);
static Value get_implicit(const char *name);
static bool implicit_self(Value *self, bool variadic, Value *tail, int count, Value *out);
static bool implicit_argc(Value *self, bool variadic, Value *tail, int count, Value *out);
static bool implicit_argv(Value *self, bool variadic, Value *tail, int count, Value *out);

static const Implicit implicit_table[] = {
    { "self", 4, implicit_self },
    { "argc", 4, implicit_argc },
    { "argv", 4, implicit_argv },
};