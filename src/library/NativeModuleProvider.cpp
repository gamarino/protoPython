#include <protoPython/NativeModuleProvider.h>
#include <protoPython/PythonEnvironment.h>
#include <iostream>

namespace protoPython {

NativeModuleProvider::NativeModuleProvider(const std::string& suffix)
    : guid_("protoPython.native" + suffix), alias_("native" + suffix) {}

void NativeModuleProvider::registerModule(const std::string& name, ModuleInitializer init) {
    modules_[name] = std::move(init);
}

const proto::ProtoObject* NativeModuleProvider::tryLoad(const std::string& logicalPath, proto::ProtoContext* ctx) {
    auto it = modules_.find(logicalPath);
    if (it != modules_.end()) {
        const proto::ProtoObject* mod = it->second(ctx);
        if (mod && mod != PROTO_NONE) {
            PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
            const proto::ProtoString* executedS = env ? env->getExecutedString()
                                                      : proto::ProtoString::createSymbol(ctx, "\001__executed__");
            // A module object is mutable, as in CPython: `math.sqrt = f`
            // (and unittest.mock.patch) must replace the attribute. A
            // module built immutable answers its update with a new object;
            // it is thawed into a mutable copy here, once.
            const proto::ProtoObject* flagged = mod->setAttribute(ctx, executedS, PROTO_TRUE);
            if (flagged != mod) {
                const proto::ProtoObject* thawed = flagged->clone(ctx, true);
                mod = (thawed && thawed != PROTO_NONE) ? thawed : flagged;
            }
        }
        return mod;
    }
    return nullptr;
}

} // namespace protoPython
