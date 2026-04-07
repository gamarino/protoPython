#include <protoPython/NativeModuleProvider.h>
#include <protoPython/PythonEnvironment.h>
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
            PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
            if (env) {
                if (mod->getAttribute(ctx, env->getExecutedString()) != PROTO_TRUE) {
                    mod = mod->setAttribute(ctx, env->getExecutedString(), PROTO_TRUE);
                }
            } else {
                const proto::ProtoString* executedS = proto::ProtoString::createSymbol(ctx, "__executed__");
                if (mod->getAttribute(ctx, executedS) != PROTO_TRUE) {
                    mod = mod->setAttribute(ctx, executedS, PROTO_TRUE);
                }
            }
        }
        return mod;
    }
    return nullptr;
}

} // namespace protoPython
