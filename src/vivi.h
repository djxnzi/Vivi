#pragma once

// All imports here
#include <cstdlib>
#include <filesystem>
#include <cstring>
#include <cstdarg>
#include <cmath>
#include <ctime>

namespace fs = std::filesystem;
int random_seed = (srand(time(0)), 0);

inline bool read_file(const char *path, std::string &out) {
    FILE *f = fopen(path, "rb");
    if (!f) return false;
    fseek(f, 0, SEEK_END);
    long len = ftell(f);
    fseek(f, 0, SEEK_SET);
    out.resize(len > 0 ? (size_t)len : 0);
    out.resize(fread(&out[0], 1, out.size(), f));
    fclose(f);
    return true;
}

// Vivi forward decl
struct Vivi {
    const char* version = "0.1";
    std::string root_dir;
    const bool exit_code_debug = false;
    void processFlags(int argc, char* argv[], std::string& outScriptPath, const char **outScriptArgs, int *outScriptArgCount);
    void sourceExec(const std::string& source, const std::string& scriptPath, const char * const *scriptArgs, int scriptArgCount);
    void run(int argc, char* argv[]);
};
inline Vivi vivi;

// wheeee unity build
#include <tokens.h>
#include <ast.h>
#include <lexer.h>
#include <parser.h>
#include <error.h>
#include <env.h>
#include <values.h>
#include <registry.h>
#include <eval.h>
#include <exec.h>
#include <primitive.h>
#include <proctime.h>

#include <arena.c>
#include <lexer.c>
#include <ast.c>
#include <parserHelpers.c>
#include <parseExpr.c>
#include <parseDecl.c>
#include <parseStmt.c>
#include <env.c>
#include <values.c>
#include <registry.c>
#include <eval.c>
#include <exec.c>
#include <error.c>
#include <primitive.c>
#include <proctimeGraph.c>
#include <proctime.c>

#include <tooling.c>
#include <runtime.c>