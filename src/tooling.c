static void print_indent(int depth) {
    for (int i = 0; i < depth; i++) printf("  ");
}

static void print_escaped(const char *s, int len) {
    for (int i = 0; i < len; i++) {
        char c = s[i];
        switch (c) {
            case '"':  printf("\\\""); break;
            case '\\': printf("\\\\"); break;
            case '\n': printf("\\n");  break;
            case '\t': printf("\\t");  break;
            case '\r': printf("\\r");  break;
            default:   putchar(c);     break;
        }
    }
}

void print_lexer(const std::string& source) {

    Lexer lx = { source.c_str(), source.c_str(), 1, 1 };

    for (;;) {
        Token t = next_token(&lx);

        printf("[%-10s] '%.*s'  line=%d nl=%d\n",
               token_name(t.type),
               t.len,
               t.start,
               t.line,
               t.preceded_nl);

        if (t.type == TOK_EOF)
            break;
    }
    printf("\n");
}

void print_ast(AstNode *node, int depth) {

    if (!node) {
        print_indent(depth);
        printf("(null)\n");
        return;
    }

    print_indent(depth);

    switch (node->type) {
        case INT_LIT:
            printf("IntLit %ld\n", node->int_val);
            break;

        case FLOAT_LIT:
            printf("FloatLit %f\n", node->float_val);
            break;

        case STR_LIT:
            printf("StrLit \"");
            print_escaped(node->text, node->len);
            printf("\"\n");
            break;

        case RUNE_LIT:
            printf("RuneLit '");
            print_escaped(node->text, node->len);
            printf("'\n");
            break;

        case BOOL_LIT:
            printf("BoolLit %s\n", node->bool_val ? "true" : "false");
            break;

        case NULL_LIT:
            printf("NullLit\n");
            break;

        case IDENT:
            printf("Ident %.*s\n", node->len, node->name);
            break;

        case BINARY:
            printf("Binary %s\n", token_name(node->op));
            print_ast(node->left, depth + 1);
            print_ast(node->right, depth + 1);
            break;

        case UNARY:
            printf("Unary %s\n", token_name(node->op));
            print_ast(node->operand, depth + 1);
            break;

        case POSTFIX:
            printf("Postfix %s\n", token_name(node->op));
            print_ast(node->operand, depth + 1);
            break;

        case TERNARY:
            printf("Ternary\n");
            print_ast(node->cond, depth + 1);
            print_ast(node->then_expr, depth + 1);
            print_ast(node->else_expr, depth + 1);
            break;

        case CALL:
            printf("Call\n");
            print_indent(depth + 1); printf("callee:\n");
            print_ast(node->callee, depth + 2);
            print_indent(depth + 1); printf("args:\n");
            for (int i = 0; i < node->arg_count; i++) {
                print_ast(node->args[i], depth + 2);
            }
            break;

        case INDEX:
            printf("Index\n");
            print_ast(node->object, depth + 1);
            print_indent(depth + 1); printf("indices:\n");
            for (int i = 0; i < node->index_count; i++) {
                print_ast(node->indices[i], depth + 2);
            }
            break;

        case FIELD_ACCESS:
            printf("FieldAccess .%.*s\n", node->len, node->field);
            print_ast(node->object, depth + 1);
            break;

        case ARRAY_LIT:
            printf("ArrayLit (%d items)\n", node->count);
            for (int i = 0; i < node->count; i++) {
                print_ast(node->items[i], depth + 1);
            }
            break;

        case ARRAY_ENTRY:
            printf("ArrayEntry\n");
            print_indent(depth + 1); printf("key:\n");
            print_ast(node->key, depth + 2);
            print_indent(depth + 1); printf("value:\n");
            print_ast(node->value, depth + 2);
            break;

        case STRUCT_LIT:
            printf("StructLit (%d fields)\n", node->count);
            for (int i = 0; i < node->count; i++) {
                print_ast(node->fields[i], depth + 1);
            }
            break;

        case FIELD:
            printf("StructLitField %.*s\n", node->len, node->name);
            print_ast(node->value, depth + 1);
            break;

        case IMMUT_DECL:
        case MUT_DECL:
            printf("%s %.*s\n",
                node->type == IMMUT_DECL ? "ImmutDecl" : "MutDecl",
                node->len, node->name);
            if (node->has_type_annotation) {
                print_indent(depth + 1); printf("type:\n");
                print_ast(node->type_annotation, depth + 2);
            }
            print_ast(node->value, depth + 1);
            break;

        case ASSIGN:
            printf("Assign %s\n", token_name(node->op));
            print_ast(node->target, depth + 1);
            print_ast(node->value, depth + 1);
            break;

        case EXPR_STMT:
            printf("ExprStmt\n");
            print_ast(node->expr, depth + 1);
            break;

        case IF_STMT:
            printf("If\n");
            print_indent(depth + 1); printf("cond:\n");
            print_ast(node->cond, depth + 2);
            print_indent(depth + 1); printf("then:\n");
            print_ast(node->then_branch, depth + 2);
            if (node->else_branch) {
                print_indent(depth + 1); printf("else:\n");
                print_ast(node->else_branch, depth + 2);
            }
            break;

        case BLOCK:
            printf("Block (%d stmts)\n", node->stmt_count);
            for (int i = 0; i < node->stmt_count; i++) {
                print_ast(node->stmts[i], depth + 1);
            }
            break;

        case PROGRAM:
            printf("Program (%d stmts)\n", node->stmt_count);
            for (int i = 0; i < node->stmt_count; i++) {
                print_ast(node->stmts[i], depth + 1);
            }
            break;

        case WHILE_STMT:
            printf("While\n");
            print_indent(depth + 1); printf("cond:\n");
            print_ast(node->cond, depth + 2);
            print_indent(depth + 1); printf("body:\n");
            print_ast(node->body, depth + 2);
            break;

        case SWITCH_STMT:
            printf("Switch\n");

            print_indent(depth + 1); printf("scrutinee:\n");
            print_ast(node->scrutinee, depth + 2);

            if (node->guard) {
                print_indent(depth + 1); printf("guard:\n");
                print_ast(node->guard, depth + 2);
            }

            print_indent(depth + 1); printf("cases:\n");
            for (int i = 0; i < node->case_count; i++) {
                print_ast(node->cases[i], depth + 2);
            }
            break;

        case SWITCH_CASE:
            printf("Case\n");

            print_indent(depth + 1); printf("label:\n");
            print_ast(node->label, depth + 2);

            print_indent(depth + 1); printf("body:\n");
            print_ast(node->body, depth + 2);
            break;

        case DEFAULT_STMT:
            printf("Default\n");
            break;

        case FOR_RANGE:
            printf("ForRange\n");
            print_indent(depth + 1); printf("start:\n");
            print_ast(node->start, depth + 2);
            if (node->end) {
                print_indent(depth + 1); printf("end:\n");
                print_ast(node->end, depth + 2);
            }
            print_indent(depth + 1); printf("body:\n");
            print_ast(node->body, depth + 2);
            break;

        case FOR_CSTYLE:
            printf("ForCStyle\n");
            print_indent(depth + 1); printf("init:\n");
            print_ast(node->init, depth + 2);
            print_indent(depth + 1); printf("cond:\n");
            print_ast(node->cond, depth + 2);
            print_indent(depth + 1); printf("incr:\n");
            print_ast(node->incr, depth + 2);
            print_indent(depth + 1); printf("body:\n");
            print_ast(node->body, depth + 2);
            break;

        case FOR_IN:
            printf("ForIn\n");
            print_indent(depth + 1); printf("key: '%.*s'\n",
                node->key_len, node->key_name);
            print_indent(depth + 1); printf("val: '%.*s'\n",
                node->val_len, node->val_name);
            print_indent(depth + 1); printf("iterable:\n");
            print_ast(node->iterable, depth + 2);
            print_indent(depth + 1); printf("body:\n");
            print_ast(node->body, depth + 2);
            break;

        case RETURN_STMT:
            printf("Return\n");
            if (node->value) {
                print_ast(node->value, depth + 1);
            }
            break;

        case DEFER_STMT:
            printf("Defer\n");
            if (node->value) {
                print_ast(node->value, depth + 1);
            }
            break;

        case BREAK_STMT:
            printf("Break\n");
            break;

        case CONTINUE_STMT:
            printf("Continue\n");
            break;

        case FALLTHROUGH_STMT:
            printf("Fallthrough\n");
            break;

        case FN_DECL:
            printf("FnDecl %.*s (%d params)\n",
                node->len, node->name, node->param_count);
            if (node->return_type) {
                print_indent(depth + 1); printf("returns:\n");
                print_ast(node->return_type, depth + 2);
            }
            print_indent(depth + 1); printf("params:\n");
            for (int i = 0; i < node->param_count; i++) print_ast(node->params[i], depth + 2);
            print_indent(depth + 1); printf("body:\n");
            print_ast(node->body, depth + 2);
            break;
            
        case METHOD:
            printf("Method %.*s (%d params%s)\n",
                node->len, node->name, node->param_count,
                node->is_mut ? ", mut" : "");
            if (node->return_type) {
                print_indent(depth + 1); printf("returns:\n");
                print_ast(node->return_type, depth + 2);
            }
            print_indent(depth + 1); printf("params:\n");
            for (int i = 0; i < node->param_count; i++) print_ast(node->params[i], depth + 2);
            print_indent(depth + 1); printf("body:\n");
            print_ast(node->body, depth + 2);
            break;

        case FN_LIT:
            printf("FnLit (%d params)\n", node->param_count);
            if (node->return_type) {
                print_indent(depth + 1); printf("returns:\n");
                print_ast(node->return_type, depth + 2);
            }
            print_indent(depth + 1); printf("params:\n");
            for (int i = 0; i < node->param_count; i++) {
                print_ast(node->params[i], depth + 2);
            }

            print_indent(depth + 1); printf("body:\n");
            print_ast(node->body, depth + 2);
            break;

        case PARAM:
            if (node->is_variadic) {
                printf("Param ... (variadic%s)\n", node->type_expr ? ", typed" : "");
                if (node->type_expr) {
                    print_indent(depth + 1); printf("type:\n");
                    print_ast(node->type_expr, depth + 2);
                }
                break;
            }
            printf("Param %.*s%s%s\n",
                node->len, node->name,
                node->type_expr ? " (typed)" : "",
                node->value ? " (default)" : "");
            if (node->type_expr) {
                print_indent(depth + 1); printf("type:\n");
                print_ast(node->type_expr, depth + 2);
            }
            if (node->value) {
                print_indent(depth + 1); printf("default:\n");
                print_ast(node->value, depth + 2);
            }
            break;

        case STRUCT_DECL:
            printf("StructDecl %.*s (%d fields)\n",
                node->len, node->name,
                node->field_count);
            print_indent(depth + 1); printf("fields:\n");
            for (int i = 0; i < node->field_count; i++) {
                print_ast(node->fields[i], depth + 2);
            }
            break;

        case ENUM_DECL:
            printf("EnumDecl %.*s (%d variants)\n",
                node->len, node->name,
                node->variant_count);
            print_indent(depth + 1); printf("variants:\n");
            for (int i = 0; i < node->variant_count; i++) {
                AstNode *v = node->variants[i];
                print_indent(depth + 2);
                printf("EnumVariant %.*s\n", v->len, v->name);
                if (v->value) {
                    print_indent(depth + 3); printf("value:\n");
                    print_ast(v->value, depth + 4);
                } else {
                    print_indent(depth + 3); printf("(auto-assigned)\n");
                }
            }
            break;

        case TYPE_BLOCK:
            printf("TypeBlock %s (%d decls)\n",
                token_name(node->type_tok),
                node->decl_count);
            for (int i = 0; i < node->decl_count; i++) {
                print_ast(node->decls[i], depth + 1);
            }
            break;

        case TRY_STMT:
            printf("Try\n");
            print_indent(depth + 1); printf("try:\n");
            print_ast(node->try_block, depth + 2);
            print_indent(depth + 1); printf("catch (%.*s):\n",
                node->catch_len, node->catch_name);
            print_ast(node->catch_block, depth + 2);
            break;

        case IMPORT:
            if (node->path) {
                printf("Import '%.*s'", node->path_len, node->path);
                if (node->alias) {
                    printf(" as %.*s", node->alias_len, node->alias);
                }
                printf("\n");
            }
            break;

        case PROCTIME_BLOCK:
            printf("ProctimeBlock (%d stmts)\n", node->stmt_count);
            for (int i = 0; i < node->stmt_count; i++) {
                print_ast(node->stmts[i], depth + 1);
            }
            break;

        case PRIMITIVE:
            printf("%s %.*s (%d params)\n",
                node->body ? "Phrase" : "Primitive",
                node->len, node->name, node->param_count);
            if (node->return_type) {
                print_indent(depth + 1); printf("returns:\n");
                print_ast(node->return_type, depth + 2);
            }
            print_indent(depth + 1); printf("params:\n");
            for (int i = 0; i < node->param_count; i++) print_ast(node->params[i], depth + 2);
            if (node->body) {
                print_indent(depth + 1); printf("body:\n");
                print_ast(node->body, depth + 2);
            }
            break;

        case WORD:
            printf("Word %.*s (%d args)\n", node->len, node->name, node->arg_count);
            for (int i = 0; i < node->arg_count; i++) print_ast(node->args[i], depth + 1);
            if (node->block) print_ast(node->block, depth + 1);
            break;

        case ARRAY_TYPE:
            printf("ArrayType\n");
            print_indent(depth + 1); printf("element:\n");
            print_ast(node->element_type, depth + 2);
            break;

        default:
            printf("<unhandled AstType %d>\n", (int)node->type);
            break;
    }
}