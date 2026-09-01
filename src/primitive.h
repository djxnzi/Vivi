// Dispatch signatures
enum Backend { BACKEND_INTERPRETER, BACKEND_AOT };
enum SigTag { SIG_INT, SIG_UINT, SIG_FLOAT, SIG_STR, SIG_ARRAY, SIG_STRUCT, SIG_GENERIC, SIG_ABSENT, SIG_PTR };

typedef Value (*Axiom)(AstNode *decl, Value *args, int arg_count);

// Forwrd decl for prims
static void resolve_primitives(AstNode *program);
static SigTag classify(AstNode *type_expr);
static void* resolve_axiom(AstNode *decl);
static Value call_primitive(AstNode *decl, Value *args, int arg_count);

// Typecasts
static Value eval_typecast(const char *name, int len, Value v);

static Backend current_backend = BACKEND_INTERPRETER;