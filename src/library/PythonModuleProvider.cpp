#include <protoPython/PythonEnvironment.h>
#include <protoPython/PythonModuleProvider.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>
#include <iostream>
#include <vector>

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

/**
 * The directories to search, in order: the live `sys.path` first, then the
 * base paths this provider was built with.
 *
 * `sys.path` is read on every load because a program may extend it at runtime
 * (`sys.path.insert(0, d)` before an import, the documented way to import from
 * a directory decided while running). The provider used to walk only the base
 * paths captured when the environment was created, so such an entry was
 * ignored and the import failed with ModuleNotFoundError.
 *
 * The base paths stay as a fallback: they are also appended to `sys.path` at
 * startup, but a program is free to reassign or clear `sys.path`, and the
 * standard library must remain importable when it does.
 */
static std::vector<std::string> searchDirectories(const std::vector<std::string>& basePaths,
                                                  proto::ProtoContext* ctx) {
    std::vector<std::string> paths;

    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* sysModule = env ? env->getSysModule() : nullptr;
    if (env && sysModule && sysModule != PROTO_NONE) {
        const proto::ProtoObject* pathObj =
            sysModule->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "path"));
        const proto::ProtoList* entries = nullptr;
        if (pathObj && pathObj != PROTO_NONE) {
            // sys.path is a Python list: the elements live in its __data__
            // payload. A bare ProtoList is accepted too.
            const proto::ProtoObject* data = pathObj->getAttribute(ctx, env->getDataString());
            if (data && data->asList(ctx)) entries = data->asList(ctx);
            else if (pathObj->asList(ctx)) entries = pathObj->asList(ctx);
        }
        for (unsigned long i = 0; entries && i < entries->getSize(ctx); ++i) {
            const proto::ProtoObject* entry = entries->getAt(ctx, static_cast<int>(i));
            if (!entry || entry == PROTO_NONE) continue;
            // A str subclass keeps its text in __data__, like every wrapped
            // built-in; a plain str is the string itself.
            if (!entry->isString(ctx)) {
                const proto::ProtoObject* d = entry->getAttribute(ctx, env->getDataString());
                entry = (d && d->isString(ctx)) ? d : nullptr;
            }
            if (!entry) continue;
            std::string text;
            entry->asString(ctx)->toUTF8String(ctx, text);
            if (!text.empty()) paths.push_back(text);
        }
    }

    for (const auto& basePath : basePaths) {
        if (std::find(paths.begin(), paths.end(), basePath) == paths.end()) {
            paths.push_back(basePath);
        }
    }
    return paths;
}

const proto::ProtoObject* PythonModuleProvider::tryLoad(const std::string& logicalPath, proto::ProtoContext* ctx) {
    // 1. Convert dotted path to slash path
    std::string relPath = logicalPath;
    std::replace(relPath.begin(), relPath.end(), '.', '/');

    const proto::ProtoString* nameKey = proto::ProtoString::createSymbol(ctx, "__name__");
    const proto::ProtoString* fileKey = proto::ProtoString::createSymbol(ctx, "__file__");
    const proto::ProtoString* pathKey = proto::ProtoString::createSymbol(ctx, "__path__");

    for (const auto& basePath : searchDirectories(basePaths_, ctx)) {
        // 2. Try <base>/<path>.py
        std::string pyPath = joinPath(basePath, relPath + ".py");
        if (protoPython::diagResolveEnabled()) {
            fprintf(stderr, "DEBUG: tryLoad(base=%s, rel=%s) checking: %s\n", 
                    basePath.c_str(), relPath.c_str(), pyPath.c_str());
        }
        if (fileExists(pyPath)) {
            if (protoPython::diagResolveEnabled()) fprintf(stderr, "DEBUG: tryLoad FOUND file: %s\n", pyPath.c_str());
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
            if (protoPython::diagResolveEnabled()) fprintf(stderr, "DEBUG: tryLoad FOUND package: %s\n", initPath.c_str());
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
