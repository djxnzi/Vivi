static const char **script_argv = nullptr;
static const char *program_source = nullptr;
static const char *current_file = nullptr;
static Value last_error_value = make_null();
static AstNode* exec_stmt(AstNode *node);
void run_program(AstNode *program, int argc, const char **argv, const char *source);