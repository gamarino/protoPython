/**
 * protopy - Minimal Python runtime entrypoint.
 * Creates PythonEnvironment and resolves a module or script path (execution stubbed).
 */

#include <protoPython/PythonEnvironment.h>
#include <algorithm>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>

#ifdef STDLIB_PATH
#define DEFAULT_STDLIB STDLIB_PATH
#else
#define DEFAULT_STDLIB ""
#endif

namespace {

constexpr int EXIT_OK = 0;
constexpr int EXIT_USAGE = 64;
constexpr int EXIT_RESOLVE = 65;
constexpr int EXIT_RUNTIME = 70;

struct CliOptions {
    bool showHelp{false};
    bool dryRun{false};
    bool bytecodeOnly{false};
    bool trace{false};
    bool repl{false};
    std::string moduleName;
    std::string scriptPath;
    std::string stdLibPath;
    std::vector<std::string> searchPaths;
};

static std::string baseName(const std::string& path) {
    std::string::size_type p = path.find_last_of("/\\");
    if (p == std::string::npos) return path;
    return path.substr(p + 1);
}

static std::string moduleNameFromPath(const std::string& path) {
    std::string name = baseName(path);
    if (name.size() >= 3 && name.compare(name.size() - 3, 3, ".py") == 0)
        name.resize(name.size() - 3);
    return name;
}

static std::string dirName(const std::string& path) {
    std::string::size_type p = path.find_last_of("/\\");
    if (p == std::string::npos) return ".";
    return path.substr(0, p);
}

static bool fileExists(const std::string& path) {
    if (path.empty()) return false;
    return ::access(path.c_str(), F_OK) == 0;
}

static bool moduleExists(const std::string& moduleName,
                         const std::string& stdLibPath,
                         const std::vector<std::string>& searchPaths) {
    std::vector<std::string> bases;
    if (!stdLibPath.empty()) bases.push_back(stdLibPath);
    bases.insert(bases.end(), searchPaths.begin(), searchPaths.end());

    std::string relPath = moduleName;
    std::replace(relPath.begin(), relPath.end(), '.', '/');
    for (const auto& base : bases) {
        std::string candidate = base + "/" + relPath + ".py";
        if (fileExists(candidate)) return true;
        std::string pkgInit = base + "/" + relPath + "/__init__.py";
        if (fileExists(pkgInit)) return true;
    }
    return false;
}

static void printUsage(const char* prog) {
    std::cout << "protopy 0.1.0 - minimal runtime (execution stubbed)\n"
                 "Usage:\n"
                 "  " << prog << " --module <name> [--path <search_path>]...\n"
                 "  " << prog << " --script <script.py> [--path <search_path>]...\n"
                 "  " << prog << " [module_name|script.py]\n"
                 "Options:\n"
                 "  --stdlib <path>   Override stdlib location (defaults to build-time path)\n"
                 "  --path <path>     Append additional module search path (repeatable)\n"
                 "  --dry-run         Validate inputs but skip environment initialization\n"
                 "  --bytecode-only   Stub: validate bytecode loading path (no execution)\n"
                 "  --trace           Enable tracing (stub)\n"
                 "  --repl            Interactive REPL (stub)\n"
                 "  --help            Show this help message\n";
}

static bool parseArgs(int argc, char* argv[], CliOptions& opts, std::string& error) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            opts.showHelp = true;
            return true;
        } else if (arg == "--module") {
            if (i + 1 >= argc) {
                error = "--module requires a value";
                return false;
            }
            opts.moduleName = argv[++i];
        } else if (arg == "--script") {
            if (i + 1 >= argc) {
                error = "--script requires a value";
                return false;
            }
            opts.scriptPath = argv[++i];
        } else if (arg == "--stdlib") {
            if (i + 1 >= argc) {
                error = "--stdlib requires a value";
                return false;
            }
            opts.stdLibPath = argv[++i];
        } else if (arg == "--dry-run") {
            opts.dryRun = true;
        } else if (arg == "--bytecode-only") {
            opts.bytecodeOnly = true;
        } else if (arg == "--path") {
            if (i + 1 >= argc) {
                error = "--path requires a value";
                return false;
            }
            opts.searchPaths.push_back(argv[++i]);
        } else if (!arg.empty() && arg[0] == '-') {
            error = "Unknown option: " + arg;
            return false;
        } else if (opts.moduleName.empty() && opts.scriptPath.empty()) {
            // Positional target for backwards compatibility.
            if (arg.find('/') != std::string::npos || arg.find('\\') != std::string::npos ||
                (arg.size() >= 3 && arg.compare(arg.size() - 3, 3, ".py") == 0)) {
                opts.scriptPath = arg;
            } else {
                opts.moduleName = arg;
            }
        } else {
            error = "Unexpected positional argument: " + arg;
            return false;
        }
    }
    if (!opts.moduleName.empty() && !opts.scriptPath.empty()) {
        error = "Specify either --module or --script, not both";
        return false;
    }
    return true;
}

int executeModule(protoPython::PythonEnvironment& env, const std::string& moduleName) {
    int ret = env.executeModule(moduleName);
    if (ret == -1) {
        std::cerr << "protopy: could not resolve module '" << moduleName << "'" << std::endl;
        return EXIT_RESOLVE;
    }
    if (ret == -2) {
        std::cerr << "protopy: module '" << moduleName << "' exited with runtime error" << std::endl;
        return EXIT_RUNTIME;
    }
    if (ret == -3)
        return env.getExitRequested();
    return EXIT_OK;
}

} // namespace

int main(int argc, char* argv[]) {
    CliOptions options;
    std::string parseError;
    if (!parseArgs(argc, argv, options, parseError)) {
        std::cerr << "protopy: " << parseError << std::endl;
        printUsage(argv[0]);
        return EXIT_USAGE;
    }

    if (options.showHelp) {
        printUsage(argv[0]);
        return EXIT_OK;
    }

    std::string stdLibPath = !options.stdLibPath.empty() ? options.stdLibPath : DEFAULT_STDLIB;
    std::vector<std::string> searchPaths = {"."};
    searchPaths.insert(searchPaths.end(), options.searchPaths.begin(), options.searchPaths.end());

    if (options.repl) {
        protoPython::PythonEnvironment env(stdLibPath, searchPaths, std::vector<std::string>());
        if (options.trace) {
            env.setExecutionHook([](const std::string& name, int phase) {
                std::cerr << (phase == 0 ? "[trace] enter " : "[trace] leave ") << name << std::endl;
            });
        }
        std::cout << "protoPython REPL - type 'exit' to quit\n>>> ";
        std::string line;
        while (std::getline(std::cin, line) && line != "exit") {
            if (line.empty()) { std::cout << ">>> "; continue; }
            const proto::ProtoObject* mod = env.resolve(line);
            if (mod && mod != PROTO_NONE) {
                int ret = env.executeModule(line);
                (void)ret;
            }
            std::cout << ">>> ";
        }
        return EXIT_OK;
    }

    if (options.moduleName.empty() && options.scriptPath.empty()) {
        printUsage(argv[0]);
        return EXIT_USAGE;
    }

    std::vector<std::string> argvVec;
    for (int i = 0; i < argc; ++i) argvVec.push_back(argv[i]);

    if (!options.scriptPath.empty()) {
        std::vector<std::string> scriptPaths = searchPaths;
        scriptPaths.insert(scriptPaths.begin(), dirName(options.scriptPath));
        if (options.dryRun || options.bytecodeOnly) {
            return fileExists(options.scriptPath) ? EXIT_OK : EXIT_RESOLVE;
        }
        protoPython::PythonEnvironment envWithPath(stdLibPath, scriptPaths, argvVec);
        std::string moduleName = moduleNameFromPath(options.scriptPath);
        return executeModule(envWithPath, moduleName);
    }

    if (options.dryRun || options.bytecodeOnly) {
        return moduleExists(options.moduleName, stdLibPath, searchPaths) ? EXIT_OK : EXIT_RESOLVE;
    }

    protoPython::PythonEnvironment env(stdLibPath, searchPaths, argvVec);
    if (options.trace) {
        env.setExecutionHook([](const std::string& name, int phase) {
            std::cerr << (phase == 0 ? "[trace] enter " : "[trace] leave ") << name << std::endl;
        });
        env.enableDefaultTrace();
    }
    return executeModule(env, options.moduleName);
}
