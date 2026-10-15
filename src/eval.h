static Value return_value;
static int current_line = 0, current_col = 0;
static Value* get_address(AstNode *node);
static Value auto_deref(AstNode *obj_node, Value obj);
static Value eval_expr(AstNode *node);
static Value eval_block_as_expr(AstNode *block);
static Value call_function(ObjFn *fn, Value *arg_values, int arg_count, Value *self = nullptr, int call_line = 0, int call_col = 0, Value *out_self = nullptr);
static Value call_method(ObjFn *fn, AstNode *receiver_expr, Value obj, Value *arg_values, int arg_count, int line, int col);

struct FnShape { AstNode **params; int param_count; const char *name; int len; AstNode *return_type; AstNode *body; };
static FnShape get_fn_shape(AstNode *decl) ;