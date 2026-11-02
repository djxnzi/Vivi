struct Enum {
    AstNode *decl;
    Value *variant_values;
};

struct NewKeyword { const char *name; int len; };
static NewKeyword *new_keywords = nullptr;
static int new_keyword_count = 0, new_keyword_capacity = 0;
static bool is_keyword(const char *name, int len);
static void register_keyword(const char *name, int len);
static void find_keywords(const char *src);

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