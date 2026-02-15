/**
 * protopy - Minimal Python runtime entrypoint.
 * Creates PythonEnvironment and resolves a module or script path (execution stubbed).
 */

#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>
#include <proto_internal.h>
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <string>
#include <vector>
#include <unistd.h>

#ifdef STDLIB_PATH
#define DEFAULT_STDLIB STDLIB_PATH
#else
#define DEFAULT_STDLIB ""
#endif

#include <limits.h>
#ifdef __linux__
#include <unistd.h>
#endif
#ifdef _WIN32
#include <windows.h>
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace {

static std::string getExecutablePath() {
#ifdef __linux__
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    if (count > 0) return std::string(result, count);
#elif defined(_WIN32)
    char result[MAX_PATH];
    DWORD count = GetModuleFileNameA(NULL, result, MAX_PATH);
    if (count > 0) return std::string(result, count);
#elif defined(__APPLE__)
    char result[PATH_MAX];
    uint32_t size = sizeof(result);
    if (_NSGetExecutablePath(result, &size) == 0)
        return std::string(result);
#endif
    return "";
}

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
    std::string commandLine;
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
                 "  " << prog << " [-m <name> | --module <name>] [-p <path> | --path <path>]...\n"
                 "  " << prog << " [-c <cmd>] [-p <path> | --path <path>]...\n"
                 "  " << prog << " [--script <script.py>] [--path <path>]...\n"
                 "  " << prog << " [module_name|script.py]\n"
                 "Options:\n"
                 "  -c <command>      Execute Python program passed as string\n"
                 "  -m <module-name>  Execute module as a script\n"
                 "  -p, --path <path> Append additional module search path (repeatable)\n"
                 "  --stdlib <path>   Override stdlib location (defaults to build-time path)\n"
                 "  --dry-run         Validate inputs but skip environment initialization\n"
                 "  --bytecode-only   Stub: validate bytecode loading path (no execution)\n"
                 "  --trace           Enable tracing (stub)\n"
                 "  --repl, -i        Interactive REPL\n"
                 "  --help, -h        Show this help message\n";
}

static bool parseArgs(int argc, char* argv[], CliOptions& opts, std::string& error) {
    for (int i = 1; i < argc; ++i) {
        std::string arg = argv[i];
        if (arg == "--help" || arg == "-h") {
            opts.showHelp = true;
            return true;
        } else if (arg == "--module" || arg == "-m") {
            if (i + 1 >= argc) {
                error = arg + " requires a value";
                return false;
            }
            opts.moduleName = argv[++i];
        } else if (arg == "-c") {
            if (i + 1 >= argc) {
                error = "-c requires a value";
                return false;
            }
            opts.commandLine = argv[++i];
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
        } else if (arg == "--trace") {
            opts.trace = true;
        } else if (arg == "--repl" || arg == "-i") {
            opts.repl = true;
        } else if (arg == "--path" || arg == "-p") {
            if (i + 1 >= argc) {
                error = arg + " requires a value";
                return false;
            }
            opts.searchPaths.push_back(argv[++i]);
        } else if (!arg.empty() && arg[0] == '-') {
            error = "Unknown option: " + arg;
            return false;
        } else if (opts.moduleName.empty() && opts.scriptPath.empty() && opts.commandLine.empty()) {
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
    int targets = (!opts.moduleName.empty()) + (!opts.scriptPath.empty()) + (!opts.commandLine.empty());
    if (targets > 1) {
        error = "Specify only one of -c, -m, or script path";
        return false;
    }
    return true;
}

int executeModule(protoPython::PythonEnvironment& env, const std::string& moduleName, bool asMain = false) {
    int ret = env.executeModule(moduleName, asMain);
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

    // Wait for worker threads to finish so env stays alive during their execution
    auto* space = env.getSpace();
    if (space) {
        int count = 0;
        while (space->runningThreads.load() > 1 && count < 100) { // Max 5s wait for stress tests
            usleep(50000);
            count++;
        }
    }

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
    std::string exePath = getExecutablePath();
    std::string exeDir = exePath.empty() ? "." : dirName(exePath);

    std::string stdLibPath = !options.stdLibPath.empty() ? options.stdLibPath : DEFAULT_STDLIB;
    
    bool resolved = false;
    if (!stdLibPath.empty()) {
        if (stdLibPath[0] == '/' || stdLibPath.find(":\\") != std::string::npos) {
            resolved = true;
        } else if (std::filesystem::exists(stdLibPath)) {
            resolved = true;
        } else {
            std::string altPath = exeDir + "/" + stdLibPath;
            if (std::filesystem::exists(altPath)) {
                stdLibPath = altPath;
                resolved = true;
            }
        }
    }

    if (!resolved) {
        // Special case for development: check ./lib/python3.14
        if (std::filesystem::exists("lib/python3.14")) {
            stdLibPath = "lib/python3.14";
        }
    }

    if (std::getenv("PROTO_ENV_DIAG")) {
        std::cerr << "[proto-diag] main: stdLibPath='" << stdLibPath << "'\n";
    }

    std::vector<std::string> searchPaths;
    
    // Support user directories (e.g., ~/.local/lib/protoPython/3.14/site-packages)
#ifdef __linux__
    const char* home = std::getenv("HOME");
    if (home) {
        std::string userLib = std::string(home) + "/.local/lib/python3.14/site-packages";
        searchPaths.push_back(userLib);
    }
#endif

    searchPaths.insert(searchPaths.end(), options.searchPaths.begin(), options.searchPaths.end());

    std::vector<std::string> argvVec;
    for (int i = 0; i < argc; ++i) argvVec.push_back(argv[i]);

    if (options.repl) {
        std::vector<std::string> replPaths = searchPaths;
        replPaths.insert(replPaths.begin(), ".");
        protoPython::PythonEnvironment env(stdLibPath, replPaths, argvVec);
        if (options.trace) {
            env.setExecutionHook([](const std::string& name, int phase) {
                std::cerr << (phase == 0 ? "[trace] enter " : "[trace] leave ") << name << std::endl;
            });
            env.enableDefaultTrace();
        }
        env.runRepl(std::cin, std::cout);
        
        auto* space = env.getSpace();
        if (space) {
            int count = 0;
            while (space->runningThreads.load() > 1 && count < 100) { usleep(50000); count++; }
        }
        return EXIT_OK;
    }

    if (!options.commandLine.empty()) {
        std::vector<std::string> cmdPaths = searchPaths;
        cmdPaths.insert(cmdPaths.begin(), ".");
        protoPython::PythonEnvironment env(stdLibPath, cmdPaths, argvVec);
        if (options.trace) {
            env.setExecutionHook([](const std::string& name, int phase) {
                std::cerr << (phase == 0 ? "[trace] enter " : "[trace] leave ") << name << std::endl;
            });
            env.enableDefaultTrace();
        }
        int ret = env.executeString(options.commandLine, "<string>");
        
        auto* space = env.getSpace();
        if (space) {
            int count = 0;
            while (space->runningThreads.load() > 1 && count < 100) { usleep(50000); count++; }
        }

        if (ret == -2) return EXIT_RUNTIME;
        return EXIT_OK;
    }

    if (options.moduleName.empty() && options.scriptPath.empty()) {
        printUsage(argv[0]);
        return EXIT_USAGE;
    }

    if (!options.scriptPath.empty()) {
        std::vector<std::string> scriptPaths = searchPaths;
        scriptPaths.insert(scriptPaths.begin(), dirName(options.scriptPath));
        if (options.dryRun || options.bytecodeOnly) {
            return fileExists(options.scriptPath) ? EXIT_OK : EXIT_RESOLVE;
        }
        protoPython::PythonEnvironment envWithPath(stdLibPath, scriptPaths, argvVec);
        if (options.trace) {
            envWithPath.setExecutionHook([](const std::string& name, int phase) {
                std::cerr << (phase == 0 ? "[trace] enter " : "[trace] leave ") << name << std::endl;
            });
            envWithPath.enableDefaultTrace();
        }
        std::string moduleName = moduleNameFromPath(options.scriptPath);
        return executeModule(envWithPath, moduleName, true);
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
    return executeModule(env, options.moduleName, true);
}
