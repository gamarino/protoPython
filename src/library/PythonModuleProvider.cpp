#include <protoPython/PythonEnvironment.h>
#include <protoPython/PythonModuleProvider.h>
#include <sys/stat.h>
#if !defined(_WIN32)
#include <unistd.h>
#endif
#include <algorithm>
#include <iostream>
#include <vector>
#include "PosixCompat.h"

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

bool directoryExists(const std::string& path) {
    struct stat st;
    return stat(path.c_str(), &st) == 0 && S_ISDIR(st.st_mode);
}

// The text of a path entry: a str, or a str subclass instance whose text is
// in __data__. Empty when the entry is not a string.
std::string pathEntryText(proto::ProtoContext* ctx, PythonEnvironment* env, const proto::ProtoObject* entry) {
    if (!entry || entry == PROTO_NONE) return std::string();
    if (!entry->isString(ctx)) {
        const proto::ProtoObject* d = entry->getAttribute(ctx, env->getDataString());
        entry = (d && d->isString(ctx)) ? d : nullptr;
    }
    std::string text;
    if (entry) entry->asString(ctx)->toUTF8String(ctx, text);
    return text;
}

// The elements of a Python list (kept in its __data__ payload), of a bare
// ProtoList or of an importlib _NamespacePath (whose entries are in _path).
const proto::ProtoList* listElements(proto::ProtoContext* ctx, PythonEnvironment* env, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return nullptr;
    const proto::ProtoObject* data = obj->getAttribute(ctx, env->getDataString());
    if (data && data != PROTO_NONE && data->asList(ctx)) return data->asList(ctx);
    if (obj->asList(ctx)) return obj->asList(ctx);
    const proto::ProtoObject* inner = obj->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "_path"));
    if (inner && inner != PROTO_NONE && inner != obj) return listElements(ctx, env, inner);
    return nullptr;
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
        const proto::ProtoList* entries = listElements(ctx, env, pathObj);
        for (proto::proto_ulong i = 0; entries && i < entries->getSize(ctx); ++i) {
            std::string text = pathEntryText(ctx, env, entries->getAt(ctx, static_cast<int>(i)));
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

/**
 * The `__path__` entries of the package `parentName` in sys.modules: a
 * submodule is also searched for there, as CPython searches only there.
 * Needed when `__path__` names a directory that is not `<sys.path entry>/
 * <package>`, as `pkg.__path__.append(dir)`, pkgutil.extend_path and the
 * setuptools nspkg .pth line arrange.
 */
static std::vector<std::string> parentPackagePath(const std::string& parentName, proto::ProtoContext* ctx) {
    std::vector<std::string> entries;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* sysModule = env ? env->getSysModule() : nullptr;
    if (!sysModule || sysModule == PROTO_NONE) return entries;
    const proto::ProtoObject* modules = sysModule->getAttribute(ctx, env->getModulesS());
    const proto::ProtoObject* data = (modules && modules != PROTO_NONE)
        ? modules->getAttribute(ctx, env->getDataString()) : nullptr;
    const proto::ProtoSparseList* dict = (data && data != PROTO_NONE) ? data->asSparseList(ctx) : nullptr;
    const proto::proto_ulong h = PythonEnvironment::getInternedString(ctx, parentName.c_str())->getHash(ctx);
    const proto::ProtoObject* parent = (dict && dict->has(ctx, h)) ? dict->getAt(ctx, h) : nullptr;
    if (!parent || parent == PROTO_NONE) return entries;
    const proto::ProtoObject* pathObj = parent->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__path__"));
    const proto::ProtoList* list = env ? listElements(ctx, env, pathObj) : nullptr;
    for (proto::proto_ulong i = 0; list && i < list->getSize(ctx); ++i) {
        std::string text = pathEntryText(ctx, env, list->getAt(ctx, static_cast<int>(i)));
        if (!text.empty()) entries.push_back(text);
    }
    return entries;
}

const proto::ProtoObject* PythonModuleProvider::tryLoad(const std::string& logicalPath, proto::ProtoContext* ctx) {
    // 1. Convert dotted path to slash path
    std::string relPath = logicalPath;
    std::replace(relPath.begin(), relPath.end(), '.', '/');

    const proto::ProtoString* nameKey = proto::ProtoString::createSymbol(ctx, "__name__");
    const proto::ProtoString* fileKey = proto::ProtoString::createSymbol(ctx, "__file__");
    const proto::ProtoString* pathKey = proto::ProtoString::createSymbol(ctx, "__path__");

    // PEP 420: directories named like the module but without __init__.py.
    // They make a namespace package only when no module or regular package
    // is found in any search location.
    std::vector<std::string> portions;

    // <base>/<rel>.py, then <base>/<rel>/__init__.py; otherwise records
    // <base>/<rel> as a namespace portion when it is a directory.
    auto tryBase = [&](const std::string& basePath, const std::string& rel) -> const proto::ProtoObject* {
        // 2. Try <base>/<path>.py
        std::string pyPath = joinPath(basePath, rel + ".py");
        if (protoPython::diagResolveEnabled()) {
            fprintf(stderr, "DEBUG: tryLoad(base=%s, rel=%s) checking: %s\n",
                    basePath.c_str(), rel.c_str(), pyPath.c_str());
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
        std::string initPath = joinPath(basePath, rel + "/__init__.py");
        if (fileExists(initPath)) {
            if (protoPython::diagResolveEnabled()) fprintf(stderr, "DEBUG: tryLoad FOUND package: %s\n", initPath.c_str());
            const proto::ProtoObject* module = ctx->newObject(false);
            if (ctx->space->objectPrototype) {
                module = module->addParent(ctx, ctx->space->objectPrototype);
            }
            module = module->setAttribute(ctx, nameKey, PythonEnvironment::getInternedString(ctx, logicalPath.c_str() )->asObject(ctx));
            module = module->setAttribute(ctx, fileKey, PythonEnvironment::getInternedString(ctx, initPath.c_str() )->asObject(ctx));

            const proto::ProtoList* pkgPath = ctx->newList();
            pkgPath = pkgPath->appendLast(ctx, PythonEnvironment::getInternedString(ctx, joinPath(basePath, rel).c_str())->asObject(ctx));
            module = module->setAttribute(ctx, pathKey, pkgPath->asObject(ctx));
            return module;
        }

        std::string dir = joinPath(basePath, rel);
#if defined(_WIN32)
        // A portion is reported in the platform's form, as os.path.join and
        // importlib's FileFinder build it.
        std::replace(dir.begin() + static_cast<std::ptrdiff_t>(basePath.size()), dir.end(), '/', '\\');
#endif
        if (directoryExists(dir) && std::find(portions.begin(), portions.end(), dir) == portions.end()) {
            portions.push_back(dir);
        }
        return nullptr;
    };

    for (const auto& basePath : searchDirectories(basePaths_, ctx)) {
        if (const proto::ProtoObject* module = tryBase(basePath, relPath)) return module;
    }

    const size_t lastDot = logicalPath.rfind('.');
    if (lastDot != std::string::npos) {
        const std::string child = logicalPath.substr(lastDot + 1);
        for (const auto& entry : parentPackagePath(logicalPath.substr(0, lastDot), ctx)) {
            if (const proto::ProtoObject* module = tryBase(entry, child)) return module;
        }
    }

    if (portions.empty()) return PROTO_NONE;

    // 4. A namespace package (PEP 420): no __file__ (None), its portions as
    // __path__ (a list; CPython's _NamespacePath also follows later sys.path
    // changes), and a spec with origin None whose submodule_search_locations
    // is that list.
    if (protoPython::diagResolveEnabled()) {
        fprintf(stderr, "DEBUG: tryLoad FOUND namespace package: %s (%zu portions)\n",
                logicalPath.c_str(), portions.size());
    }
    const proto::ProtoList* portionList = ctx->newList();
    for (const auto& portion : portions) {
        portionList = portionList->appendLast(ctx, PythonEnvironment::getInternedString(ctx, portion.c_str())->asObject(ctx));
    }
    // A mutable list instance: `pkg.__path__.append(dir)` must change it.
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* pathList = portionList->asObject(ctx);
    if (env && env->getListPrototype()) {
        proto::ProtoObject* listObj = const_cast<proto::ProtoObject*>(env->getListPrototype()->newChild(ctx, true));
        listObj->setAttribute(ctx, env->getDataString(), portionList->asObject(ctx));
        pathList = listObj;
    }
    const proto::ProtoObject* nameObj = PythonEnvironment::getInternedString(ctx, logicalPath.c_str())->asObject(ctx);

    const proto::ProtoObject* spec = ctx->newObject(true);
    if (ctx->space->objectPrototype) spec = spec->addParent(ctx, ctx->space->objectPrototype);
    spec = spec->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "name"), nameObj);
    spec = spec->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "loader"), PROTO_NONE);
    spec = spec->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "origin"), PROTO_NONE);
    spec = spec->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "submodule_search_locations"), pathList);
    spec = spec->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "loader_state"), PROTO_NONE);
    spec = spec->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "cached"), PROTO_NONE);
    spec = spec->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "has_location"), PROTO_FALSE);
    spec = spec->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "parent"), nameObj);
    spec = spec->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "_initializing"), PROTO_FALSE);

    const proto::ProtoObject* module = ctx->newObject(false);
    if (ctx->space->objectPrototype) module = module->addParent(ctx, ctx->space->objectPrototype);
    module = module->setAttribute(ctx, nameKey, nameObj);
    module = module->setAttribute(ctx, fileKey, PROTO_NONE);
    module = module->setAttribute(ctx, pathKey, pathList);
    module = module->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__package__"), nameObj);
    module = module->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__loader__"), PROTO_NONE);
    module = module->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__spec__"), spec);
    return module;
}

} // namespace protoPython
