static void register_builtins(Env *env);
static Value eval_typecast(const char *name, int len, Value v);
static Env *builtin_env = nullptr;