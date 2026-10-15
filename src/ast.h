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
    IMPORT, PRIMITIVE, METHOD,

    // Root
    PROGRAM
};

enum ValueType {
    Null, Bool, Int, Float, Str, Rune,
    Array, Struct, Fn, Frame
};

struct AstNode {
    AstType type;
    int line, col;
    bool is_proctime;       // :: proctime sorting for order independent decl's
    bool is_local;
    bool is_static;
    void *static_slot;      // lazy alloc for statics in ObjVar

    union {

        // Literals
        struct {
            long value;
        } int_lit;
        struct {
            double value;
        } float_lit;
        struct {
            const char *start;
            int len;
        } str_lit;
        struct {
            const char *start;
            int len;
        } rune_lit;
        struct {
            bool value;
        } bool_lit;
        struct {
            struct AstNode **items;
            int count;
        } array_lit;
        struct {
            struct AstNode **fields;
            int count;
        } struct_lit;
        struct {
            const char *name;
            int len;
            struct AstNode *type_expr;
            struct AstNode *value;
            bool is_variadic;
        } field;
        struct {
            struct AstNode *key;
            struct AstNode *value;
        } array_entry;
        struct {
            const char *name;
            int len;
        } ident;

        // Decl
        struct {
            TokenType type_annotation;
            AstNode **decls;
            int decl_count;
        } type_block;
        struct {
            struct AstNode *element_type;
        } array_type;
        struct {
            const char *name;
            int len;
            bool has_type_annotation;
            struct AstNode *type_annotation;
            struct AstNode *type_size;
            bool is_unsized_array;
            struct AstNode *value;
        } decl;
        struct {
            struct AstNode **targets;
            int target_count;
            bool is_immutable;
            struct AstNode *value;
        } destructure_decl;
        struct {
            struct AstNode **types;
            int count;
        } return_type_list;

        // Expr
        struct { // left/right for comparison ops, arithmetic & string concat
            TokenType op;
            struct AstNode *left, *right;
        } binary;
        struct { // !/-
            TokenType op;
            struct AstNode *operand;
        } unary;
        struct { // cond ? then : else + if as expr
            struct AstNode *cond, *then_expr, *else_expr;
        } ternary;
        struct { // call ident
            struct AstNode *callee;
            struct AstNode **args;
            int arg_count;
        } call;
        struct { // array indices [x]
            struct AstNode *object;
            struct AstNode **indices;
            int index_count;
        } index;
        struct { // .accessor chain
            struct AstNode *object;
            const char *field;
            int len;
        } field_access;
        struct { // fn name :: {...} & name := fn {...}
            const char *name;
            int len;
            struct AstNode **params;
            int param_count;
            AstNode *return_type;
            struct AstNode *body;
        } fn_decl;
        struct { // struct name {...}
            const char *name;
            int len;
            struct AstNode **fields;
            int field_count;
        } struct_decl;
        struct { // enum name { a, b, c }
            const char *name;
            int len;
            struct AstNode **variants;
            int variant_count;
        } enum_decl;

        // Stmt
        struct { // =
            struct AstNode *target;
            TokenType op;
            struct AstNode *value;
            bool is_method;
        } assign;
        struct { // { ... }
            struct AstNode **stmts;
            int stmt_count;
        } block;
        struct {
            struct AstNode *cond;
            struct AstNode *then_branch;
            struct AstNode *else_branch;
        } if_stmt;
        struct {
            struct AstNode *cond;
            struct AstNode *body;
        } while_stmt;
        struct {
            struct AstNode *start, *end;
            struct AstNode *body;
        } for_range;
        struct {
            struct AstNode *init, *cond, *incr;
            struct AstNode *body;
        } for_cstyle;
        struct {
            const char *key_name;
            int key_len;
            const char *val_name;
            int val_len;
            struct AstNode *iterable;
            struct AstNode *body;
        } for_in;
        struct {
            AstNode *try_block;
            const char *catch_name;
            int catch_len;
            AstNode *catch_block;
        } try_stmt;
        struct {
            struct AstNode *scrutinee;
            struct AstNode *guard;
            struct AstNode **cases;
            int case_count;
        } switch_stmt;
        struct {
            struct AstNode *label;
            struct AstNode *body;
        } switch_case;
        struct {
            struct AstNode *value;
        } return_stmt;
        struct { // bare call ie print()
            struct AstNode *expr;
        } expr_stmt;
        struct {
            struct AstNode *value;
        } defer_stmt;

        // Meta
        struct { // #import
            const char *path;
            int path_len;
            const char *alias;
            int alias_len;
        } import;
        struct { // #primitive
            const char *name;
            int len;
            struct AstNode **params;
            int param_count;
            AstNode *return_type;
            void *resolved;
            const char *file;
        } primitive;
        struct { // #proctime
            struct AstNode **stmts;
            int stmt_count;
        } proctime_block;
        struct { // #method fn name :: () {...}, #method name := fn () {}
            const char *name;
            int len;
            struct AstNode **params;
            int param_count;
            AstNode *return_type;
            struct AstNode *body;
            bool is_mut;
        } method;


        struct { // interpreter meta
            struct AstNode **stmts;
            int stmt_count;
        } program;
    } as;
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