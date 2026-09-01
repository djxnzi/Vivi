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
            pt_add_dep(owner, expr->as.ident.name, expr->as.ident.len);
            break;

        case UNARY:
        case POSTFIX:
            pt_collect_refs(expr->as.unary.operand, owner);
            break;

        case BINARY:
            pt_collect_refs(expr->as.binary.left, owner);
            pt_collect_refs(expr->as.binary.right, owner);
            break;

        case TERNARY:
            pt_collect_refs(expr->as.ternary.cond, owner);
            pt_collect_refs(expr->as.ternary.then_expr, owner);
            pt_collect_refs(expr->as.ternary.else_expr, owner);
            break;

        case CALL:
            pt_collect_refs(expr->as.call.callee, owner);
            for (int i = 0; i < expr->as.call.arg_count; i++)
                pt_collect_refs(expr->as.call.args[i], owner);
            break;

        case ARRAY_LIT:
            for (int i = 0; i < expr->as.array_lit.count; i++) {
                AstNode *item = expr->as.array_lit.items[i];
                if (item->type == ARRAY_ENTRY) {
                    pt_collect_refs(item->as.array_entry.key, owner);
                    pt_collect_refs(item->as.array_entry.value, owner);
                } else {
                    pt_collect_refs(item, owner);
                }
            }
            break;

        case INDEX:
            pt_collect_refs(expr->as.index.object, owner);
            for (int i = 0; i < expr->as.index.index_count; i++)
                pt_collect_refs(expr->as.index.indices[i], owner);
            break;

        case FIELD_ACCESS:
            pt_collect_refs(expr->as.field_access.object, owner);
            break;

        case STRUCT_LIT:
            for (int i = 0; i < expr->as.struct_lit.count; i++)
                pt_collect_refs(expr->as.struct_lit.fields[i]->as.field.value, owner);
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
    for (int v = 0; v < enum_decl->as.enum_decl.variant_count; v++) {
        AstNode *variant = enum_decl->as.enum_decl.variants[v];
        if (variant->as.field.value) pt_collect_refs(variant->as.field.value, node);
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