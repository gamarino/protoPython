#include <protoPython/NativeModuleProvider.h>
#include <iostream>

namespace protoPython {

NativeModuleProvider::NativeModuleProvider() 
    : guid_("protoPython.native"), alias_("native") {}

void NativeModuleProvider::registerModule(const std::string& name, ModuleInitializer init) {
    std::cerr << "!!! DEBUG NATIVE: registerModule(" << name << ")" << std::endl;
    modules_[name] = std::move(init);
}

const proto::ProtoObject* NativeModuleProvider::tryLoad(const std::string& logicalPath, proto::ProtoContext* ctx) {
    std::cerr << "!!! DEBUG NATIVE: tryLoad(" << logicalPath << ") - map size=" << modules_.size() << std::endl;
    auto it = modules_.find(logicalPath);
    if (it != modules_.end()) {
        const proto::ProtoObject* mod = it->second(ctx);
        if (mod && mod != PROTO_NONE) {
            fprintf(stderr, "DEBUG NATIVE: Loaded %s mod=%p\n", logicalPath.c_str(), (void*)mod);
            mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__executed__"), PROTO_TRUE);
        }
        return mod;
    }
    return nullptr;
}

} // namespace protoPython
