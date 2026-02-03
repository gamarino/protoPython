#include <protoPython/PythonModuleProvider.h>
#include <sys/stat.h>
#include <unistd.h>
#include <algorithm>

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
    return stat(path.c_str(), &st) == 0 && S_ISREG(st.st_mode);
}

} // anonymous namespace

PythonModuleProvider::PythonModuleProvider(std::vector<std::string> basePaths)
    : basePaths_(std::move(basePaths)), guid_("protoPython.stdlib"), alias_("python_stdlib") {}

const proto::ProtoObject* PythonModuleProvider::tryLoad(const std::string& logicalPath, proto::ProtoContext* ctx) {
    // 1. Convert dotted path to slash path
    std::string relPath = logicalPath;
    std::replace(relPath.begin(), relPath.end(), '.', '/');

    const proto::ProtoString* nameKey = proto::ProtoString::fromUTF8String(ctx, "__name__");
    const proto::ProtoString* fileKey = proto::ProtoString::fromUTF8String(ctx, "__file__");
    const proto::ProtoString* pathKey = proto::ProtoString::fromUTF8String(ctx, "__path__");

    for (const auto& basePath : basePaths_) {
        // 2. Try <base>/<path>.py
        std::string pyPath = joinPath(basePath, relPath + ".py");
        if (fileExists(pyPath)) {
            const proto::ProtoObject* module = ctx->newObject(false);
            module = module->setAttribute(ctx, nameKey, ctx->fromUTF8String(logicalPath.c_str()));
            module = module->setAttribute(ctx, fileKey, ctx->fromUTF8String(pyPath.c_str()));
            return module;
        }

        // 3. Try <base>/<path>/__init__.py (package)
        std::string initPath = joinPath(basePath, relPath + "/__init__.py");
        if (fileExists(initPath)) {
            const proto::ProtoObject* module = ctx->newObject(false);
            module = module->setAttribute(ctx, nameKey, ctx->fromUTF8String(logicalPath.c_str()));
            module = module->setAttribute(ctx, fileKey, ctx->fromUTF8String(initPath.c_str()));
            
            const proto::ProtoList* pkgPath = ctx->newList();
            pkgPath = pkgPath->appendLast(ctx, ctx->fromUTF8String(joinPath(basePath, relPath).c_str()));
            module = module->setAttribute(ctx, pathKey, pkgPath->asObject(ctx));
            return module;
        }
    }

    return PROTO_NONE;
}

} // namespace protoPython
