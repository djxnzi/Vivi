void Vivi::processFlags(int argc, char* argv[], std::string& outScriptPath, const char **outScriptArgs, int *outScriptArgCount) {

    outScriptPath.clear();
    *outScriptArgCount = 0;
    bool hasScript = false;
    bool arg1IsPath = false;
    if (argc > 1 && argv[1][0] != '-') {
        outScriptPath = argv[1];
        hasScript = true;
        arg1IsPath = true;
    }

    if (argc == 1) {
        printf("Vivi Usage: vivi <path> --args\n");
    }

    if (hasScript) {
        fs::path p(outScriptPath);
        if (!fs::exists(p)) {
            printf("Error: path '%s' does not exist.\n\n", outScriptPath.c_str());
            hasScript = false;
            outScriptPath.clear();
        } else if (fs::is_directory(p)) {
            fs::path mainPath = p / "main.vivi";
            if (fs::exists(mainPath) && fs::is_regular_file(mainPath)) {
                outScriptPath = mainPath.string();
            } else {
                bool found = false;
                for (const auto& entry : fs::directory_iterator(p)) {
                    if (fs::is_regular_file(entry.status()) && entry.path().extension() == ".vivi") {
                        outScriptPath = entry.path().string();
                        found = true;
                        break;
                    }
                }
                if (!found) {
                    printf("Error: no .vivi file found in directory '%s'.\n\n", p.string().c_str());
                    hasScript = false;
                    outScriptPath.clear();
                }
            }
        } else if (fs::is_regular_file(p)) {
            bool has_shebang = false;
            if (p.extension() != ".vivi") {
                FILE *probe = fopen(p.string().c_str(), "rb");
                if (probe) {
                    has_shebang = (fgetc(probe) == '#' && fgetc(probe) == '!');
                    fclose(probe);
                }
            }
            if (p.extension() != ".vivi" && !has_shebang) {
                printf("Error: '%s' is not a .vivi file.\n\n", outScriptPath.c_str());
                hasScript = false;
                outScriptPath.clear();
            }
        } else {
            printf("Error: '%s' is not a valid file or directory.\n\n", outScriptPath.c_str());
            hasScript = false;
            outScriptPath.clear();
        }
    }

    std::string source;
    if (!outScriptPath.empty()) read_file(outScriptPath.c_str(), source);

    int startIdx = arg1IsPath ? 2 : 1;
    for (int i = startIdx; i < argc; ++i) {
        outScriptArgs[*outScriptArgCount] = argv[i];
        (*outScriptArgCount)++;
        std::string arg(argv[i]);

        auto eqPos = arg.find('=');
        std::string key = (eqPos != std::string::npos) ? arg.substr(0, eqPos) : arg;
        std::string value = (eqPos != std::string::npos) ? arg.substr(eqPos + 1) : "";

        switch (hash(key.c_str())) {
            case hash("--version"):
                printf("Vivi is v%s\n\n", vivi.version);
                break;

            case hash("--lexer"): {
                if (!source.empty()) {
                    printf("Lexer output:\n\n");
                    print_lexer(source);
                } else {
                    printf("Error: no source to lex.\n\n");
                }
                break;
            }

            case hash("--ast"): {
                if (!source.empty()) {
                    Lexer parse_lx = { source.c_str(), source.c_str(), 1, 1 };
                    Parser p = {};
                    p.lx = &parse_lx;
                    p.had_error = false;
                    p.lookahead_count = 0;
                    parser_advance(&p);

                    AstNode *program = parse_program(&p);
                    if (p.had_error) {
                        printf("\nParsing finished with errors.\n\n");
                    } else {
                        printf("AST output:\n\n");
                        print_ast(program, 0);
                        printf("\n");
                    }
                } else {
                    printf("Error: no source to parse.\n\n");
                }
                break;
            }

            default:
                if (key.size() >= 2 && key[0] == '-' && key[1] == '-') {
                    printf("Unknown flag %s, ignoring.\n\n", argv[i]);
                }
                break;
        }
    }
}

void Vivi::sourceExec(const std::string& source, const std::string& scriptPath, const char * const *scriptArgs, int scriptArgCount) {
    current_file = scriptPath.c_str();
    program_source = source.c_str();
    Lexer parse_lx = { source.c_str(), source.c_str(), 1, 1 };
    Parser p = {};
    p.lx = &parse_lx;
    p.had_error = false;
    p.lookahead_count = 0;
    parser_advance(&p);
    AstNode *program = parse_program(&p);

    global_env = env_new(nullptr);

    // autoload _base.vivi
    std::string base_path = "./lib/core/_base.vivi";
    std::string base_source;
    if (read_file(base_path.c_str(), base_source)) {
        char *heap_src = (char*)arena_alloc((int)base_source.length() + 1);
        memcpy(heap_src, base_source.c_str(), base_source.length() + 1);
        char *heap_path = (char*)arena_alloc((int)base_path.length() + 1);
        memcpy(heap_path, base_path.c_str(), base_path.length() + 1);

        const char *saved_file = current_file;
        const char *saved_source = program_source;
        current_file = heap_path;
        program_source = heap_src;

        Lexer base_lx = { heap_src, heap_src, 1, 1 };
        Parser base_p = {};
        base_p.lx = &base_lx;
        base_p.had_error = false;
        base_p.lookahead_count = 0;
        parser_advance(&base_p);
        AstNode *base_program = parse_program(&base_p);

        if (!base_p.had_error) {
            resolve_primitives(base_program);
            for (int i = 0; i < base_program->as.program.stmt_count; i++)
                exec_stmt(base_program->as.program.stmts[i]);
        }

        current_file = saved_file;
        program_source = saved_source;
    }

    const char **argv = (const char**)arena_alloc((scriptArgCount + 1) * sizeof(const char*));
    argv[0] = scriptPath.c_str();
    for (int i = 0; i < scriptArgCount; i++) argv[i + 1] = scriptArgs[i];

    resolve_primitives(program);
    run_proctime(program);
    pt_check_structure(program);

    fwrite(proctime_kept_buf, 1, proctime_kept_size, stdout);
    free(proctime_kept_buf);

    if (pending_errors) {
        fclose(pending_errors);
        fwrite(pending_errors_buf, 1, pending_errors_size, stdout);
        free(pending_errors_buf);
    }

    if (error_count == 0) {
        run_program(program, scriptArgCount + 1, argv, source.c_str());
    } else {
        printf("\n\%d error(s) found; not running.\n", error_count);
        exit(CODE_USAGE);
    }
}

void Vivi::run(int argc, char* argv[]) {
    root_dir = fs::canonical("/proc/self/exe").parent_path().string();

    std::string scriptPath;
    const char **scriptArgs = (const char**)arena_alloc(argc * sizeof(const char*));
    int scriptArgCount = 0;
    vivi.processFlags(argc, argv, scriptPath, scriptArgs, &scriptArgCount);

    if (!scriptPath.empty()) {
        std::string source;
        if (!read_file(scriptPath.c_str(), source)) {
            printf("Error: could not open '%s'", scriptPath.c_str());
            return;
        }
        sourceExec(source, scriptPath, scriptArgs, scriptArgCount);
    }
}