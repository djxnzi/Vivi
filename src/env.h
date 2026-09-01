static Env *global_env = nullptr;
static Env* env_new(Env *parent);
static EnvEntry* env_find_local(Env *env, const char *name, int len);
static EnvEntry* env_find(Env *env, const char *name, int len);
static Value value_copy(Value v);
static bool env_define(Env *env, const char *name, int len, Value value, bool is_const, bool proctime_owned = false, bool is_local = false, void *static_box = nullptr);
static void env_set_type_annotation(Env *env, const char *name, int len, AstNode *type_annotation);
static bool env_get(Env *env, const char *name, int len, Value *out);
static bool env_assign(Env *env, const char *name, int len, Value value);
static Env* build_closure_env();
static void print_frame_chain(Env *env);
static void print_traceback(int err_line, int err_col, FILE *dest);

struct LoopVar { const char *name; int len; };
struct LoopFrame { LoopVar vars[2]; int var_count; };
static LoopFrame *loop_stack = nullptr;
static int loop_stack_count = 0, loop_stack_capacity = 0;
static void loop_stack_push(LoopFrame frame);