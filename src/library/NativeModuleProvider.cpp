#include <protoPython/NativeModuleProvider.h>
#include <iostream>

namespace protoPython {

NativeModuleProvider::NativeModuleProvider() 
    : guid_("protoPython.native"), alias_("native") {}

void NativeModuleProvider::registerModule(const std::string& name, ModuleInitializer init) {
    modules_[name] = std::move(init);
}

const proto::ProtoObject* NativeModuleProvider::tryLoad(const std::string& logicalPath, proto::ProtoContext* ctx) {
    auto it = modules_.find(logicalPath);
    if (it != modules_.end()) {
        const proto::ProtoObject* mod = it->second(ctx);
        if (mod && mod != PROTO_NONE) {
            mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__executed__"), PROTO_TRUE);
        }
        return mod;
    }
    return nullptr;
}

} // namespace protoPython
