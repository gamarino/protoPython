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
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : loadedHandles_) {
        if (entry.second) dlclose(entry.second);
    }
}

const proto::ProtoObject* CompiledModuleProvider::tryLoad(const std::string& logicalPath, proto::ProtoContext* ctx) {
    std::string filename = logicalPath;
    std::replace(filename.begin(), filename.end(), '.', '/');

    std::string foundPath;
    for (const auto& basePath : basePaths_) {
        std::string p = (std::filesystem::path(basePath) / (filename + ".so")).string();
        if (std::filesystem::exists(p)) {
            foundPath = p;
            break;
        }
    }

    if (foundPath.empty()) {
        return PROTO_NONE;
    }

    void* handle = dlopen(foundPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        return PROTO_NONE;
    }

    // Check for proto_module_init
    using InitFunc = void* (*)();
    InitFunc initFunc = (InitFunc)dlsym(handle, "proto_module_init");
    if (!initFunc) {
        dlclose(handle);
        return PROTO_NONE;
    }

    {
        // dlopen reference-counts a library that is already open: keep one
        // reference per module name for the provider's lifetime.
        std::lock_guard<std::mutex> lock(mutex_);
        // A different library loaded later under the same name stays open
        // but untracked: code that existing module objects may still run is
        // never unloaded.
        const auto inserted = loadedHandles_.emplace(logicalPath, handle);
        if (!inserted.second && inserted.first->second == handle) {
            dlclose(handle);  // same library: the recorded reference stays
        }
    }

    // Create the module object. It must be mutable: the generated code binds
    // module-level names with PythonEnvironment::storeName, and setAttribute
    // updates only a mutable object in place. On an immutable object every
    // binding would produce a new object that the module returned below never
    // sees.
    const proto::ProtoObject* mod = ctx->newObject(true);
    if (ctx->space->objectPrototype) mod = mod->addParent(ctx, ctx->space->objectPrototype);
    
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__name__"), PythonEnvironment::getInternedString(ctx, logicalPath.c_str())->asObject(ctx));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__file__"), PythonEnvironment::getInternedString(ctx, foundPath.c_str())->asObject(ctx));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__loader__"), PythonEnvironment::getInternedString(ctx, "CompiledModuleProvider")->asObject(ctx));

    // Mark the module executed before running it, as for Python modules.
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__executed__"), PROTO_TRUE);

    // The generated code resolves and binds names through the current frame
    // and the current globals (PythonEnvironment::lookupName / storeName), so
    // both must be this module while proto_module_init runs, not the importer's
    // namespace. The previous values are restored on every exit path.
    struct ModuleScope {
        const proto::ProtoObject* oldGlobals = PythonEnvironment::getCurrentGlobals();
        const proto::ProtoObject* oldFrame = PythonEnvironment::getCurrentFrame();
        explicit ModuleScope(const proto::ProtoObject* module) {
            PythonEnvironment::setCurrentGlobals(module);
            PythonEnvironment::setCurrentFrame(module);
            PythonEnvironment::pushScopeGlobals(module);
        }
        ~ModuleScope() {
            PythonEnvironment::popScopeGlobals();
            PythonEnvironment::setCurrentFrame(oldFrame);
            PythonEnvironment::setCurrentGlobals(oldGlobals);
        }
    };
    {
        ModuleScope scope(mod);
        initFunc();
    }

    return mod;
}

} // namespace protoPython
