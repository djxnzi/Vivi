static const char **script_argv = nullptr;
static const char *program_source = nullptr;
static const char *current_file = nullptr;
static Value last_error_value = make_null();
static AstNode* exec_stmt(AstNode *node);
static AstNode* exec_try_block(AstNode *block, bool *caught);
void run_program(AstNode *program, int argc, const char **argv, const char *source);

struct ImportUnit { AstNode *program; const char *path; const char *source; Env *env; Env *hidden; };
static ImportUnit* load_import_unit(const std::string &path, const std::string &file_path, bool report);