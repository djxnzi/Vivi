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
            printf("IntLit %ld\n", node->as.int_lit.value);
            break;

        case FLOAT_LIT:
            printf("FloatLit %f\n", node->as.float_lit.value);
            break;

        case STR_LIT:
            printf("StrLit \"");
            print_escaped(node->as.str_lit.start, node->as.str_lit.len);
            printf("\"\n");
            break;

        case RUNE_LIT:
            printf("RuneLit '");
            print_escaped(node->as.rune_lit.start, node->as.rune_lit.len);
            printf("'\n");
            break;

        case BOOL_LIT:
            printf("BoolLit %s\n", node->as.bool_lit.value ? "true" : "false");
            break;

        case NULL_LIT:
            printf("NullLit\n");
            break;

        case IDENT:
            printf("Ident %.*s\n", node->as.ident.len, node->as.ident.name);
            break;

        case BINARY:
            printf("Binary %s\n", token_name(node->as.binary.op));
            print_ast(node->as.binary.left, depth + 1);
            print_ast(node->as.binary.right, depth + 1);
            break;

        case UNARY:
            printf("Unary %s\n", token_name(node->as.unary.op));
            print_ast(node->as.unary.operand, depth + 1);
            break;

        case POSTFIX:
            printf("Postfix %s\n", token_name(node->as.unary.op));
            print_ast(node->as.unary.operand, depth + 1);
            break;

        case TERNARY:
            printf("Ternary\n");
            print_ast(node->as.ternary.cond, depth + 1);
            print_ast(node->as.ternary.then_expr, depth + 1);
            print_ast(node->as.ternary.else_expr, depth + 1);
            break;

        case CALL:
            printf("Call\n");
            print_indent(depth + 1); printf("callee:\n");
            print_ast(node->as.call.callee, depth + 2);
            print_indent(depth + 1); printf("args:\n");
            for (int i = 0; i < node->as.call.arg_count; i++) {
                print_ast(node->as.call.args[i], depth + 2);
            }
            break;

        case INDEX:
            printf("Index\n");
            print_ast(node->as.index.object, depth + 1);
            print_indent(depth + 1); printf("indices:\n");
            for (int i = 0; i < node->as.index.index_count; i++) {
                print_ast(node->as.index.indices[i], depth + 2);
            }
            break;

        case FIELD_ACCESS:
            printf("FieldAccess .%.*s\n", node->as.field_access.len, node->as.field_access.field);
            print_ast(node->as.field_access.object, depth + 1);
            break;

        case ARRAY_LIT:
            printf("ArrayLit (%d items)\n", node->as.array_lit.count);
            for (int i = 0; i < node->as.array_lit.count; i++) {
                print_ast(node->as.array_lit.items[i], depth + 1);
            }
            break;

        case ARRAY_ENTRY:
            printf("ArrayEntry\n");
            print_indent(depth + 1); printf("key:\n");
            print_ast(node->as.array_entry.key, depth + 2);
            print_indent(depth + 1); printf("value:\n");
            print_ast(node->as.array_entry.value, depth + 2);
            break;

        case STRUCT_LIT:
            printf("StructLit (%d fields)\n", node->as.struct_lit.count);
            for (int i = 0; i < node->as.struct_lit.count; i++) {
                print_ast(node->as.struct_lit.fields[i], depth + 1);
            }
            break;

        case FIELD:
            printf("StructLitField %.*s\n", node->as.field.len, node->as.field.name);
            print_ast(node->as.field.value, depth + 1);
            break;

        case IMMUT_DECL:
        case MUT_DECL:
            printf("%s %.*s\n",
                node->type == IMMUT_DECL ? "ImmutDecl" : "MutDecl",
                node->as.decl.len, node->as.decl.name);
            if (node->as.decl.has_type_annotation) {
                print_indent(depth + 1); printf("type:\n");
                print_ast(node->as.decl.type_annotation, depth + 2);
            }
            print_ast(node->as.decl.value, depth + 1);
            break;

        case ASSIGN:
            printf("Assign %s\n", token_name(node->as.assign.op));
            print_ast(node->as.assign.target, depth + 1);
            print_ast(node->as.assign.value, depth + 1);
            break;

        case EXPR_STMT:
            printf("ExprStmt\n");
            print_ast(node->as.expr_stmt.expr, depth + 1);
            break;

        case IF_STMT:
            printf("If\n");
            print_indent(depth + 1); printf("cond:\n");
            print_ast(node->as.if_stmt.cond, depth + 2);
            print_indent(depth + 1); printf("then:\n");
            print_ast(node->as.if_stmt.then_branch, depth + 2);
            if (node->as.if_stmt.else_branch) {
                print_indent(depth + 1); printf("else:\n");
                print_ast(node->as.if_stmt.else_branch, depth + 2);
            }
            break;

        case BLOCK:
            printf("Block (%d stmts)\n", node->as.block.stmt_count);
            for (int i = 0; i < node->as.block.stmt_count; i++) {
                print_ast(node->as.block.stmts[i], depth + 1);
            }
            break;

        case PROGRAM:
            printf("Program (%d stmts)\n", node->as.program.stmt_count);
            for (int i = 0; i < node->as.program.stmt_count; i++) {
                print_ast(node->as.program.stmts[i], depth + 1);
            }
            break;

        case WHILE_STMT:
            printf("While\n");
            print_indent(depth + 1); printf("cond:\n");
            print_ast(node->as.while_stmt.cond, depth + 2);
            print_indent(depth + 1); printf("body:\n");
            print_ast(node->as.while_stmt.body, depth + 2);
            break;

        case SWITCH_STMT:
            printf("Switch\n");

            print_indent(depth + 1); printf("scrutinee:\n");
            print_ast(node->as.switch_stmt.scrutinee, depth + 2);

            if (node->as.switch_stmt.guard) {
                print_indent(depth + 1); printf("guard:\n");
                print_ast(node->as.switch_stmt.guard, depth + 2);
            }

            print_indent(depth + 1); printf("cases:\n");
            for (int i = 0; i < node->as.switch_stmt.case_count; i++) {
                print_ast(node->as.switch_stmt.cases[i], depth + 2);
            }
            break;

        case SWITCH_CASE:
            printf("Case\n");

            print_indent(depth + 1); printf("label:\n");
            print_ast(node->as.switch_case.label, depth + 2);

            print_indent(depth + 1); printf("body:\n");
            print_ast(node->as.switch_case.body, depth + 2);
            break;

        case DEFAULT_STMT:
            printf("Default\n");
            break;

        case FOR_RANGE:
            printf("ForRange\n");
            print_indent(depth + 1); printf("start:\n");
            print_ast(node->as.for_range.start, depth + 2);
            if (node->as.for_range.end) {
                print_indent(depth + 1); printf("end:\n");
                print_ast(node->as.for_range.end, depth + 2);
            }
            print_indent(depth + 1); printf("body:\n");
            print_ast(node->as.for_range.body, depth + 2);
            break;

        case FOR_CSTYLE:
            printf("ForCStyle\n");
            print_indent(depth + 1); printf("init:\n");
            print_ast(node->as.for_cstyle.init, depth + 2);
            print_indent(depth + 1); printf("cond:\n");
            print_ast(node->as.for_cstyle.cond, depth + 2);
            print_indent(depth + 1); printf("incr:\n");
            print_ast(node->as.for_cstyle.incr, depth + 2);
            print_indent(depth + 1); printf("body:\n");
            print_ast(node->as.for_cstyle.body, depth + 2);
            break;

        case FOR_IN:
            printf("ForIn\n");
            print_indent(depth + 1); printf("key: '%.*s'\n",
                node->as.for_in.key_len, node->as.for_in.key_name);
            print_indent(depth + 1); printf("val: '%.*s'\n",
                node->as.for_in.val_len, node->as.for_in.val_name);
            print_indent(depth + 1); printf("iterable:\n");
            print_ast(node->as.for_in.iterable, depth + 2);
            print_indent(depth + 1); printf("body:\n");
            print_ast(node->as.for_in.body, depth + 2);
            break;

        case RETURN_STMT:
            printf("Return\n");
            if (node->as.return_stmt.value) {
                print_ast(node->as.return_stmt.value, depth + 1);
            }
            break;

        case DEFER_STMT:
            printf("Defer\n");
            if (node->as.defer_stmt.value) {
                print_ast(node->as.defer_stmt.value, depth + 1);
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
                node->as.fn_decl.len, node->as.fn_decl.name, node->as.fn_decl.param_count);
            if (node->as.fn_decl.return_type) {
                print_indent(depth + 1); printf("returns:\n");
                print_ast(node->as.fn_decl.return_type, depth + 2);
            }
            print_indent(depth + 1); printf("params:\n");
            for (int i = 0; i < node->as.fn_decl.param_count; i++) print_ast(node->as.fn_decl.params[i], depth + 2);
            print_indent(depth + 1); printf("body:\n");
            print_ast(node->as.fn_decl.body, depth + 2);
            break;
            
        case METHOD:
            printf("Method %.*s (%d params%s)\n",
                node->as.method.len, node->as.method.name, node->as.method.param_count,
                node->as.method.is_mut ? ", mut" : "");
            if (node->as.method.return_type) {
                print_indent(depth + 1); printf("returns:\n");
                print_ast(node->as.method.return_type, depth + 2);
            }
            print_indent(depth + 1); printf("params:\n");
            for (int i = 0; i < node->as.method.param_count; i++) print_ast(node->as.method.params[i], depth + 2);
            print_indent(depth + 1); printf("body:\n");
            print_ast(node->as.method.body, depth + 2);
            break;

        case FN_LIT:
            printf("FnLit (%d params)\n", node->as.fn_decl.param_count);
            if (node->as.fn_decl.return_type) {
                print_indent(depth + 1); printf("returns:\n");
                print_ast(node->as.fn_decl.return_type, depth + 2);
            }
            print_indent(depth + 1); printf("params:\n");
            for (int i = 0; i < node->as.fn_decl.param_count; i++) {
                print_ast(node->as.fn_decl.params[i], depth + 2);
            }

            print_indent(depth + 1); printf("body:\n");
            print_ast(node->as.fn_decl.body, depth + 2);
            break;

        case PARAM:
            if (node->as.field.is_variadic) {
                printf("Param ... (variadic%s)\n", node->as.field.type_expr ? ", typed" : "");
                if (node->as.field.type_expr) {
                    print_indent(depth + 1); printf("type:\n");
                    print_ast(node->as.field.type_expr, depth + 2);
                }
                break;
            }
            printf("Param %.*s%s%s\n",
                node->as.field.len, node->as.field.name,
                node->as.field.type_expr ? " (typed)" : "",
                node->as.field.value ? " (default)" : "");
            if (node->as.field.type_expr) {
                print_indent(depth + 1); printf("type:\n");
                print_ast(node->as.field.type_expr, depth + 2);
            }
            if (node->as.field.value) {
                print_indent(depth + 1); printf("default:\n");
                print_ast(node->as.field.value, depth + 2);
            }
            break;

        case STRUCT_DECL:
            printf("StructDecl %.*s (%d fields)\n",
                node->as.struct_decl.len, node->as.struct_decl.name,
                node->as.struct_decl.field_count);
            print_indent(depth + 1); printf("fields:\n");
            for (int i = 0; i < node->as.struct_decl.field_count; i++) {
                print_ast(node->as.struct_decl.fields[i], depth + 2);
            }
            break;

        case ENUM_DECL:
            printf("EnumDecl %.*s (%d variants)\n",
                node->as.enum_decl.len, node->as.enum_decl.name,
                node->as.enum_decl.variant_count);
            print_indent(depth + 1); printf("variants:\n");
            for (int i = 0; i < node->as.enum_decl.variant_count; i++) {
                AstNode *v = node->as.enum_decl.variants[i];
                print_indent(depth + 2);
                printf("EnumVariant %.*s\n", v->as.field.len, v->as.field.name);
                if (v->as.field.value) {
                    print_indent(depth + 3); printf("value:\n");
                    print_ast(v->as.field.value, depth + 4);
                } else {
                    print_indent(depth + 3); printf("(auto-assigned)\n");
                }
            }
            break;

        case TYPE_BLOCK:
            printf("TypeBlock %s (%d decls)\n",
                token_name(node->as.type_block.type_annotation),
                node->as.type_block.decl_count);
            for (int i = 0; i < node->as.type_block.decl_count; i++) {
                print_ast(node->as.type_block.decls[i], depth + 1);
            }
            break;

        case TRY_STMT:
            printf("Try\n");
            print_indent(depth + 1); printf("try:\n");
            print_ast(node->as.try_stmt.try_block, depth + 2);
            print_indent(depth + 1); printf("catch (%.*s):\n",
                node->as.try_stmt.catch_len, node->as.try_stmt.catch_name);
            print_ast(node->as.try_stmt.catch_block, depth + 2);
            break;

        case IMPORT:
            if (node->as.import.path) {
                printf("Import '%.*s'", node->as.import.path_len, node->as.import.path);
                if (node->as.import.alias) {
                    printf(" as %.*s", node->as.import.alias_len, node->as.import.alias);
                }
                printf("\n");
            }
            break;

        case PROCTIME_BLOCK:
            printf("ProctimeBlock (%d stmts)\n", node->as.proctime_block.stmt_count);
            for (int i = 0; i < node->as.proctime_block.stmt_count; i++) {
                print_ast(node->as.proctime_block.stmts[i], depth + 1);
            }
            break;

        case PRIMITIVE:
            printf("Primitive %.*s (%d params)\n",
                node->as.primitive.len, node->as.primitive.name, node->as.primitive.param_count);
            if (node->as.primitive.return_type) {
                print_indent(depth + 1); printf("returns:\n");
                print_ast(node->as.primitive.return_type, depth + 2);
            }
            print_indent(depth + 1); printf("params:\n");
            for (int i = 0; i < node->as.primitive.param_count; i++) print_ast(node->as.primitive.params[i], depth + 2);
            break;

        case ARRAY_TYPE:
            printf("ArrayType\n");
            print_indent(depth + 1); printf("element:\n");
            print_ast(node->as.array_type.element_type, depth + 2);
            break;

        default:
            printf("<unhandled AstType %d>\n", (int)node->type);
            break;
    }
}