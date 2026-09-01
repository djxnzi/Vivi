static const char* exit_code(int code);
static const char* op_symbol(TokenType op);
void print_err(const char* code, ...);
static const char* format_op_error(const char *fmt, va_list *args);;
static void print_frame_chain(Env *env);
static void print_source_line(FILE *out, const char *src, int target_line);
static void print_traceback(int err_line, int err_col, FILE *dest = stderr);
static int error_count = 0;
static int try_depth = 0;
static bool in_exec = false;
static FILE *program_output = nullptr;
static char *program_output_buf = nullptr;
static size_t program_output_size = 0;
static char *proctime_kept_buf = nullptr;
static size_t proctime_kept_size = 0;
static FILE *pending_errors = nullptr;
static char *pending_errors_buf = nullptr;
static size_t pending_errors_size = 0;

constexpr unsigned int hash(const char *s, int off = 0) {
    return !s[off] ? 5381 : (hash(s, off+1)*33) ^ static_cast<unsigned int>(s[off]);
}

enum ExitCode {
    CODE_OK       = 0,     // everything a-ok
    CODE_ERROR    = 1,     // runtime error
    CODE_USAGE    = 2,     // parse error
    CODE_NOTFOUND = 127,   // 404 thing not found
    CODE_PANIC    = 134,   // panic() sigabort
};