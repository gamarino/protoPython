#include <protoCore.h>
#include <protoPython/PythonEnvironment.h>

namespace protoPython {
namespace weakref {

static const proto::ProtoObject* py_weakref_ref_call(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* data = self->getAttribute(ctx, env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__"));
    return data ? data : PROTO_NONE;
}

static const proto::ProtoObject* py_weakref_ref(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = posArgs->getAt(ctx, 0);
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    
    // Create a ref object
    proto::ProtoObject* refObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
    refObj->setAttribute(ctx, env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__"), obj);
    refObj->setAttribute(ctx, env ? env->getCallString() : proto::ProtoString::fromUTF8String(ctx, "__call__"),
        ctx->fromMethod(refObj, py_weakref_ref_call));
    return refObj;
}

static const proto::ProtoObject* py_weakref_proxy(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    // For now, identity proxy
    return posArgs->getAt(ctx, 0);
}

static const proto::ProtoObject* py_weakref_getweakrefcount(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    return ctx->fromInteger(0);
}

static const proto::ProtoObject* py_weakref_getweakrefs(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    return ctx->newList()->asObject(ctx);
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    proto::ProtoObject* mod = const_cast<proto::ProtoObject*>(ctx->newObject(true));
    mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "proxy"),
        ctx->fromMethod(mod, py_weakref_proxy));
    mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ref"),
        ctx->fromMethod(mod, py_weakref_ref));
    mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "getweakrefcount"),
        ctx->fromMethod(mod, py_weakref_getweakrefcount));
    mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "getweakrefs"),
        ctx->fromMethod(mod, py_weakref_getweakrefs));
    return mod;
}

} // namespace weakref
} // namespace protoPython
