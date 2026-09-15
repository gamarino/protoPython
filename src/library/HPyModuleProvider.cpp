#include <protoPython/PythonEnvironment.h>
#include <protoPython/HPyModuleProvider.h>
#include <protoPython/HPyABI.h>
#include <algorithm>
#include <dlfcn.h>
#include <filesystem>
#include <system_error>

namespace protoPython {

HPyModuleProvider::HPyModuleProvider(std::vector<std::string> basePaths)
    : basePaths_(std::move(basePaths)), guid_("protoPython.hpy"), alias_("hpy") {}

HPyModuleProvider::~HPyModuleProvider() {
    std::lock_guard<std::mutex> lock(mutex_);
    for (auto& entry : loadedHandles_) {
        if (entry.second) dlclose(entry.second);
    }
}

const proto::ProtoObject* HPyModuleProvider::tryLoad(const std::string& logicalPath, proto::ProtoContext* ctx) {
    // Step 1205: find <name>.hpy.so, then <name>.so, in each search directory;
    // dots in the module name map to directory separators.
    std::string filename = logicalPath;
    std::replace(filename.begin(), filename.end(), '.', '/');

    std::string foundPath;
    std::error_code ec;
    for (const auto& basePath : basePaths_) {
        for (const char* suffix : {".hpy.so", ".so"}) {
            std::string candidate = (std::filesystem::path(basePath) / (filename + suffix)).string();
            if (std::filesystem::exists(candidate, ec)) {
                foundPath = std::move(candidate);
                break;
            }
        }
        if (!foundPath.empty()) break;
    }
    if (foundPath.empty()) return PROTO_NONE;

    // Step 1206: the entry point of module "a.b.foo" is HPyInit_foo.
    const size_t lastDot = logicalPath.find_last_of('.');
    const std::string initName =
        "HPyInit_" + (lastDot == std::string::npos ? logicalPath : logicalPath.substr(lastDot + 1));

    // RTLD_LOCAL: an extension's symbols must not interpose on those of other
    // libraries loaded later.
    void* handle = dlopen(foundPath.c_str(), RTLD_NOW | RTLD_LOCAL);
    if (!handle) return PROTO_NONE;

    using HPyInitFunc = HPy (*)(HPyContext*);
    const HPyInitFunc initFunc = reinterpret_cast<HPyInitFunc>(dlsym(handle, initName.c_str()));
    if (!initFunc) {
        // Not an HPy extension module: do not keep the library loaded.
        dlclose(handle);
        return PROTO_NONE;
    }
    {
        // dlopen reference-counts a library that is already open; keep one
        // reference per library for the provider's lifetime. Module resolution
        // is not serialised by a lock, so the map is guarded here.
        std::lock_guard<std::mutex> lock(mutex_);
        const auto inserted = loadedHandles_.emplace(foundPath, handle);
        if (!inserted.second) dlclose(handle);  // the first reference stays
    }

    // Step 1207: run the entry point. Handles it creates stay pinned while hctx
    // lives, so the module is wired before hctx is destroyed.
    const proto::ProtoObject* mod = nullptr;
    {
        HPyContext hctx(ctx);
        const HPy hMod = initFunc(&hctx);
        mod = (hMod == HPy_NULL) ? nullptr : hctx.asProtoObject(hMod);
        if (!mod || mod == PROTO_NONE) return PROTO_NONE;

        // Step 1210: module metadata. setAttribute returns the updated object
        // (a copy when the extension returned an immutable one), so keep it.
        // __executed__ tells module resolution the module is already initialised.
        auto key = [ctx](const std::string& name) { return PythonEnvironment::getInternedString(ctx, name); };
        mod = mod->setAttribute(ctx, key("__file__"), key(foundPath)->asObject(ctx));
        mod = mod->setAttribute(ctx, key("__name__"), key(logicalPath)->asObject(ctx));
        mod = mod->setAttribute(ctx, key("__loader__"), key("HPyModuleProvider")->asObject(ctx));
        mod = mod->setAttribute(ctx, key("__executed__"), PROTO_TRUE);
    }
    return mod;
}

} // namespace protoPython
