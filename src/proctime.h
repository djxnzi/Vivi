struct ProcDep {
    const char *name;
    int len;
};

struct ProcNode {
    AstNode *decl;
    const char *name;
    int name_len;
    ProcDep *deps;
    int dep_count, dep_capacity;
    bool visiting;
    bool resolved;
};

static ProcNode *pt_nodes = nullptr;
static Env *pt_bleed_env = nullptr;
static Env *pt_check_env = nullptr;
static int pt_check_method_depth = 0;
static int pt_node_count = 0, pt_node_capacity = 0;

// Graph
static void pt_add_node(const char *name, int len, AstNode *decl, AstNode *init_expr);
static void pt_add_dep(ProcNode *owner, const char *name, int len);
static void pt_collect_refs(AstNode *expr, ProcNode *owner);
static ProcNode* pt_find_node(const char *name, int len);
static bool pt_visit(ProcNode *node, AstNode **order, int *order_count);
static bool pt_toposort(AstNode ***out_order, int *out_count);

// Proctime
static void pt_declare_name(const char *name, int len, bool bleeds = false);
static bool pt_name_resolves(const char *name, int len);
static void pt_check_expr(AstNode *e);
static void pt_walk_stmt(AstNode *stmt, AstNode *owner_try, bool checking);
static void pt_check_structure(AstNode *program);
static void pt_add_enum_node(const char *name, int len, AstNode *enum_decl, AstNode *decl);
static void run_proctime(AstNode *program);