struct Enum {
    AstNode *decl;
    Value *variant_values;
};

static void register_primitive(AstNode *decl);
static AstNode* find_primitive(const char *name, int len);
static Value call_primitive(AstNode *decl, Value *args, int arg_count);
static void* resolve_axiom(AstNode *decl);
static AstNode **primitive_decls = nullptr;
static int primitive_decl_count = 0, primitive_decl_capacity = 0;

static void register_method(const char *name, int len);
static void unregister_method(const char *name, int len);
static ObjFn* find_method(const char *name, int len);
struct MethodName { const char *name; int len; };
static MethodName *method_decls = nullptr;
static int method_decl_count = 0, method_decl_capacity = 0;

static void register_struct_decl(AstNode *decl);
static AstNode* find_struct_decl(const char *name, int len);
static bool label_matches(AstNode *label, Value scrutinee);
static AstNode **struct_decls = nullptr;
static int struct_decl_count = 0, struct_decl_capacity = 0;

static void register_enum_decl(AstNode *decl);
static Enum* find_enum_decl(const char *name, int len);
static Enum *enum_decls = nullptr;
static int enum_decl_count = 0, enum_decl_capacity = 0;