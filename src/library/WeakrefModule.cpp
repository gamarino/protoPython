#include <protoCore.h>
#include <protoPython/PythonEnvironment.h>

namespace protoPython {
namespace weakref {

static const proto::ProtoObject* py_weakref_proxy(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    // For now, identity proxy
    return posArgs->getAt(ctx, 0);
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    proto::ProtoObject* mod = const_cast<proto::ProtoObject*>(ctx->newObject(true));
    mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "proxy"),
        ctx->fromMethod(mod, py_weakref_proxy));
    return mod;
}

} // namespace weakref
} // namespace protoPython
