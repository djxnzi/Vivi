static void pt_add_dep(ProcNode *owner, const char *name, int len) {
    owner->deps = (ProcDep*)grow_array(owner->deps, owner->dep_count, &owner->dep_capacity, sizeof(ProcDep), 4);
    owner->deps[owner->dep_count].name = name;
    owner->deps[owner->dep_count].len = len;
    owner->dep_count++;
}

static void pt_collect_refs(AstNode *expr, ProcNode *owner) {
    if (!expr) return;
    switch (expr->type) {
        case INT_LIT: case FLOAT_LIT: case BOOL_LIT: case NULL_LIT:
        case STR_LIT: case RUNE_LIT: case FN_LIT:
            break;

        case IDENT:
            pt_add_dep(owner, expr->name, expr->len);
            break;

        case UNARY:
        case POSTFIX:
            pt_collect_refs(expr->operand, owner);
            break;

        case BINARY:
            pt_collect_refs(expr->left, owner);
            pt_collect_refs(expr->right, owner);
            break;

        case TERNARY:
            pt_collect_refs(expr->cond, owner);
            pt_collect_refs(expr->then_expr, owner);
            pt_collect_refs(expr->else_expr, owner);
            break;

        case CALL:
            pt_collect_refs(expr->callee, owner);
            for (int i = 0; i < expr->arg_count; i++)
                pt_collect_refs(expr->args[i], owner);
            break;

        case ARRAY_LIT:
            for (int i = 0; i < expr->count; i++) {
                AstNode *item = expr->items[i];
                if (item->type == ARRAY_ENTRY) {
                    pt_collect_refs(item->key, owner);
                    pt_collect_refs(item->value, owner);
                } else {
                    pt_collect_refs(item, owner);
                }
            }
            break;

        case INDEX:
            pt_collect_refs(expr->object, owner);
            for (int i = 0; i < expr->index_count; i++)
                pt_collect_refs(expr->indices[i], owner);
            break;

        case FIELD_ACCESS:
            pt_collect_refs(expr->object, owner);
            break;

        case STRUCT_LIT:
            for (int i = 0; i < expr->count; i++)
                pt_collect_refs(expr->fields[i]->value, owner);
            break;

        default:
            break;
    }
}

static void pt_add_node(const char *name, int len, AstNode *decl, AstNode *init_expr) {
    pt_nodes = (ProcNode*)grow_array(pt_nodes, pt_node_count, &pt_node_capacity, sizeof(ProcNode), 8);
    ProcNode *node = &pt_nodes[pt_node_count++];
    node->decl = decl;
    node->name = name;
    node->name_len = len;
    node->deps = nullptr;
    node->dep_count = node->dep_capacity = 0;
    node->visiting = false;
    node->resolved = false;
    pt_collect_refs(init_expr, node);
}

static void pt_add_enum_node(const char *name, int len, AstNode *enum_decl, AstNode *decl) {
    pt_nodes = (ProcNode*)grow_array(pt_nodes, pt_node_count, &pt_node_capacity, sizeof(ProcNode), 8);
    ProcNode *node = &pt_nodes[pt_node_count++];
    node->decl = decl;
    node->name = name;
    node->name_len = len;
    node->deps = nullptr;
    node->dep_count = node->dep_capacity = 0;
    node->visiting = false;
    node->resolved = false;
    for (int v = 0; v < enum_decl->variant_count; v++) {
        AstNode *variant = enum_decl->variants[v];
        if (variant->value) pt_collect_refs(variant->value, node);
    }
}

static ProcNode* pt_find_node(const char *name, int len) {
    for (int i = 0; i < pt_node_count; i++) {
        if (pt_nodes[i].name_len == len && strncmp(pt_nodes[i].name, name, len) == 0)
            return &pt_nodes[i];
    }
    return nullptr;
}

static bool pt_visit(ProcNode *node, AstNode **order, int *order_count) {
    if (node->resolved) return true;
    for (int i = 0; i < *order_count; i++)
        if (order[i] == node->decl) return true;
    if (node->visiting) {
        print_err("proctime_cycle", node->name_len, node->name);
        return false;
    }
    node->visiting = true;
    for (int i = 0; i < node->dep_count; i++) {
        ProcNode *dep = pt_find_node(node->deps[i].name, node->deps[i].len);
        if (dep && !pt_visit(dep, order, order_count)) return false;
    }
    node->visiting = false;
    node->resolved = true;
    order[(*order_count)++] = node->decl;
    return true;
}

static bool pt_toposort(AstNode ***out_order, int *out_count) {
    AstNode **order = (AstNode**)arena_alloc(sizeof(AstNode*) * pt_node_count);
    int order_count = 0;
    for (int i = 0; i < pt_node_count; i++) {
        if (!pt_visit(&pt_nodes[i], order, &order_count)) return false;
    }
    *out_order = order;
    *out_count = order_count;
    return true;
}