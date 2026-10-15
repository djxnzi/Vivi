static const char* exit_code(int code) {
    switch (code) {
        case CODE_OK:       return "exit code 0: ok";
        case CODE_ERROR:    return "exit code 1: runtime error";
        case CODE_USAGE:    return "exit code 2: parse error";
        case CODE_NOTFOUND: return "exit code 127: not found";
        case CODE_PANIC:    return "exit code 134: abort";
        default:            return "unknown error";
    }
}

static const char* op_symbol(TokenType op) {
    switch (op) {
        case TOK_PLUS:    return "+";
        case TOK_MINUS:   return "-";
        case TOK_STAR:    return "*";
        case TOK_SLASH:   return "/";
        case TOK_PERCENT: return "%%";
        case TOK_CARET:   return "^";
        case TOK_AMP:     return "&";
        case TOK_SHL:     return "<<";
        case TOK_SHR:     return ">>";
        case TOK_LT:      return "<";
        case TOK_LTE:     return "<=";
        case TOK_GT:      return ">";
        case TOK_GTE:     return ">=";
        default:          return "?";
    }
}

void print_err(const char* code, ...) {
    const char* fmt;
    va_list args;
    va_start(args, code);

    switch (hash(code)) {
        // Lexer / parser
        case hash("unterminated"):              fmt = "Unterminated string literal starting at line %d"; break;
        case hash("interpolation"):             fmt = "Invalid expression inside string interpolation"; break;
        case hash("rune_lit"):                  fmt = "Rune literal must be exactly one character, got %d at line %d"; break;
        case hash("parse_error"):               fmt = "Parse error at line %d, col %d: %s (got '%.*s')"; break;
        case hash("local_static_decl"):         fmt = "local/static must prefix a declaration at line %d, col %d (got '%.*s')"; break;

        // Proctime
        case hash("proctime_cycle"):            fmt = "Circular proctime dependency involving '%.*s'"; break;

        // Declarations / variables / types
        case hash("undefined"):                 fmt = "Undefined variable '%.*s'"; break;
        case hash("immut_redecl"):              fmt = "Cannot redeclare immutable variable '%.*s'"; break;
        case hash("mut_redecl"):                fmt = "Cannot redeclare mutable variable '%.*s'"; break;
        case hash("reserved"):                  fmt = "'%.*s' collision with builtin function"; break;
        case hash("reserved_type"):             fmt = "Type name cannot be used as a declaration name (got '%.*s')"; break;
        case hash("type_mismatch"):             fmt = "Value does not match declared type for '%.*s'"; break;
        case hash("bad_array_size"):            fmt = "Array size for '%.*s' must be a non-negative integer"; break;
        case hash("ident_assignment"):          fmt = "Can only assign to a plain identifier or struct field"; break;
        case hash("param_null"):                fmt = "Parameter '%.*s' is typed and cannot be null"; break;
        case hash("primitive_null_arg"):        fmt = "Argument %d to primitive '%.*s' is null"; break;

        // Control flow
        case hash("for_range"):                 fmt = "For Range only supports integers"; break;
        case hash("for_in"):                    fmt = "For In only supports arrays and structs"; break;
        case hash("non_numeric_range"):         fmt = "Switch range case only supports numbers, lang gap needs fixing"; break;

        // Operators / expressions
        case hash("unary"):                     fmt = "Unary '-' only supports numbers"; break;
        case hash("postfix"):                   fmt = "'++'/'--' only supports plain identifiers"; break;
        case hash("deref_type"):                fmt = "Dereference target must be a pointer"; break;
        case hash("unhandled"):                 fmt = "Unhandled unary op %d"; break;
        case hash("unhandled_math_op"):         fmt = "Unhandled math op %d"; break;
        case hash("bitwise_int_only"):          fmt = "Bitwise operators only support integers"; break;
        case hash("only_num"):                  fmt = "Operator %d only supports numbers"; break;
        case hash("coerce"):                    fmt = "Type mismatch, cannot coerce non-numeric string for arithmetic"; break;
        case hash("rhs_in"):                    fmt = "'in' requires an array on the right"; break;
        case hash("math_gap"):                  fmt = format_op_error("Operator '%s' does not support this operand type", &args); break;
        case hash("unprintable"):               fmt = "Unprintable value type %d"; break;

        // Typecasts
        case hash("cast_mismatch"):             fmt = "Cannot cast this value to %.*s"; break;
        case hash("string_cast"):               fmt = "Cannot cast this value to a string"; break;

        // Functions / calls
        case hash("not_function"):              fmt = "'%.*s' is not a function"; break;
        case hash("not_method"):                fmt = "'%.*s' is not a method"; break;
        case hash("not_callable"):              fmt = "Value is not callable"; break;
        case hash("missing_arg"):               fmt = "Missing argument for parameter '%.*s'"; break;
        case hash("param_type_mismatch"):       fmt = "Argument for parameter '%.*s' does not match declared type"; break;
        case hash("variadic_type_mismatch"):    fmt = "Variadic argument %d does not match declared type"; break;
        case hash("no_return_value"):           fmt = "Function declared with '-> type' must return a value"; break;
        case hash("wrong_return_type"):         fmt = "Function declared with '-> %.*s' returned a value of the wrong type"; break;
        case hash("return_type_mismatch"):      fmt = "Function returned a value that doesn't match its declared return type"; break;
        case hash("self_outside_method"):       fmt = "'self' used outside a method"; break;

        // Structs / fields
        case hash("field_access"):              fmt = "Field access only supports structs"; break;
        case hash("field_assignment"):          fmt = "Field assignment only supports structs"; break;
        case hash("no_field"):                  fmt = "No such field '%.*s'"; break;
        case hash("no_field_access"):           fmt = "Field access not supported for this type"; break;
        case hash("duplicate_struct"):          fmt = "Struct '%.*s' is already declared"; break;
        case hash("cant_destructure"):          fmt = "Cannot destructure this value, expected an array or struct"; break;
        case hash("destructure_val_mismatch"):  fmt = "Destructuring expected %d values, got %d"; break;

        // Enums
        case hash("no_enum_variant"):           fmt = "Enum '%.*s' has no variant '%.*s'"; break;
        case hash("enum_variant"):              fmt = "Enum variant '%.*s' has no value and the previous variant isn't numeric"; break;

        // Arrays / indexing
        case hash("arr_index"):                 fmt = "Indexing only supports arrays"; break;
        case hash("int_or_str"):                fmt = "Array index must be int or string"; break;
        case hash("index_not_found"):           fmt = "Index not found"; break;
        case hash("pop_empty"):                 fmt = "Cannot pop from an empty array"; break;
        case hash("take_out_of_range"):         fmt = "take() index out of range"; break;
        case hash("sort_non_numeric"):          fmt = "sort() only supports arrays of numbers"; break;
        case hash("cant_sort"):                 fmt = "sort() can't order null, bool, array, struct or function values"; break;

        // String / array methods
        case hash("char_at_pos"):               fmt = "char_at() position must be an integer"; break;
        case hash("char_at_range"):             fmt = "char_at() index out of range"; break;
        case hash("method_wrong_args"):         fmt = "'%.*s' called with the wrong number of arguments"; break;
        case hash("method_wrong_type"):         fmt = "Method '%.*s' is not supported on this type"; break;
        case hash("unknown_method"):            fmt = "Unknown array method '%.*s'"; break;

        // Builtins
        case hash("print_wrong_args"):          fmt = "print() takes exactly one argument"; break;
        case hash("print_no_args"):             fmt = "print() was not provided with a string"; break;
        case hash("exit_not_int"):              fmt = "Exit code must be an integer"; break;
        case hash("ext_not_str"):               fmt = "Ext path must be a string"; break;

        // Directives
        case hash("cant_find_import"):          fmt = "Cannot find import '%s'"; break;
        case hash("import_parse_err"):          fmt = "Parse error in import '%s'"; break;
        case hash("duplicate_method"):          fmt = "Method '%.*s' is already declared"; break;
        case hash("not_a_method"):              fmt = "'%.*s' is not a method"; break;

        // Primitives
        case hash("duplicate_primitive"):       fmt = "Primitive '%.*s' is already declared"; break;
        case hash("primitive_arity"):           fmt = "Primitive '%.*s' expects %d argument(s), got %d"; break;
        case hash("primitive_arg_type"):        fmt = "Argument %d does not match primitive '%.*s' signature"; break;
        case hash("primitive_unavailable"):     fmt = "Primitive '%.*s' is not supported by this target"; break;
        case hash("primitive_return_type"):     fmt = "Primitive '%.*s' returned a value outside its declared signature"; break;
        case hash("primitive_contract"):        fmt = "Interpreter primitive '%.*s' received an invalid internal call"; break;
        case hash("primitive_variadic"):        fmt = "Primitive declarations cannot be variadic"; break;

        // General / runtime infra
        case hash("runtime_error"):             fmt = "Runtime error '%.*s'"; break;
        case hash("unhandled_node_type"):       fmt = "Unhandled node type %d"; break;
        case hash("alloc_guard"):               fmt = "Allocation guard: single request of %zu bytes exceeds the %zu byte block limit"; break;
        case hash("malformed_stmt"):            fmt = "Malformed statement: '%s'"; break;

        default:                                fmt = "Unknown error code"; break;
    }

    // this is kind of a mess but i was having a lot of trouble formatting the output
    // so i just said fuck it and used direct fwrites

    char buf[512];
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (n < 0) n = 0;
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;

    char *heap = (char*)arena_alloc(n);
    memcpy(heap, buf, n);
    bool will_exit = in_exec && try_depth == 0;
    if (!pending_errors) pending_errors = open_memstream(&pending_errors_buf, &pending_errors_size);
    Value msg = make_str(heap, n);

    char *caught_buf = nullptr;
    size_t caught_size = 0;
    FILE *caught_stream = open_memstream(&caught_buf, &caught_size);
    fprintf(caught_stream, "[CAUGHT] ");
    fwrite(heap, 1, (size_t)n, caught_stream);
    print_traceback(current_line, current_col, caught_stream);
    fclose(caught_stream);
    last_error_value = make_str(caught_buf, (int)caught_size);

    if (will_exit && program_output) {
        fclose(program_output);
        fwrite(program_output_buf, 1, program_output_size, stdout);
        program_output = nullptr;
    }

    if (will_exit || try_depth == 0) {
        FILE *dest = (will_exit || !program_output) ? stdout : pending_errors;
        fprintf(dest, "[ERROR] ");
        value_print(msg, dest);
        if (will_exit || !program_output) fflush(stdout);
        print_traceback(current_line, current_col, (will_exit || !program_output) ? stderr : pending_errors);
        fprintf(dest, "\n");
    }

    error_count++;
    if (will_exit) {
        exit(CODE_ERROR);
    }
}

static const char* format_op_error(const char *fmt, va_list *args) {
    static char buffer[128];
    TokenType op = (TokenType)va_arg(*args, int);
    snprintf(buffer, sizeof(buffer), fmt, op_symbol(op));
    return buffer;
}

static void print_frame_chain(Env *env) {
    if (!env) return;
    print_frame_chain(env->caller);
    if (!env->caller) return;
    if (env->call_name) {
        if (env->call_line == 0 && env->call_col == 0)
            fprintf(stderr, "  in fn %.*s\n", env->call_name_len, env->call_name);
        else
            fprintf(stderr, "  at line %d, col %d, in fn %.*s\n", env->call_line, env->call_col, env->call_name_len, env->call_name);
    } else {
        fprintf(stderr, "  at line %d, col %d, in anonymous fn\n", env->call_line, env->call_col);
    }
}

static void print_source_line(FILE *out, const char *src, int target_line) {
    int line = 1;
    const char *line_start = src;
    for (const char *p = src; *p && line < target_line; p++) {
        if (*p == '\n') { line++; line_start = p + 1; }
    }
    const char *line_end = line_start;
    while (*line_end && *line_end != '\n') line_end++;
    fprintf(out, "    %.*s\n", (int)(line_end - line_start), line_start);
}

static void print_traceback(int err_line, int err_col, FILE *dest) {
    if (!program_source) {
        if (current_file) {
            fprintf(dest, ", in %s", current_file);
        }
        return;
    }
    fprintf(dest, "\nTraceback: %s, at line %d, col %d:\n", current_file ? current_file : "<unknown>", err_line, err_col);
    print_source_line(dest, program_source, err_line);
}