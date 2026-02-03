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

PythonModuleProvider::PythonModuleProvider(std::string basePath)
    : basePath_(std::move(basePath)), guid_("protoPython.stdlib"), alias_("python_stdlib") {}

const proto::ProtoObject* PythonModuleProvider::tryLoad(const std::string& logicalPath, proto::ProtoContext* ctx) {
    // 1. Convert dotted path to slash path
    std::string path = logicalPath;
    std::replace(path.begin(), path.end(), '.', '/');

    // 2. Try <path>.py
    std::string pyPath = joinPath(basePath_, path + ".py");
    if (fileExists(pyPath)) {
        // Create a basic Python module object
        // Later this will hold the compiled bytecode AST
        const proto::ProtoObject* module = ctx->newObject(false);
        const proto::ProtoString* nameKey = proto::ProtoString::fromUTF8String(ctx, "__name__");
        const proto::ProtoString* fileKey = proto::ProtoString::fromUTF8String(ctx, "__file__");
        
        module = module->setAttribute(ctx, nameKey, ctx->fromUTF8String(logicalPath.c_str()));
        module = module->setAttribute(ctx, fileKey, ctx->fromUTF8String(pyPath.c_str()));
        
        return module;
    }

    // 3. Try <path>/__init__.py (package)
    std::string initPath = joinPath(basePath_, path + "/__init__.py");
    if (fileExists(initPath)) {
        const proto::ProtoObject* module = ctx->newObject(false);
        const proto::ProtoString* nameKey = proto::ProtoString::fromUTF8String(ctx, "__name__");
        const proto::ProtoString* fileKey = proto::ProtoString::fromUTF8String(ctx, "__file__");
        const proto::ProtoString* pathKey = proto::ProtoString::fromUTF8String(ctx, "__path__");
        
        module = module->setAttribute(ctx, nameKey, ctx->fromUTF8String(logicalPath.c_str()));
        module = module->setAttribute(ctx, fileKey, ctx->fromUTF8String(initPath.c_str()));
        
        // __path__ for packages
        const proto::ProtoList* pkgPath = ctx->newList();
        pkgPath = pkgPath->appendLast(ctx, ctx->fromUTF8String(joinPath(basePath_, path).c_str()));
        module = module->setAttribute(ctx, pathKey, pkgPath->asObject(ctx));
        
        return module;
    }

    return PROTO_NONE;
}

} // namespace protoPython
