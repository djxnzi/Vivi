// some ast factory bullshit all in an incoherent order

struct AstList {
    AstNode **items;
    int count;
    int capacity;
};

static AstNode* ast_alloc(AstType type, int line, int col) {
    AstNode *n = (AstNode*)arena_alloc(sizeof(AstNode));
    memset(n, 0, sizeof(AstNode));
    n->type = type;
    n->line = line;
    n->col = col;
    n->is_proctime = false;
    return n;
}

static void ast_list_init(AstList *list) {
    list->items = nullptr;
    list->count = 0;
    list->capacity = 0;
}

static void ast_list_push(AstList *list, AstNode *node) {
    if (list->count == list->capacity) {
        int new_cap = list->capacity == 0 ? 4 : list->capacity * 2;
        AstNode **new_items = (AstNode**)malloc(sizeof(AstNode*) * new_cap);
        if (list->items) {
            memcpy(new_items, list->items, sizeof(AstNode*) * list->count);
            free(list->items);
        }
        list->items = new_items;
        list->capacity = new_cap;
    }
    list->items[list->count++] = node;
}

static AstNode** ast_list_finish(AstList *list, int *out_count) {
    *out_count = list->count;
    if (list->count == 0) {
        free(list->items);
        return nullptr;
    }
    AstNode **arena_copy = (AstNode**)arena_alloc(sizeof(AstNode*) * list->count);
    memcpy(arena_copy, list->items, sizeof(AstNode*) * list->count);
    free(list->items);
    return arena_copy;
}

// Grammar

AstNode* make_int_lit(long value, int line, int col) {
    AstNode *n = ast_alloc(INT_LIT, line, col);
    n->as.int_lit.value = value;
    return n;
}

AstNode* make_float_lit(double value, int line, int col) {
    AstNode *n = ast_alloc(FLOAT_LIT, line, col);
    n->as.float_lit.value = value;
    return n;
}

AstNode* make_str_lit(const char *start, int len, int line, int col) {
    AstNode *n = ast_alloc(STR_LIT, line, col);
    n->as.str_lit.start = start;
    n->as.str_lit.len = len;
    return n;
}

AstNode* make_rune_lit(const char *start, int len, int line, int col) {
    AstNode *n = ast_alloc(RUNE_LIT, line, col);
    n->as.rune_lit.start = start;
    n->as.rune_lit.len = len;
    return n;
}

AstNode* make_bool_lit(bool value, int line, int col) {
    AstNode *n = ast_alloc(BOOL_LIT, line, col);
    n->as.bool_lit.value = value;
    return n;
}

AstNode* make_null_lit(int line, int col) {
    return ast_alloc(NULL_LIT, line, col);
}

AstNode* make_ident(const char *name, int len, int line, int col) {
    AstNode *n = ast_alloc(IDENT, line, col);
    n->as.ident.name = name;
    n->as.ident.len = len;
    return n;
}

AstNode* make_binary(TokenType op, AstNode *left, AstNode *right, int line, int col) {
    AstNode *n = ast_alloc(BINARY, line, col);
    n->as.binary.op = op;
    n->as.binary.left = left;
    n->as.binary.right = right;
    return n;
}

AstNode* make_unary(TokenType op, AstNode *operand, int line, int col) {
    AstNode *n = ast_alloc(UNARY, line, col);
    n->as.unary.op = op;
    n->as.unary.operand = operand;
    return n;
}

AstNode* make_postfix(TokenType op, AstNode *operand, int line, int col) {
    AstNode *n = ast_alloc(POSTFIX, line, col);
    n->as.unary.op = op;
    n->as.unary.operand = operand;
    return n;
}

AstNode* make_ternary(AstNode *cond, AstNode *then_expr, AstNode *else_expr, int line, int col) {
    AstNode *n = ast_alloc(TERNARY, line, col);
    n->as.ternary.cond = cond;
    n->as.ternary.then_expr = then_expr;
    n->as.ternary.else_expr = else_expr;
    return n;
}

AstNode* make_call(AstNode *callee, AstList *args, int line, int col) {
    AstNode *n = ast_alloc(CALL, line, col);
    n->as.call.callee = callee;
    n->as.call.args = ast_list_finish(args, &n->as.call.arg_count);
    return n;
}

AstNode* make_index(AstNode *object, AstNode **indices, int index_count, int line, int col) {
    AstNode *n = ast_alloc(INDEX, line, col);
    n->as.index.object = object;
    n->as.index.indices = indices;
    n->as.index.index_count = index_count;
    return n;
}

AstNode* make_field(const char *name, int len, AstNode *type_expr, AstNode *value, int line, int col) {
    AstNode *n = ast_alloc(FIELD, line, col);
    n->as.field.name = name;
    n->as.field.len = len;
    n->as.field.type_expr = type_expr;
    n->as.field.value = value;
    return n;
}

AstNode* make_field_access(AstNode *object, const char *field, int len, int line, int col) {
    AstNode *n = ast_alloc(FIELD_ACCESS, line, col);
    n->as.field_access.object = object;
    n->as.field_access.field = field;
    n->as.field_access.len = len;
    return n;
}

AstNode* make_block(AstList *stmts, int line, int col) {
    AstNode *n = ast_alloc(BLOCK, line, col);
    n->as.block.stmts = ast_list_finish(stmts, &n->as.block.stmt_count);
    return n;
}

AstNode* make_if_stmt(AstNode *cond, AstNode *then_branch, AstNode *else_branch, int line, int col) {
    AstNode *n = ast_alloc(IF_STMT, line, col);
    n->as.if_stmt.cond = cond;
    n->as.if_stmt.then_branch = then_branch;
    n->as.if_stmt.else_branch = else_branch;
    return n;
}

AstNode* make_while_stmt(AstNode *cond, AstNode *body, int line, int col) {
    AstNode *n = ast_alloc(WHILE_STMT, line, col);
    n->as.while_stmt.cond = cond;
    n->as.while_stmt.body = body;
    return n;
}

AstNode* make_switch_stmt(AstNode *scrutinee, AstNode *guard, AstList *cases, int line, int col) {
    AstNode *n = ast_alloc(SWITCH_STMT, line, col);
    n->as.switch_stmt.scrutinee = scrutinee;
    n->as.switch_stmt.guard = guard;
    n->as.switch_stmt.cases = ast_list_finish(cases, &n->as.switch_stmt.case_count);
    return n;
}

AstNode* make_switch_case(AstNode *label, AstNode *body, int line, int col) {
    AstNode *n = ast_alloc(SWITCH_CASE, line, col);
    n->as.switch_case.label = label;
    n->as.switch_case.body = body;
    return n;
}

AstNode* make_for_range(AstNode *start, AstNode *end, AstNode *body, int line, int col) {
    AstNode *n = ast_alloc(FOR_RANGE, line, col);
    n->as.for_range.start = start;
    n->as.for_range.end = end;
    n->as.for_range.body = body;
    return n;
}

AstNode* make_for_cstyle(AstNode *init, AstNode *cond, AstNode *incr, AstNode *body, int line, int col) {
    AstNode *n = ast_alloc(FOR_CSTYLE, line, col);
    n->as.for_cstyle.init = init;
    n->as.for_cstyle.cond = cond;
    n->as.for_cstyle.incr = incr;
    n->as.for_cstyle.body = body;
    return n;
}

AstNode* make_for_in(const char *key_name, int key_len, const char *val_name, int val_len, AstNode *iterable, AstNode *body, int line, int col) {
    AstNode *n = ast_alloc(FOR_IN, line, col);
    n->as.for_in.key_name = key_name;
    n->as.for_in.key_len  = key_len;
    n->as.for_in.val_name = val_name;
    n->as.for_in.val_len  = val_len;
    n->as.for_in.iterable = iterable;
    n->as.for_in.body     = body;
    return n;
}

AstNode* make_return_stmt(AstNode *value, int line, int col) {
    AstNode *n = ast_alloc(RETURN_STMT, line, col);
    n->as.return_stmt.value = value;
    return n;
}

AstNode* make_defer_stmt(AstNode *value, int line, int col) {
    AstNode *n = ast_alloc(DEFER_STMT, line, col);
    n->as.defer_stmt.value = value;
    return n;
}

AstNode* make_default_stmt(int line, int col) {
    AstNode *n = ast_alloc(DEFAULT_STMT, line, col);
    return n;
}

AstNode* make_break_stmt(int line, int col) {
    AstNode *n = ast_alloc(BREAK_STMT, line, col);
    return n;
}

AstNode* make_continue_stmt(int line, int col) {
    AstNode *n = ast_alloc(CONTINUE_STMT, line, col);
    return n;
}

AstNode* make_fallthrough_stmt(int line, int col) {
    AstNode *n = ast_alloc(FALLTHROUGH_STMT, line, col);
    return n;
}

AstNode* make_assign(AstNode *target, TokenType op, AstNode *value, int line, int col) {
    AstNode *n = ast_alloc(ASSIGN, line, col);
    n->as.assign.target = target;
    n->as.assign.op = op;
    n->as.assign.value = value;
    return n;
}

AstNode* make_program(AstList *stmts, int line, int col) {
    AstNode *n = ast_alloc(PROGRAM, line, col);
    n->as.program.stmts = ast_list_finish(stmts, &n->as.program.stmt_count);
    return n;
}

AstNode* make_expr_stmt(AstNode *expr, int line, int col) {
    AstNode *n = ast_alloc(EXPR_STMT, line, col);
    n->as.expr_stmt.expr = expr;
    return n;
}

AstNode* make_array_lit(AstNode **items, int count, int line, int col) {
    AstNode *n = ast_alloc(ARRAY_LIT, line, col);
    n->as.array_lit.items = items;
    n->as.array_lit.count = count;
    return n;
}

AstNode* make_array_entry(AstNode *key, AstNode *value, int line, int col) {
    AstNode *n = ast_alloc(ARRAY_ENTRY, line, col);
    n->as.array_entry.key = key;
    n->as.array_entry.value = value;
    return n;
}

AstNode* make_struct_lit(AstNode **fields, int count, int line, int col) {
    AstNode *n = ast_alloc(STRUCT_LIT, line, col);
    n->as.struct_lit.fields = fields;
    n->as.struct_lit.count = count;
    return n;
}

AstNode* make_immut_decl(const char *name, int len, bool has_type_annotation, AstNode *type_annotation, AstNode *value, int line, int col) {
    AstNode *n = ast_alloc(IMMUT_DECL, line, col);
    n->as.decl.name = name;
    n->as.decl.len = len;
    n->as.decl.has_type_annotation = has_type_annotation;
    n->as.decl.type_annotation = type_annotation;
    n->as.decl.value = value;
    return n;
}

AstNode* make_mut_decl(const char *name, int len, bool has_type_annotation, AstNode *type_annotation, AstNode *value, int line, int col) {
    AstNode *n = ast_alloc(MUT_DECL, line, col);
    n->as.decl.name = name;
    n->as.decl.len = len;
    n->as.decl.has_type_annotation = has_type_annotation;
    n->as.decl.type_annotation = type_annotation;
    n->as.decl.value = value;
    return n;
}

AstNode* make_param(const char *name, int len, AstNode *type_expr, AstNode *default_value, int line, int col) {
    AstNode *n = ast_alloc(PARAM, line, col);
    n->as.field.name = name;
    n->as.field.len = len;
    n->as.field.type_expr = type_expr;
    n->as.field.value = default_value;
    return n;
}

AstNode* make_fn_lit(AstNode **params, int param_count, AstNode *return_type, AstNode *body, int line, int col) {
    AstNode *n = ast_alloc(FN_LIT, line, col);
    n->as.fn_decl.name = nullptr;
    n->as.fn_decl.len = 0;
    n->as.fn_decl.params = params;
    n->as.fn_decl.param_count = param_count;
    n->as.fn_decl.return_type = return_type;
    n->as.fn_decl.body = body;
    return n;
}

AstNode* make_fn_decl(const char *name, int len, AstNode **params, int param_count, AstNode *return_type, AstNode *body, int line, int col) {
    AstNode *n = ast_alloc(FN_DECL, line, col);
    n->as.fn_decl.name = name;
    n->as.fn_decl.len = len;
    n->as.fn_decl.params = params;
    n->as.fn_decl.param_count = param_count;
    n->as.fn_decl.return_type = return_type;
    n->as.fn_decl.body = body;
    return n;
}

AstNode* make_struct_decl(const char *name, int len, AstNode **fields, int field_count, int line, int col) {
    AstNode *n = ast_alloc(STRUCT_DECL, line, col);
    n->as.struct_decl.name = name;
    n->as.struct_decl.len = len;
    n->as.struct_decl.fields = fields;
    n->as.struct_decl.field_count = field_count;
    return n;
}

AstNode* make_enum_decl(const char *name, int len, AstNode **variants, int variant_count, int line, int col) {
    AstNode *n = ast_alloc(ENUM_DECL, line, col);
    n->as.enum_decl.name = name;
    n->as.enum_decl.len = len;
    n->as.enum_decl.variants = variants;
    n->as.enum_decl.variant_count = variant_count;
    return n;
}

AstNode* make_try_stmt(AstNode *try_block, const char *catch_name, int catch_len, AstNode *catch_block, int line, int col) {
    AstNode *n = ast_alloc(TRY_STMT, line, col);
    n->as.try_stmt.try_block = try_block;
    n->as.try_stmt.catch_name = catch_name;
    n->as.try_stmt.catch_len = catch_len;
    n->as.try_stmt.catch_block = catch_block;
    return n;
}

AstNode* make_type_block(TokenType type_annotation, AstNode **decls, int decl_count, int line, int col) {
    AstNode *n = ast_alloc(TYPE_BLOCK, line, col);
    n->as.type_block.type_annotation = type_annotation;
    n->as.type_block.decls = decls;
    n->as.type_block.decl_count = decl_count;
    return n;
}

AstNode* make_array_type(AstNode *element_type, int line, int col) {
    AstNode *n = ast_alloc(ARRAY_TYPE, line, col);
    n->as.array_type.element_type = element_type;
    return n;
}

AstNode* make_destructure_decl(AstNode **targets, int target_count, bool is_immutable, AstNode *value, int line, int col) {
    AstNode *n = ast_alloc(DESTRUCTURE_DECL, line, col);
    n->as.destructure_decl.targets = targets;
    n->as.destructure_decl.target_count = target_count;
    n->as.destructure_decl.is_immutable = is_immutable;
    n->as.destructure_decl.value = value;
    return n;
}

AstNode* make_return_type_list(AstNode **types, int count, int line, int col) {
    AstNode *n = ast_alloc(RETURN_TYPE_LIST, line, col);
    n->as.return_type_list.types = types;
    n->as.return_type_list.count = count;
    return n;
}

// Directives

AstNode* make_import(const char *path, int path_len, const char *alias, int alias_len, int line, int col) {
    AstNode *n = ast_alloc(IMPORT, line, col);
    n->as.import.path = path;
    n->as.import.path_len = path_len;
    n->as.import.alias = alias;
    n->as.import.alias_len = alias_len;
    return n;
}

AstNode* make_primitive(const char *name, int len, AstNode **params, int param_count, AstNode *return_type, int line, int col) {
    AstNode *n = ast_alloc(PRIMITIVE, line, col);
    n->as.primitive.name = name;
    n->as.primitive.len = len;
    n->as.primitive.params = params;
    n->as.primitive.param_count = param_count;
    n->as.primitive.return_type = return_type;
    n->as.primitive.file = current_file;
    return n;
}

AstNode* make_proctime_block(AstList *stmts, int line, int col) {
    AstNode *n = ast_alloc(PROCTIME_BLOCK, line, col);
    n->as.proctime_block.stmts = ast_list_finish(stmts, &n->as.proctime_block.stmt_count);
    return n;
}

AstNode* make_method(const char *name, int len, AstNode **params, int param_count, AstNode *return_type, AstNode *body, int line, int col) {
    AstNode *n = ast_alloc(METHOD, line, col);
    n->as.method.name = name;
    n->as.method.len = len;
    n->as.method.params = params;
    n->as.method.param_count = param_count;
    n->as.method.return_type = return_type;
    n->as.method.body = body;
    return n;
}

// AstNode* make_interop() {}

// AstNode* make_inline()  {}