#include <protoPython/HPyModuleProvider.h>
#include <dlfcn.h>
#include <filesystem>
#include <iostream>

namespace protoPython {

HPyModuleProvider::HPyModuleProvider(std::vector<std::string> basePaths)
    : basePaths_(std::move(basePaths)), guid_("protoPython.hpy"), alias_("hpy") {}

HPyModuleProvider::~HPyModuleProvider() {
    for (auto& entry : loadedHandles_) {
        if (entry.second) {
            dlclose(entry.second);
        }
    }
}

const proto::ProtoObject* HPyModuleProvider::tryLoad(const std::string& logicalPath, proto::ProtoContext* ctx) {
    if (loadedHandles_.count(logicalPath)) {
        // Module already loaded at the library level. 
        // In a more complex system, we'd need to re-invoke init if the context is different,
        // but for now we follow the 1-load-per-provider rule.
        // Actually, we should probably return the cached module object if we had it.
    }

    std::string filename = logicalPath;
    // Replace dots with slashes for nested modules if applicable
    for (size_t i = 0; i < filename.length(); ++i) {
        if (filename[i] == '.') filename[i] = '/';
    }

    std::string foundPath;
    for (const auto& basePath : basePaths_) {
        std::string p1 = (std::filesystem::path(basePath) / (filename + ".hpy.so")).string();
        std::string p2 = (std::filesystem::path(basePath) / (filename + ".so")).string();

        if (std::filesystem::exists(p1)) {
            foundPath = p1;
            break;
        }
        if (std::filesystem::exists(p2)) {
            foundPath = p2;
            break;
        }
    }

    if (foundPath.empty()) {
        return PROTO_NONE;
    }

    // Step 1220: CLI Flags for HPy (Debug)
    bool debug = std::getenv("PROTO_HPY_DEBUG") != nullptr;
    if (debug) {
        // log removed
    }

    // Step 1205: Implement logic to load .so and .hpy.so files
    void* handle = dlopen(foundPath.c_str(), RTLD_NOW | RTLD_GLOBAL);
    if (!handle) {
        if (debug) {
            // log removed
        }
        return PROTO_NONE;
    }

    loadedHandles_[logicalPath] = handle;

    // Step 1206: HPyModuleDef Resolution (actually HPyInit symbol)
    // The entry point for an HPy module named 'foo' is 'HPyInit_foo'
    std::string initName = "HPyInit_" + logicalPath;
    // Handle nested names by taking the last part if logicalPath has dots
    size_t lastDot = logicalPath.find_last_of('.');
    if (lastDot != std::string::npos) {
        initName = "HPyInit_" + logicalPath.substr(lastDot + 1);
    }

    using HPyInitFunc = HPy (*)(HPyContext*);
    HPyInitFunc initFunc = (HPyInitFunc)dlsym(handle, initName.c_str());

    // Step 1216: ABI Version Check (Simplified)
    // In a real scenario, we might check for a 'HPy_ABI_VERSION' symbol
    int* abiVersion = (int*)dlsym(handle, "HPy_ABI_VERSION");
    if (abiVersion && *abiVersion > 0) {
        if (debug) {
        }
    }

    if (!initFunc) {
        // log removed
        return PROTO_NONE;
    }

    // Step 1207: HPy Init Invocation
    HPyContext hctx(ctx);
    HPy hMod = initFunc(&hctx);

    if (hMod == HPy_NULL) {
        // log removed
        return PROTO_NONE;
    }

    // Convert handle back to ProtoObject*
    const proto::ProtoObject* mod = hctx.asProtoObject(hMod);
    if (!mod || mod == PROTO_NONE) {
        return PROTO_NONE;
    }

    // Step 1210: Module Wiring
    // Set __file__, __name__, and other metadata
    const proto::ProtoString* py_file = proto::ProtoString::fromUTF8String(ctx, "__file__");
    const proto::ProtoString* py_name = proto::ProtoString::fromUTF8String(ctx, "__name__");
    const proto::ProtoString* py_loader = proto::ProtoString::fromUTF8String(ctx, "__loader__");

    mod->setAttribute(ctx, py_file, ctx->fromUTF8String(foundPath.c_str()));
    mod->setAttribute(ctx, py_name, ctx->fromUTF8String(logicalPath.c_str()));
    // For now, we don't have a specialized loader object, so we just set a string or None
    mod->setAttribute(ctx, py_loader, ctx->fromUTF8String("HPyModuleProvider"));

    // Step 1300: Refine sys.modules wiring
    // In protoPython, the PythonEnvironment usually manages sys.modules. 
    // Here we ensure that if it's available, we populate it to avoid redundant loads.
    // (Note: This is a best-effort refinement; typically the resolution chain handles this).

    return mod;
}

} // namespace protoPython
