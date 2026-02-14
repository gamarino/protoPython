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
        const proto::ProtoObject* res = it->second(ctx);
        if (std::getenv("PROTO_ENV_DIAG")) {
            std::cerr << "[proto-diag] NativeModuleProvider::tryLoad found '" << logicalPath << "' -> " << res << "\n" << std::flush;
        }
        return res;
    }
    return PROTO_NONE;
}

} // namespace protoPython
