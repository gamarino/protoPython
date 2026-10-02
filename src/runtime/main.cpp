/**
 * protopy - Python runtime entrypoint.
 * Creates a PythonEnvironment and executes a module, a script, a -c command or the REPL.
 */

#include <protoPython/PythonEnvironment.h>
#include <protoPython/IOModule.h>
#include <protoPython/DiagUtils.h>
#include <protoPython/Version.h>
#include <protoCore.h>
#include <algorithm>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <sstream>
#include <string>
#include <vector>
#if !defined(_WIN32)
#include <unistd.h>
#endif

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
#include <fcntl.h>
#include <io.h>
#include <windows.h>
#include "../library/PosixCompat.h"
#endif
#ifdef __APPLE__
#include <mach-o/dyld.h>
#endif

namespace {

#ifdef _WIN32
static void usleep(unsigned int microseconds) {
    Sleep(microseconds / 1000);
}
#endif

#ifdef _WIN32
// The console's code pages before protopy switched them to UTF-8; restored
// when the process exits (normally, through sys.exit or an uncaught
// exception, or on Ctrl+C / Ctrl+Break / closing the console window), so the
// console is left as protopy found it. os._exit() ends the process without
// running exit handlers and leaves UTF-8 selected.
static UINT g_savedConsoleInputCP = 0;
static UINT g_savedConsoleOutputCP = 0;

static void restoreConsoleCodePages() {
    if (g_savedConsoleOutputCP != 0) SetConsoleOutputCP(g_savedConsoleOutputCP);
    if (g_savedConsoleInputCP != 0) SetConsoleCP(g_savedConsoleInputCP);
}

static BOOL WINAPI restoreConsoleOnControlEvent(DWORD) {
    restoreConsoleCodePages();
    return FALSE;  // the default handling (ending the process) follows
}

#endif

// Windows: the standard streams carry exactly the bytes the program writes, as
// on Linux and macOS (no "\n" -> "\r\n" translation), and a console shows and
// reads them as UTF-8. The process code page is UTF-8 through the manifest
// (src/windows/utf8.manifest), so argv, getenv and paths are UTF-8 too.
static void prepareStandardStreams() {
#ifdef _WIN32
    // protopy.exe reports invalid C runtime arguments as errors on every
    // thread, the collector's included; the library does it only on the
    // threads that run Python code (PosixCompat.h).
    protopy_ignore_invalid_parameters_process_wide();
    _setmode(_fileno(stdin), _O_BINARY);
    _setmode(_fileno(stdout), _O_BINARY);
    _setmode(_fileno(stderr), _O_BINARY);
    g_savedConsoleOutputCP = GetConsoleOutputCP();  // 0 without a console
    g_savedConsoleInputCP = GetConsoleCP();
    if (g_savedConsoleOutputCP != 0 || g_savedConsoleInputCP != 0) {
        std::atexit(restoreConsoleCodePages);
        SetConsoleCtrlHandler(restoreConsoleOnControlEvent, TRUE);
        SetConsoleOutputCP(CP_UTF8);
        SetConsoleCP(CP_UTF8);
    }
#endif
    // std::cin (the REPL's input) reads through sys.stdin's buffer, so the
    // REPL, input() and sys.stdin consume standard input in order; on a
    // Windows console it reads with ReadConsoleW (IOModule.cpp).
    std::cin.rdbuf(protoPython::io::standardInputBuffer());
}

// The separator of PROTO_PYTHONPATH's directory list: ';' on Windows, where
// ':' follows a drive letter.
#ifdef _WIN32
constexpr char kPathListSeparator = ';';
#else
constexpr char kPathListSeparator = ':';
#endif

static std::string getExecutablePath() {
#ifdef __linux__
    char result[PATH_MAX];
    ssize_t count = readlink("/proc/self/exe", result, PATH_MAX);
    if (count > 0) return std::string(result, count);
#elif defined(_WIN32)
    return protopy_executable_path();
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
    bool safePath{false};  // -P (or -I): nothing prepended to sys.path
    std::string moduleName;
    std::string scriptPath;
    std::string commandLine;
    std::string stdLibPath;
    std::vector<std::string> searchPaths;
    std::vector<std::string> targetArgs;
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

/**
 * Chooses the standard library directory, in this order:
 *  1. --stdlib, as given (a relative value is relative to the working directory);
 *  2. STDLIB_PATH, the installed location: used as is when absolute, otherwise
 *     taken relative to the executable's directory, never to the working directory;
 *  3. STDLIB_BUILD_PATH, the source tree's copy, for a binary run from its build tree.
 * Prints a warning and returns an empty string when none of them exists.
 */
static std::string resolveStdLibPath(const std::string& cliPath, const std::string& exeDir) {
    namespace fs = std::filesystem;
    if (!cliPath.empty()) return cliPath;
    std::error_code ec;
    const fs::path installed(DEFAULT_STDLIB);
    if (!installed.empty()) {
        fs::path candidate = installed;
        if (candidate.is_relative()) candidate = exeDir.empty() ? fs::path() : fs::path(exeDir) / installed;
        if (!candidate.empty() && fs::is_directory(candidate, ec)) return candidate.lexically_normal().string();
    }
#ifdef STDLIB_BUILD_PATH
    if (fs::is_directory(fs::path(STDLIB_BUILD_PATH), ec)) return STDLIB_BUILD_PATH;
#endif
    std::cerr << "protopy: standard library not found; use --stdlib <dir>" << std::endl;
    return "";
}

static void printUsage(const char* prog) {
    std::cout << "protopy " PROTOPYTHON_VERSION " - protoPython runtime\n"
                 "Usage:\n"
                 "  " << prog << " [-m <name> | --module <name>] [-p <path> | --path <path>]...\n"
                 "  " << prog << " [-c <cmd>] [-p <path> | --path <path>]...\n"
                 "  " << prog << " [--script <script.py>] [--path <path>]...\n"
                 "  " << prog << " [module_name|script.py]\n"
                 "Options:\n"
                 "  -c <command>      Execute Python program passed as string\n"
                 "  -m <module-name>  Execute module as a script\n"
                 "  -P                Don't prepend a potentially unsafe path to sys.path\n"
                 "  -p, --path <path> Append additional module search path (repeatable)\n"
                 "  --stdlib <path>   Override stdlib location (defaults to build-time path)\n"
                 "  --dry-run         Validate inputs but skip environment initialization\n"
                 "  --bytecode-only   Stub: validate bytecode loading path (no execution)\n"
                 "  --trace           Print module enter/leave events to stderr\n"
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
            for (int j = i + 1; j < argc; ++j) opts.targetArgs.push_back(argv[j]);
            break;
        } else if (arg == "-c") {
            if (i + 1 >= argc) {
                error = "-c requires a value";
                return false;
            }
            opts.commandLine = argv[++i];
            for (int j = i + 1; j < argc; ++j) opts.targetArgs.push_back(argv[j]);
            break;
        } else if (arg == "--script") {
            if (i + 1 >= argc) {
                error = "--script requires a value";
                return false;
            }
            opts.scriptPath = argv[++i];
            for (int j = i + 1; j < argc; ++j) opts.targetArgs.push_back(argv[j]);
            break;
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
        } else if (arg == "-P") {
            opts.safePath = true;
        } else if (arg == "-I" || arg == "-E" || arg == "-S" || arg == "-s"
                || arg == "-O" || arg == "-OO" || arg == "-B" || arg == "-q"
                || arg == "-u" || arg == "-b" || arg == "-bb" || arg == "-d"
                || arg == "-v" || arg == "-vv" || arg == "-vvv") {
            // STRUCT-309: accept CPython interpreter flags as no-ops.
            // assert_python_ok / test.support.script_helper passes
            // `-I -X faulthandler` to every subprocess it spawns; if
            // we error on these, every stdlib test that uses
            // assert_python_* fails before its script even runs.
            // The flags are accepted but have no effect on the
            // protoPython interpreter (it has no PYTHONHOME hook to
            // ignore, no .pth files to suppress, no -O bytecode
            // optimisation level, etc.). -I implies -P, as in CPython.
            if (arg == "-I") opts.safePath = true;
        } else if (arg == "-X" || arg == "-W") {
            // -X opt and -W warning_filter both take one value argument.
            // Accept and discard (warnings filter is applied at the
            // warnings module level, not via CLI on protopy yet).
            if (i + 1 < argc) ++i;
        } else if (arg.size() > 2 && arg[0] == '-'
                && (arg[1] == 'X' || arg[1] == 'W')) {
            // -Xfoo / -Wfoo (no space) — also no-ops here.
            (void)arg;
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
            for (int j = i + 1; j < argc; ++j) opts.targetArgs.push_back(argv[j]);
            break;
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

// Waits for the program's worker threads (at most 5 s), so the environment
// stays alive while they run, and then runs the atexit handlers: CPython runs
// them once, at interpreter shutdown, whether the program ended normally,
// through SystemExit or with an unhandled exception (which is reported first).
void finishInterpreter(protoPython::PythonEnvironment& env) {
    auto* space = env.getSpace();
    if (space) {
        int count = 0;
        while (space->runningThreads.load() > 1 && count < 100) {
            usleep(50000);
            count++;
        }
    }
    env.runExitHandlers();
}

int executeModule(protoPython::PythonEnvironment& env, const std::string& moduleName, bool asMain = false) {
    int ret = env.executeModule(moduleName, asMain);
    if (ret == -1) {
        std::cerr << "protopy: could not resolve module '" << moduleName << "'" << std::endl;
        return EXIT_RESOLVE;
    }
    if (ret == -2) {
        const proto::ProtoObject* exc = env.takePendingException();
        if (exc && exc != PROTO_NONE) {
            std::cerr << "protopy: unhandled exception during execution of '" << moduleName << "':\n";
            env.handleException(exc, nullptr, std::cerr);
        } else {
            std::cerr << "protopy: module '" << moduleName << "' exited with runtime error" << std::endl;
        }
        finishInterpreter(env);
        return EXIT_RUNTIME;
    }
    finishInterpreter(env);
    if (ret == -3)
        return env.getExitRequested();
    return EXIT_OK;
}

} // namespace

int main(int argc, char* argv[]) {
    prepareStandardStreams();
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

    // PROTO_PYTHONPATH: extra module search directories, separated by ':'
    // (';' on Windows). Empty entries are ignored.
    if (const char* pathEnv = std::getenv("PROTO_PYTHONPATH")) {
        const std::string paths = pathEnv;
        size_t start = 0;
        while (start <= paths.size()) {
            size_t end = paths.find(kPathListSeparator, start);
            if (end == std::string::npos) end = paths.size();
            if (end > start) options.searchPaths.push_back(paths.substr(start, end - start));
            start = end + 1;
        }
    }

    const std::string exePath = getExecutablePath();
    const std::string stdLibPath = resolveStdLibPath(options.stdLibPath, exePath.empty() ? "" : dirName(exePath));

    if (get_env_diag()) {
        fprintf(stderr, "DEBUG MAIN: Resolved stdLibPath: %s\n", stdLibPath.c_str());
        fflush(stderr);
    }

    // sys.path, as CPython 3.14 orders it: sys.path[0] (below), the -p and
    // PROTO_PYTHONPATH directories (PYTHONPATH's place), the standard library,
    // the user's site-packages. The environment is also given the directories
    // as search paths: the module providers' fallback, and the only paths of
    // the compiled and HPy extension providers.
    std::vector<std::string> sitePaths;
#ifdef __linux__
    const char* home = std::getenv("HOME");
    if (home) {
        std::string userLib = std::string(home) + "/.local/lib/python3.14/site-packages";
        sitePaths.push_back(userLib);
    }
#endif
    // sys.path[0]: '' for -c and the REPL (the current directory when each
    // import runs), the current directory for -m, the script's directory for
    // a script; nothing with -P (safe path).
    auto beforeStdlib = [&](const std::string& path0) {
        std::vector<std::string> before;
        if (!options.safePath) before.push_back(path0);
        before.insert(before.end(), options.searchPaths.begin(), options.searchPaths.end());
        return before;
    };
    std::error_code cwdError;
    const std::string cwd = std::filesystem::current_path(cwdError).string();

    std::vector<std::string> searchPaths = sitePaths;
    searchPaths.insert(searchPaths.end(), options.searchPaths.begin(), options.searchPaths.end());

    std::vector<std::string> argvVec;
    if (!options.commandLine.empty()) argvVec.push_back("-c");
    else if (!options.scriptPath.empty()) argvVec.push_back(options.scriptPath);
    else if (!options.moduleName.empty()) argvVec.push_back(options.moduleName);
    else argvVec.push_back("");
    
    argvVec.insert(argvVec.end(), options.targetArgs.begin(), options.targetArgs.end());


    if (options.repl) {
        std::vector<std::string> replPaths = options.searchPaths; // Use options.searchPaths which now includes PROTO_PYTHONPATH
        if (!options.safePath) replPaths.insert(replPaths.begin(), cwd);
        protoPython::PythonEnvironment env(stdLibPath, replPaths, argvVec);
        env.setSysPath(beforeStdlib(""), {});
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
        if (!options.safePath) cmdPaths.insert(cmdPaths.begin(), cwd);
        protoPython::PythonEnvironment env(stdLibPath, cmdPaths, argvVec);
        env.setSysPath(beforeStdlib(""), sitePaths);
        if (options.trace) {
            env.setExecutionHook([](const std::string& name, int phase) {
                std::cerr << (phase == 0 ? "[trace] enter " : "[trace] leave ") << name << std::endl;
            });
            env.enableDefaultTrace();
        }
        int ret = env.executeString(options.commandLine, "<string>");

        if (ret == -2) {
            const proto::ProtoObject* exc = env.takePendingException();
            if (exc && exc != PROTO_NONE) {
                // SystemExit writes nothing here and sets the exit status,
                // as in script execution (PythonEnvironment::executeModule).
                std::ostringstream excOut;
                env.handleException(exc, nullptr, excOut);
                if (excOut.str().empty()) {
                    finishInterpreter(env);
                    return env.getExitRequested();
                }
                std::cerr << "protopy: unhandled exception in -c execution:\n" << excOut.str();
            }
            finishInterpreter(env);
            return EXIT_RUNTIME;
        }
        finishInterpreter(env);
        return EXIT_OK;
    }

    if (options.moduleName.empty() && options.scriptPath.empty()) {
        printUsage(argv[0]);
        return EXIT_USAGE;
    }

    if (!options.scriptPath.empty()) {
        std::vector<std::string> scriptPaths = searchPaths;
        // The script is found through its directory made absolute, so its
        // __file__ is absolute, as CPython's __main__.__file__ since 3.9:
        // the current directory joined with the path as given, without
        // resolving `..` or links on POSIX (std::filesystem::absolute, like
        // CPython's _Py_abspath), normalized on Windows (GetFullPathNameW in
        // both). sys.argv[0] keeps the path as given.
        std::error_code absError;
        const std::filesystem::path absScript =
            std::filesystem::absolute(std::filesystem::path(options.scriptPath), absError);
        // The script is found through the module search: its directory as
        // given stays a search path even with -P.
        scriptPaths.insert(scriptPaths.begin(),
                           dirName(absError ? options.scriptPath : absScript.string()));
        // sys.path[0] is the script's directory with symbolic links resolved
        // (realpath on POSIX, as CPython; GetFullPathNameW on Windows).
        std::string scriptDir = dirName(absError ? options.scriptPath : absScript.string());
#if !defined(_WIN32)
        std::error_code realError;
        const std::filesystem::path realScript = std::filesystem::canonical(absScript, realError);
        if (!absError && !realError) scriptDir = realScript.parent_path().string();
#endif
        if (options.dryRun || options.bytecodeOnly) {
            return fileExists(options.scriptPath) ? EXIT_OK : EXIT_RESOLVE;
        }
        protoPython::PythonEnvironment envWithPath(stdLibPath, scriptPaths, argvVec);
        envWithPath.setSysPath(beforeStdlib(scriptDir), sitePaths);
        if (options.trace) {
            envWithPath.setExecutionHook([](const std::string& name, int phase) {
                std::cerr << (phase == 0 ? "[trace] enter " : "[trace] leave ") << name << std::endl;
            });
            envWithPath.enableDefaultTrace();
        }
        std::string moduleName = moduleNameFromPath(options.scriptPath);
        return executeModule(envWithPath, moduleName, true);
    }

    // -m: the current directory is searched first.
    if (!options.safePath) searchPaths.insert(searchPaths.begin(), cwd);
    if (options.dryRun || options.bytecodeOnly) {
        return moduleExists(options.moduleName, stdLibPath, searchPaths) ? EXIT_OK : EXIT_RESOLVE;
    }

    protoPython::PythonEnvironment env(stdLibPath, searchPaths, argvVec);
    env.setSysPath(beforeStdlib(cwd), sitePaths);
    if (options.trace) {
        env.setExecutionHook([](const std::string& name, int phase) {
            std::cerr << (phase == 0 ? "[trace] enter " : "[trace] leave ") << name << std::endl;
        });
        env.enableDefaultTrace();
    }
    return executeModule(env, options.moduleName, true);
}
