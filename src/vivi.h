#pragma once

// All imports here
#include <cstdlib>
#include <filesystem>
#include <cstring>
#include <string>
#include <cstdarg>
#include <cmath>
#include <ctime>
#include <csetjmp>
#include <cerrno>
#include <fcntl.h>
#include <unistd.h>
#include <sys/wait.h>

namespace fs = std::filesystem;

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

// VIVI_HOME wins if set (the Nix wrapper sets it) :3
// Otherwise use <dir of the binary>/../lib, which works in the repo
// (build/vivi -> ./lib) and for a plain install (bin/vivi -> ../lib)
// Teehee :3 - Xansi
inline std::string default_lib_dir() {
    if (const char *env = std::getenv("VIVI_HOME"))
        return std::string(env) + "/lib";
    std::error_code ec;
    auto exe = std::filesystem::canonical("/proc/self/exe", ec);
    if (ec) return "./lib";   // old behavior as a last resort

    // Fix for new library location
    auto root = exe.parent_path().parent_path();
    if (std::filesystem::is_directory(root / "lib/vivi")) {
        return (root / "lib/vivi").string();
    }
    if (std::filesystem::is_directory(root / "lib")) {
        return (exe.parent_path().parent_path() / (exe.parent_path().filename() == "bin" ? "lib/vivi" : "lib")).string();
    }

    return "./lib";
}

// Vivi forward decl
struct Vivi {
    const char* version = "0.03a";
    std::string root_dir;
    std::string lib_dir = default_lib_dir();
    const bool exit_code_debug = false;
    void processFlags(int argc, char* argv[], std::string& outScriptPath, const char **outScriptArgs, int *outScriptArgCount);
    void sourceExec(const std::string& source, const std::string& scriptPath, const char * const *scriptArgs, int scriptArgCount);
    void run(int argc, char* argv[]);
    void seed(unsigned int s);
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
#include <builtin.h>
#include <proctime.h>

#include <arena.cpp>
#include <lexer.cpp>
#include <ast.cpp>
#include <parserHelpers.cpp>
#include <parseExpr.cpp>
#include <parseDecl.cpp>
#include <parseStmt.cpp>
#include <env.cpp>
#include <values.cpp>
#include <registry.cpp>
#include <eval.cpp>
#include <exec.cpp>
#include <error.cpp>
#include <builtin.cpp>
#include <proctimeGraph.cpp>
#include <proctime.cpp>

#include <tooling.cpp>
#include <runtime.cpp>
