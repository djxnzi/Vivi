enum AstType {
    // Lits
    INT_LIT, FLOAT_LIT, STR_LIT, RUNE_LIT, BOOL_LIT, NULL_LIT, IDENT,

    // Collection lits
    ARRAY_LIT, ARRAY_ENTRY, STRUCT_LIT,

    // Expr
    BINARY, UNARY, POSTFIX, TERNARY, CALL, INDEX,
    FIELD_ACCESS, FN_LIT,

    // Decl
    IMMUT_DECL, MUT_DECL, FN_DECL, STRUCT_DECL, ENUM_DECL,
    PARAM, FIELD, DESTRUCTURE_DECL,

    // Statements
    EXPR_STMT, ASSIGN, BLOCK, IF_STMT, WHILE_STMT,
    FOR_RANGE, FOR_CSTYLE, FOR_IN,
    SWITCH_STMT, SWITCH_CASE, DEFAULT_STMT,
    BREAK_STMT, CONTINUE_STMT, RETURN_STMT, FALLTHROUGH_STMT,
    DEFER_STMT, TRY_STMT,

    // Types
    TYPE_BLOCK, ARRAY_TYPE, RETURN_TYPE_LIST,

    // Proctime
    PROCTIME_BLOCK,

    // DIRECTIVES
    IMPORT, PRIMITIVE, METHOD, WORD,

    // Root
    PROGRAM
};

enum ValueType {
    Null, Bool, Int, Float, Str, Rune,
    Array, Struct, Fn, Frame, Phrase
};

struct AstNode {
    AstType type;
    int line, col;
    bool is_proctime;       // :: proctime sorting for order independent decl's
    bool is_local;
    bool is_static;
    void *static_slot;      // lazy alloc for statics in ObjVar

    // Literals
    long int_val;
    double float_val;
    bool bool_val;
    const char *text;

    const char *name;
    int len;

    // Collection lits
    AstNode **items;
    int count;
    AstNode **fields;
    int field_count;
    AstNode *key;

    AstNode *type_expr;
    AstNode *value;
    bool is_variadic;

    // Decl
    TokenType type_tok;
    AstNode **decls;
    int decl_count;
    AstNode *element_type;
    bool has_type_annotation;
    AstNode *type_annotation;
    AstNode *type_size;
    bool is_unsized_array;
    AstNode **targets;
    int target_count;
    bool is_immutable;
    AstNode **types;

    // Expr
    TokenType op;
    AstNode *left, *right;
    AstNode *operand;
    AstNode *cond;
    AstNode *then_expr, *else_expr;
    AstNode *callee;
    AstNode **args;
    int arg_count;
    AstNode *object;
    AstNode **indices;
    int index_count;
    const char *field;

    AstNode **params;
    int param_count;
    AstNode *return_type;
    AstNode *body;
    bool is_mut;

    AstNode **variants;
    int variant_count;

    // Stmt
    AstNode *target;
    bool is_method;
    AstNode **stmts;
    int stmt_count;
    AstNode *then_branch, *else_branch;
    AstNode *start, *end;
    AstNode *init, *incr;
    const char *key_name;
    int key_len;
    const char *val_name;
    int val_len;
    AstNode *iterable;
    AstNode *try_block;
    const char *catch_name;
    int catch_len;
    AstNode *catch_block;
    AstNode *scrutinee;
    AstNode *guard;
    AstNode **cases;
    int case_count;
    AstNode *label;
    AstNode *expr;

    // Meta
    const char *path;
    int path_len;
    const char *alias;
    int alias_len;
    void *fn_obj;
    void *resolved;
    const char *file;
    AstNode *block;
};

struct Obj {
    ValueType type;
    int refcount;
};

struct Value {
    ValueType type;
    union {
        bool b;
        long i;
        double f;
        Obj *obj;
    } as;
};

struct EnvEntry {
    const char *name;
    int len;
    Value value;
    bool is_const;
    bool is_local;
    void *static_box;
    bool proctime_owned;
    AstNode *type_annotation;
};

struct Env {
    EnvEntry *entries;
    Env *parent;
    Env *caller;
    AstNode **defers;
    Value self_binding;
    int count, capacity;
    bool has_self;
    int defer_count, defer_capacity;
    int call_line, call_col;
    const char *call_name; int call_name_len;
    int loop_floor;
};

struct StructField {
    const char *name;
    int len;
    Value value;
};

struct ObjArray {
    Obj obj;
    Value *keys;
    Value *items;
    int count, capacity;
};

struct ObjStruct {
    Obj obj;
    const char *type_name;
    int type_name_len;
    StructField *fields;
    int count, capacity;
};

struct ObjFn {
    Obj obj;
    AstNode *decl;
    Env *closure;
    const char *file;
    const char *source;
    bool is_native;
    const char *native_name;
    int native_name_len;
};

struct ObjPhrase {
    Obj obj;
    AstNode *node;
};

struct ObjFrame {
    Obj obj;
    Env *frame;
    Value result;
};

struct ObjStr {
    Obj obj;
    const char *chars;
    int len;
};

struct ObjVar {
    Obj obj;
    Value value;
};
