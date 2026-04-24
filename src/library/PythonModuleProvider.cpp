#include <protoPython/PythonEnvironment.h>
#include <protoPython/PythonModuleProvider.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <iostream>

namespace protoPython {

namespace {

std::string joinPath(const std::string& base, const std::string& logicalPath) {
    if (base.empty()) return logicalPath;
    if (logicalPath.empty()) return base;
    bool baseEndsWithSep = !base.empty() && (base.back() == '/' || base.back() == '\\');
    if (baseEndsWithSep) return base + logicalPath;
    bool pathStartsWithSep = !logicalPath.empty() && (logicalPath[0] == '/' || logicalPath[0] == '\\');
    if (pathStartsWithSep) return base + logicalPath;
    return base + "/" + logicalPath;
}

bool fileExists(const std::string& path) {
    struct stat st;
    int res = stat(path.c_str(), &st);
    return res == 0 && S_ISREG(st.st_mode);
}

} // anonymous namespace

PythonModuleProvider::PythonModuleProvider(std::vector<std::string> basePaths)
    : basePaths_(std::move(basePaths)), guid_("protoPython.stdlib"), alias_("python_stdlib") {}

const proto::ProtoObject* PythonModuleProvider::tryLoad(const std::string& logicalPath, proto::ProtoContext* ctx) {
    // 1. Convert dotted path to slash path
    std::string relPath = logicalPath;
    std::replace(relPath.begin(), relPath.end(), '.', '/');

    const proto::ProtoString* nameKey = proto::ProtoString::createSymbol(ctx, "__name__");
    const proto::ProtoString* fileKey = proto::ProtoString::createSymbol(ctx, "__file__");
    const proto::ProtoString* pathKey = proto::ProtoString::createSymbol(ctx, "__path__");

    for (const auto& basePath : basePaths_) {
        // 2. Try <base>/<path>.py
        std::string pyPath = joinPath(basePath, relPath + ".py");
        if (std::getenv("PROTO_RESOLVE_DIAG")) {
            fprintf(stderr, "DEBUG: tryLoad(base=%s, rel=%s) checking: %s\n", 
                    basePath.c_str(), relPath.c_str(), pyPath.c_str());
        }
        if (fileExists(pyPath)) {
            if (std::getenv("PROTO_RESOLVE_DIAG")) fprintf(stderr, "DEBUG: tryLoad FOUND file: %s\n", pyPath.c_str());
            /* Mutable so exec() STORE_NAME updates the same module object (proto setAttribute on mutable returns this). */
            const proto::ProtoObject* module = ctx->newObject(false);
            if (ctx->space->objectPrototype) {
                module = module->addParent(ctx, ctx->space->objectPrototype);
            }
            module = module->setAttribute(ctx, nameKey, PythonEnvironment::getInternedString(ctx, logicalPath.c_str() )->asObject(ctx));
            module = module->setAttribute(ctx, fileKey, PythonEnvironment::getInternedString(ctx, pyPath.c_str() )->asObject(ctx));
            return module;
        }

        // 3. Try <base>/<path>/__init__.py (package)
        std::string initPath = joinPath(basePath, relPath + "/__init__.py");
        if (fileExists(initPath)) {
            if (std::getenv("PROTO_RESOLVE_DIAG")) fprintf(stderr, "DEBUG: tryLoad FOUND package: %s\n", initPath.c_str());
            const proto::ProtoObject* module = ctx->newObject(false);
            if (ctx->space->objectPrototype) {
                module = module->addParent(ctx, ctx->space->objectPrototype);
            }
            module = module->setAttribute(ctx, nameKey, PythonEnvironment::getInternedString(ctx, logicalPath.c_str() )->asObject(ctx));
            module = module->setAttribute(ctx, fileKey, PythonEnvironment::getInternedString(ctx, initPath.c_str() )->asObject(ctx));
            
            const proto::ProtoList* pkgPath = ctx->newList();
            pkgPath = pkgPath->appendLast(ctx, PythonEnvironment::getInternedString(ctx, joinPath(basePath, relPath).c_str())->asObject(ctx));
            module = module->setAttribute(ctx, pathKey, pkgPath->asObject(ctx));
            return module;
        }
    }

    return PROTO_NONE;
}

} // namespace protoPython
