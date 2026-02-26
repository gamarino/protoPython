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

static const proto::ProtoObject* py_weakref_remove_dead_weakref(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* dct = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* key = posArgs->getAt(ctx, 1);
    
    // Attempt to delete the key from the dictionary.
    // If it's a dict, use deleteItem or setAttribute to none depending on the runtime support.
    // For now, since dict mutation is supported, we can just do a setAttribute with None (or delete if API exists).
    // In our simplified engine, we can try to use delItem or just ignore if it fails.
    if (dct && key) {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        if (env) {
            // del dct[key]
            env->delItem(dct, key);
        }
    }
    return PROTO_NONE;
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
    mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "_remove_dead_weakref"),
        ctx->fromMethod(mod, py_weakref_remove_dead_weakref));

    // Register types (for now, simply aliases to avoid missing attribute errors).
    // In a full implementation, these would point to type prototypes.
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (env) {
        const proto::ProtoObject* pyType = env->lookupName("type");
        mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ReferenceType"), pyType ? pyType : PROTO_NONE);
        mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ProxyType"), pyType ? pyType : PROTO_NONE);
        mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "CallableProxyType"), pyType ? pyType : PROTO_NONE);
    }

    return mod;
}

} // namespace weakref
} // namespace protoPython
