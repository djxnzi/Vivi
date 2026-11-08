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

static const char* format_op_error(const char *fmt, va_list *args) {
    static char buffer[128];
    TokenType op = (TokenType)va_arg(*args, int);
    snprintf(buffer, sizeof(buffer), fmt, op_symbol(op));
    return buffer;
}

static const char* error_category(const char *code) {
    switch (hash(code)) {
        case hash("type_mismatch"):
        case hash("param_type_mismatch"):
        case hash("variadic_type_mismatch"):
        case hash("wrong_return_type"):
        case hash("return_type_mismatch"):
        case hash("coerce"):
        case hash("math_gap"):
        case hash("cast_mismatch"):
        case hash("cast_range"):
        case hash("string_cast"):
            return "Type Mismatch";

        case hash("parse_error"):
        case hash("malformed_stmt"):
        case hash("local_static_decl"):
        case hash("unterminated"):
        case hash("interpolation"):
        case hash("rune_lit"):
            return "Parse Error";

        case hash("missing_arg"):
        case hash("builtin_arity"):
            return "Arity Mismatch";

        case hash("proctime_cycle"):
            return "Proctime Error";

        case hash("cant_destructure"):
        case hash("destructure_val_mismatch"):
            return "Destructure Error";

        case hash("no_enum_variant"):
        case hash("enum_variant"):
            return "Enum Error";

        case hash("bad_array_size"):
        case hash("duplicate_struct"):
        case hash("duplicate_method"):
            return "Declaration Error";

        case hash("io_open"):
        case hash("io_read"):
        case hash("io_write"):
        case hash("io_close"):
        case hash("io_run"):
        case hash("io_pipe"):
        case hash("io_wait"):
            return "IO Error";

        default:
            return nullptr;
    }
}

static const char* suggestion_for(const char *code) {
    switch (hash(code)) {
        case hash("immut_redecl"):          return "Use := for mutable.";
        case hash("self_outside_method"):   return "Declare as #method.";
        case hash("no_return_value"):       return "Add a return, or declare as void.";
        case hash("coerce"):                return "use ~ to concatenate strings";
        default:                            return nullptr;
    }
}

static bool is_internal_error(const char *code) {
    switch (hash(code)) {
        case hash("unhandled"):
        case hash("unhandled_math_op"):
        case hash("unprintable"):
        case hash("unhandled_node_type"):
        case hash("alloc_guard"):
        case hash("non_numeric_range"):
            return true;
        default:
            return false;
    }
}

static void print_source_line(FILE *out, const char *src, int target_line, int col, const char *label, int label_len) {
    const int context = 2;
    int first = target_line - context;
    if (first < 1) first = 1;
    int last = target_line;
    int w = snprintf(NULL, 0, "%d", last);

    int line = 1;
    const char *p = src;
    while (*p && line < first) {
        if (*p == '\n') line++;
        p++;
    }

    for (; line <= last; line++) {
        if (!*p && line > target_line) break;

        const char *end = p;
        while (*end && *end != '\n') end++;
        int len = (int)(end - p);

        if (len > 0) fprintf(out, " %*d |  %.*s\n", w, line, len, p);
        else         fprintf(out, " %*d |\n", w, line);

        if (line == target_line) {
            if (col > 0) {
                fprintf(out, " %*s |  ", w, "");
                for (int i = 0; i < col - 1 && i < len; i++) {
                    fputc(p[i] == '\t' ? '\t' : ' ', out);
                }
                fprintf(out, "ˆ");
                if (label) fprintf(out, " %.*s", label_len, label);
                fputc('\n', out);
            } else if (label) {
                fprintf(out, " %*s |  %.*s\n", w, "", label_len, label);
            }
        }

        if (!*end) break;
        p = end + 1;
    }
}

static void print_breadcrumb(FILE *dest, Env *env) {
    const char *names[64];
    int lens[64];
    int count = 0;

    for (Env *e = env; e && e->caller && count < 64; e = e->caller) {
        if (e->call_name) {
            names[count] = e->call_name;
            lens[count] = e->call_name_len;
        } else {
            names[count] = "anon";
            lens[count] = 4;
        }
        count++;
    }
    if (count == 0) return;

    fprintf(dest, "    » traced from [");
    for (int i = count - 1; i >= 0; i--) {
        fprintf(dest, "%.*s", lens[i], names[i]);
        if (i > 0) fprintf(dest, "() › ");
    }
    fprintf(dest, "]\n");
}

static bool prev_was_oneline = false;

static void write_error(FILE *dest, const char *msg, int n, const char *code, const char *hint) {
    const char *category = error_category(code);
    bool internal = is_internal_error(code);
    const char *file = current_file ? current_file : "<unknown>";

    if (category && program_source) {
        // Expanded error
        if (prev_was_oneline) fputc('\n', dest);
        prev_was_oneline = false;
        fprintf(dest, "[ERROR] %s in %s\n", category, file);
        print_source_line(dest, program_source, current_line, current_col, msg, n);
        if (hint) fprintf(dest, "    » try %s\n", hint);
        print_breadcrumb(dest, global_env);
        fprintf(dest, "\n");
    } else if (internal) {
        prev_was_oneline = true;
        fprintf(dest, "» %s:%d: [internal] %.*s\n", file, current_line, n, msg);
    } else {
        // One line error
        prev_was_oneline = true;
        fprintf(dest, "[ERROR] %s:%d: %.*s", file, current_line, n, msg);
        if (hint) fprintf(dest, " %s", hint);
        fprintf(dest, "\n");
    }
}

static void build_caught_string(const char *heap, int n) {
    char *caught_buf = nullptr;
    size_t caught_size = 0;
    FILE *caught_stream = open_memstream(&caught_buf, &caught_size);
    fprintf(caught_stream, "[CAUGHT] ");
    fwrite(heap, 1, (size_t)n, caught_stream);
    if (current_file)
        fprintf(caught_stream, " in %s:%d:%d", current_file, current_line, current_col);
    print_breadcrumb(caught_stream, global_env);
    fclose(caught_stream);
    last_error_value = make_str(caught_buf, (int)caught_size);
}

static const char* value_type_label(ValueType t) {
    switch (t) {
        case ValueType::Null:   return "null";
        case ValueType::Bool:   return "bool";
        case ValueType::Int:    return "int";
        case ValueType::Float:  return "float";
        case ValueType::Str:    return "string";
        case ValueType::Rune:   return "rune";
        case ValueType::Array:  return "array";
        case ValueType::Struct: return "struct";
        case ValueType::Fn:     return "function";
        default:                return "value";
    }
}

static void type_annotation_name(AstNode *t, const char **name, int *len) {
    if (t && t->type == IDENT && t->name) { *name = t->name; *len = t->len; }
    else { *name = "the declared type"; *len = 17; }
}

static void err_type_mismatch(AstNode *annotation, Value v) {
    const char *tn;
    int tl;
    type_annotation_name(annotation, &tn, &tl);
    print_err("type_mismatch", tl, tn, value_type_label(v.type));
}

static bool is_scalar_type(const char *name, int len) {
    static const char *scalars[] = {
        "string", "rune", "bool", "i8", "i16", "i32", "i64",
        "u8", "u16", "u32", "u64", "f32", "f64",
    };
    for (const char *s : scalars)
        if ((int)strlen(s) == len && strncmp(s, name, len) == 0) return true;
    return false;
}

static int edit_distance(const char *a, int al, const char *b, int bl) {
    if (al > 31 || bl > 31) return 99;
    int prev[32], cur[32];
    for (int j = 0; j <= bl; j++) prev[j] = j;
    for (int i = 1; i <= al; i++) {
        cur[0] = i;
        for (int j = 1; j <= bl; j++) {
            int best = prev[j - 1] + (a[i - 1] == b[j - 1] ? 0 : 1);
            if (prev[j] + 1 < best) best = prev[j] + 1;
            if (cur[j - 1] + 1 < best) best = cur[j - 1] + 1;
            cur[j] = best;
        }
        memcpy(prev, cur, sizeof(int) * (size_t)(bl + 1));
    }
    return prev[bl];
}

static void closest_in_env(Env *env, const char *name, int len, int *best, const char **out, int *out_len) {
    for (Env *e = env; e; e = e->parent) {
        for (int i = 0; i < e->count; i++) {
            EnvEntry *en = &e->entries[i];
            if (en->len > 0 && en->name[0] == '_') continue;
            if (en->len == len && strncmp(en->name, name, len) == 0) continue;
            int d = edit_distance(name, len, en->name, en->len);
            if (d < *best) { *best = d; *out = en->name; *out_len = en->len; }
        }
    }
}

static bool closest_name(const char *name, int len, const char **out, int *out_len) {
    if (len < 3) return false;
    int limit = len / 3;
    if (limit < 1) limit = 1;
    int best = limit + 1;

    closest_in_env(global_env, name, len, &best, out, out_len);
    closest_in_env(pt_check_env, name, len, &best, out, out_len);
    closest_in_env(pt_bleed_env, name, len, &best, out, out_len);
    return best <= limit;
}

static const char* build_hint(const char *code, va_list args, char *buf, size_t size) {
    switch (hash(code)) {
        case hash("undefined"): {
            int len = va_arg(args, int);
            const char *name = va_arg(args, const char*);
            const char *near_name = nullptr;
            int near_len = 0;
            if (!closest_name(name, len, &near_name, &near_len)) return nullptr;
            snprintf(buf, size, "Did you mean '%.*s'?", near_len, near_name);
            return buf;
        }
        case hash("type_mismatch"): {
            int tl = va_arg(args, int);
            const char *tn = va_arg(args, const char*);
            const char *got = va_arg(args, const char*);
            if (!is_scalar_type(tn, tl)) return nullptr;
            bool got_is_type = !strcmp(got, "string") || !strcmp(got, "rune") || !strcmp(got, "bool");
            bool got_castable = got_is_type || !strcmp(got, "int") || !strcmp(got, "float");
            if (!got_castable) return nullptr;
            if (got_is_type)
                snprintf(buf, size, "%.*s(...) to cast, or declare it as %s", tl, tn, got);
            else
                snprintf(buf, size, "%.*s(...) to cast", tl, tn);
            return buf;
        }
        default:
            return suggestion_for(code);
    }
}

static void print_traceback(int err_line, int err_col, FILE *dest) {
    const char *file = current_file ? current_file : "<unknown>";
    if (!program_source) {
        fprintf(dest, "%s:%d:%d", file, err_line, err_col);
        return;
    }
    fprintf(dest, "%s:%d:%d\n", file, err_line, err_col);
    print_source_line(dest, program_source, err_line, err_col, nullptr, 0);
    print_breadcrumb(dest, global_env);
}

void print_err(const char* code, ...) {
    const char* fmt;
    va_list args, probe;
    va_start(args, code);
    va_copy(probe, args);

    switch (hash(code)) {
        // Lexer / parser
        case hash("unterminated"):              fmt = "Unterminated string literal starting at line %d"; break;
        case hash("interpolation"):             fmt = "Invalid expression inside string interpolation"; break;
        case hash("rune_lit"):                  fmt = "Rune literal must be exactly one character, got %d at line %d"; break;
        case hash("parse_error"):               fmt = "%s (got '%.*s')"; break;
        case hash("local_static_decl"):         fmt = "local/static must prefix a declaration (got '%.*s')"; break;

        // Proctime
        case hash("proctime_cycle"):            fmt = "Circular proctime dependency involving '%.*s'"; break;

        // Declarations / variables / types
        case hash("undefined"):                 fmt = "'%.*s' is not declared."; break;
        case hash("immut_redecl"):              fmt = "Can't redeclare '%.*s'. Declared with ::."; break;
        case hash("mut_redecl"):                fmt = "Can't redeclare '%.*s'."; break;
        case hash("reserved"):                  fmt = "'%.*s' collides with a builtin function."; break;
        case hash("reserved_type"):             fmt = "Type name can't be used as a declaration name (got '%.*s')."; break;
        case hash("type_mismatch"):             fmt = "expected %.*s, got %s"; break;
        case hash("bad_array_size"):            fmt = "Array size for '%.*s' must be a non-negative integer."; break;
        case hash("ident_assignment"):          fmt = "Can only assign to a plain identifier or struct field."; break;
        case hash("param_null"):                fmt = "Parameter '%.*s' is typed and cannot be null."; break;

        // Control flow
        case hash("for_range"):                 fmt = "For loop range bounds must be integers."; break;
        case hash("for_in"):                    fmt = "For-in requires an array or struct."; break;
        case hash("non_numeric_range"):         fmt = "Switch range case only supports numbers, lang gap needs fixing."; break;

        // Operators / expressions
        case hash("unary"):                     fmt = "Unary '-' only supports numbers."; break;
        case hash("postfix"):                   fmt = "'++'/'--' only supports plain identifiers."; break;
        case hash("deref_type"):                fmt = "Dereference target must be a pointer."; break;
        case hash("unhandled"):                 fmt = "Unhandled unary op %d."; break;
        case hash("unhandled_math_op"):         fmt = "Unhandled math op %d."; break;
        case hash("bitwise_int_only"):          fmt = "Bitwise operators only support integers."; break;
        case hash("only_num"):                  fmt = "Operator %d only supports numbers."; break;
        case hash("coerce"):                    fmt = "Can't coerce non-numeric string for arithmetic."; break;
        case hash("rhs_in"):                    fmt = "'in' requires an array on the right."; break;
        case hash("math_gap"):                  fmt = format_op_error("Operator '%s' does not support this operand type.", &args); break;
        case hash("unprintable"):               fmt = "Unprintable value type %d."; break;

        // Typecasts
        case hash("cast_mismatch"):             fmt = "Cannot cast this value to %.*s."; break;
        case hash("string_cast"):               fmt = "Cannot cast this value to a string."; break;
        case hash("cast_range"):                fmt = "Value doesn't fit in %.*s."; break;

        // Functions / calls
        case hash("not_function"):              fmt = "'%.*s' is not a function."; break;
        case hash("not_method"):                fmt = "'%.*s' is not a method."; break;
        case hash("not_callable"):              fmt = "Value is not callable."; break;
        case hash("missing_arg"):               fmt = "Missing argument for parameter '%.*s'."; break;
        case hash("param_type_mismatch"):       fmt = "Argument for parameter '%.*s' does not match declared type."; break;
        case hash("variadic_type_mismatch"):    fmt = "Variadic argument %d does not match declared type."; break;
        case hash("no_return_value"):           fmt = "Function declared with '-> type' must return a value."; break;
        case hash("wrong_return_type"):         fmt = "Return value doesn't match declared return type '%.*s'."; break;
        case hash("return_type_mismatch"):      fmt = "Return value doesn't match declared return type."; break;
        case hash("self_outside_method"):       fmt = "'self' used outside a method."; break;

        // Structs / fields
        case hash("field_access"):              fmt = "'.' only works on structs."; break;
        case hash("field_assignment"):          fmt = "Field assignment only works on structs."; break;
        case hash("no_field"):                  fmt = "No such field '%.*s'."; break;
        case hash("no_field_access"):           fmt = "Field access not supported for this type."; break;
        case hash("duplicate_struct"):          fmt = "Struct '%.*s' is already declared."; break;
        case hash("cant_destructure"):          fmt = "Cannot destructure this value, expected an array or struct."; break;
        case hash("destructure_val_mismatch"):  fmt = "Destructuring expected %d values, got %d."; break;

        // Enums
        case hash("no_enum_variant"):           fmt = "Enum '%.*s' has no variant '%.*s'."; break;
        case hash("enum_variant"):              fmt = "Enum variant '%.*s' has no value and the previous variant isn't numeric."; break;

        // Arrays / indexing
        case hash("arr_index"):                 fmt = "Indexing only works on arrays."; break;
        case hash("int_or_str"):                fmt = "Array index must be int or string."; break;
        case hash("index_not_found"):           fmt = "Index not found."; break;
        case hash("pop_empty"):                 fmt = "pop() called on an empty array."; break;
        case hash("take_out_of_range"):         fmt = "take() index out of range."; break;
        case hash("sort_non_numeric"):          fmt = "sort() only works on arrays of numbers."; break;
        case hash("cant_sort"):                 fmt = "sort() can't order null, bool, array, struct or function values."; break;

        // String / array methods
        case hash("char_at_pos"):               fmt = "char_at() position must be an integer."; break;
        case hash("char_at_range"):             fmt = "char_at() index out of range."; break;
        case hash("method_wrong_args"):         fmt = "'%.*s' called with the wrong number of arguments."; break;
        case hash("method_wrong_type"):         fmt = "Method '%.*s' is not supported on this type."; break;
        case hash("unknown_method"):            fmt = "Unknown array method '%.*s'."; break;

        // Builtins
        case hash("print_wrong_args"):          fmt = "print() takes exactly one argument."; break;
        case hash("print_no_args"):             fmt = "print() was not provided with a string."; break;
        case hash("exit_not_int"):              fmt = "Exit code must be an integer."; break;
        case hash("ext_not_str"):               fmt = "Ext path must be a string."; break;

        // Directives
        case hash("cant_find_import"):          fmt = "Cannot find import '%s'."; break;
        case hash("import_parse_err"):          fmt = "Parse error in import '%s'."; break;
        case hash("duplicate_method"):          fmt = "Method '%.*s' is already declared."; break;
        case hash("not_a_method"):              fmt = "'%.*s' is not a method."; break;

        // Builtins
        case hash("builtin_arity"):             fmt = "Builtin '%.*s' expects %d argument(s), got %d."; break;
        case hash("builtin_contract"):          fmt = "Builtin '%.*s' received an invalid argument."; break;
        case hash("io_open"):                   fmt = "Could not open '%.*s': %s."; break;
        case hash("io_read"):                   fmt = "Could not read fd %d: %s."; break;
        case hash("io_write"):                  fmt = "Could not write fd %d: %s."; break;
        case hash("io_close"):                  fmt = "Could not close fd %d: %s."; break;
        case hash("io_run"):                    fmt = "Could not run '%.*s': %s."; break;
        case hash("io_pipe"):                   fmt = "Could not create a pipe: %s."; break;
        case hash("io_wait"):                   fmt = "Could not wait for process %d: %s."; break;

        // General / runtime infra
        case hash("runtime_error"):             fmt = "Runtime error '%.*s'."; break;
        case hash("unhandled_node_type"):       fmt = "Unhandled node type %d."; break;
        case hash("alloc_guard"):               fmt = "Allocation guard: single request of %zu bytes exceeds the %zu byte block limit."; break;
        case hash("malformed_stmt"):            fmt = "%s"; break;

        default:                                fmt = "Unknown error code"; break;
    }

    char hint_buf[160];
    const char *hint = build_hint(code, probe, hint_buf, sizeof(hint_buf));
    va_end(probe);

    char buf[512];
    int n = vsnprintf(buf, sizeof(buf), fmt, args);
    va_end(args);

    if (n < 0) n = 0;
    if (n > (int)sizeof(buf) - 1) n = (int)sizeof(buf) - 1;

    char *heap = (char*)arena_alloc(n);
    memcpy(heap, buf, n);
    bool will_exit = in_exec && try_depth == 0;

    if (in_exec) {
        build_caught_string(heap, n);

        if (will_exit) {
            if (stdout_open_line) fputc('\n', stdout);
            fflush(stdout);
            write_error(stderr, heap, n, code, hint);
        }

        error_count++;
        if (will_exit) exit(CODE_ERROR);
        longjmp(*error_handler, 1);
    }

    if (!pending_errors) pending_errors = open_memstream(&pending_errors_buf, &pending_errors_size);

    build_caught_string(heap, n);

    if (try_depth == 0) {
        FILE *dest = !program_output ? stderr : pending_errors;
        write_error(dest, heap, n, code, hint);
    }

    error_count++;
}
