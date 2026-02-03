#include <protoPython/NativeModuleProvider.h>

namespace protoPython {

NativeModuleProvider::NativeModuleProvider() 
    : guid_("protoPython.native"), alias_("native") {}

void NativeModuleProvider::registerModule(const std::string& name, ModuleInitializer init) {
    modules_[name] = std::move(init);
}

const proto::ProtoObject* NativeModuleProvider::tryLoad(const std::string& logicalPath, proto::ProtoContext* ctx) {
    auto it = modules_.find(logicalPath);
    if (it != modules_.end()) {
        return it->second(ctx);
    }
    return PROTO_NONE;
}

} // namespace protoPython
