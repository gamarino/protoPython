#include <protoPython/CompiledModuleProvider.h>
#include <protoPython/PythonEnvironment.h>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>
#include <algorithm>

namespace protoPython {

CompiledModuleProvider::CompiledModuleProvider(std::vector<std::string> basePaths)
    : basePaths_(std::move(basePaths)), guid_("protoPython.compiled"), alias_("compiled") {}

CompiledModuleProvider::~CompiledModuleProvider() {
    for (auto& entry : loadedHandles_) {
        if (entry.second) dlclose(entry.second);
    }
}

const proto::ProtoObject* CompiledModuleProvider::tryLoad(const std::string& logicalPath, proto::ProtoContext* ctx) {
    if (std::getenv("PROTO_ENV_DIAG")) {
        std::cerr << "[proto-env] CompiledModuleProvider::tryLoad(" << logicalPath << ") searchPaths: ";
        for (const auto& p : basePaths_) std::cerr << p << " ";
        std::cerr << "\n";
    }
    std::string filename = logicalPath;
    std::replace(filename.begin(), filename.end(), '.', '/');

    std::string foundPath;
    for (const auto& basePath : basePaths_) {
        std::string p = (std::filesystem::path(basePath) / (filename + ".so")).string();
        if (std::getenv("PROTO_ENV_DIAG")) std::cerr << "[proto-env] CompiledModuleProvider: checking " << p << "\n";
        if (std::filesystem::exists(p)) {
            foundPath = p;
            break;
        }
    }

    if (foundPath.empty()) {
        if (std::getenv("PROTO_ENV_DIAG")) std::cerr << "[proto-env] CompiledModuleProvider: NOT FOUND " << logicalPath << "\n" << std::flush;
        return PROTO_NONE;
    }

    if (std::getenv("PROTO_ENV_DIAG")) std::cerr << "[proto-env] CompiledModuleProvider: found " << foundPath << " for " << logicalPath << "\n" << std::flush;

    void* handle = dlopen(foundPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        if (std::getenv("PROTO_ENV_DIAG")) std::cerr << "[proto-env] CompiledModuleProvider: dlopen FAILED for " << foundPath << ": " << dlerror() << "\n" << std::flush;
        return PROTO_NONE;
    }

    // Check for proto_module_init
    using InitFunc = void* (*)();
    InitFunc initFunc = (InitFunc)dlsym(handle, "proto_module_init");
    if (!initFunc) {
        if (std::getenv("PROTO_ENV_DIAG")) std::cerr << "[proto-env] CompiledModuleProvider: proto_module_init NOT FOUND in " << foundPath << "\n" << std::flush;
        dlclose(handle);
        return PROTO_NONE;
    }

    loadedHandles_[logicalPath] = handle;

    // Create module object
    const proto::ProtoObject* mod = ctx->newObject(true);
    if (ctx->space->objectPrototype) mod = mod->addParent(ctx, ctx->space->objectPrototype);
    
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__name__"), ctx->fromUTF8String(logicalPath.c_str()));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__file__"), ctx->fromUTF8String(foundPath.c_str()));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__loader__"), ctx->fromUTF8String("CompiledModuleProvider"));

    // To allow proto_module_init to work, we need to set the current globals to this module.
    // In protoPython, resolve() depends on s_currentGlobals.
    const proto::ProtoObject* oldGlobals = PythonEnvironment::getCurrentGlobals();
    PythonEnvironment::setCurrentGlobals(mod);
    
    // Also, we might need a way to mark it as executed
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__executed__"), PROTO_TRUE);

    initFunc();

    PythonEnvironment::setCurrentGlobals(oldGlobals);

    return mod;
}

} // namespace protoPython
