/**
 * protopy - Minimal Python runtime entrypoint.
 * Creates PythonEnvironment and resolves a module or script path (execution stubbed).
 */

#include <protoPython/PythonEnvironment.h>
#include <iostream>
#include <string>
#include <vector>

#ifdef STDLIB_PATH
#define DEFAULT_STDLIB STDLIB_PATH
#else
#define DEFAULT_STDLIB ""
#endif

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

int main(int argc, char* argv[]) {
    std::string stdLibPath = DEFAULT_STDLIB;
    std::vector<std::string> searchPaths = {"."};

    // Optional: first arg as script path or module name
    std::string target;
    if (argc >= 2)
        target = argv[1];

    protoPython::PythonEnvironment env(stdLibPath, searchPaths);

    if (!target.empty()) {
        std::string moduleName;
        if (target.find('/') != std::string::npos || target.find('\\') != std::string::npos ||
            (target.size() >= 3 && target.compare(target.size() - 3, 3, ".py") == 0)) {
            searchPaths.insert(searchPaths.begin(), dirName(target));
            protoPython::PythonEnvironment envWithPath(stdLibPath, searchPaths);
            moduleName = moduleNameFromPath(target);
            const proto::ProtoObject* mod = envWithPath.resolve(moduleName);
            if (mod == nullptr || mod == PROTO_NONE) {
                std::cerr << "protopy: could not resolve module '" << moduleName << "' for " << target << std::endl;
                return 1;
            }
            int ret = envWithPath.runModuleMain(moduleName);
            if (ret != 0) return ret;
            return 0;
        }
        moduleName = target;
        const proto::ProtoObject* mod = env.resolve(moduleName);
        if (mod == nullptr || mod == PROTO_NONE) {
            std::cerr << "protopy: could not resolve module '" << moduleName << "'" << std::endl;
            return 1;
        }
        int ret = env.runModuleMain(moduleName);
        if (ret != 0) return ret;
        return 0;
    }

    // No argument: print short usage / version
    std::cout << "protopy 0.1.0 - minimal runtime (execution stubbed). Usage: protopy [module_name|script.py]" << std::endl;
    return 0;
}
