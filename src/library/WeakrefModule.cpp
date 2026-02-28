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
    
    // Inherit from ReferenceType
    if (env) {
        const proto::ProtoObject* mod = env->lookupName("_weakref");
        if (mod && mod != PROTO_NONE) {
            const proto::ProtoObject* refType = mod->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ReferenceType"));
            if (refType && refType != PROTO_NONE) {
                refObj = const_cast<proto::ProtoObject*>(refObj->addParent(ctx, refType));
            }
        }
    }

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
    const proto::ProtoObject* obj = posArgs->getAt(ctx, 0);
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    proto::ProtoObject* proxyObj = const_cast<proto::ProtoObject*>(ctx->newObject(true));
    
    if (env) {
        const proto::ProtoObject* mod = env->lookupName("_weakref");
        if (mod && mod != PROTO_NONE) {
            const proto::ProtoObject* proxyType = mod->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ProxyType"));
            if (proxyType && proxyType != PROTO_NONE) {
                proxyObj = const_cast<proto::ProtoObject*>(proxyObj->addParent(ctx, proxyType));
            }
        }
    }
    
    proxyObj->setAttribute(ctx, env ? env->getDataString() : proto::ProtoString::fromUTF8String(ctx, "__data__"), obj);
    return proxyObj;
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

    // Register types
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (env) {
        const proto::ProtoObject* pyType = env->lookupName("type");
        
        proto::ProtoObject* refType = const_cast<proto::ProtoObject*>(ctx->newObject(true));
        if (pyType && pyType != PROTO_NONE) refType = const_cast<proto::ProtoObject*>(refType->addParent(ctx, pyType));
        refType->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__name__"), ctx->fromUTF8String("ReferenceType"));
        refType->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__call__"), ctx->fromMethod(refType, py_weakref_ref));
        mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ReferenceType"), refType);

        proto::ProtoObject* proxyType = const_cast<proto::ProtoObject*>(ctx->newObject(true));
        if (pyType && pyType != PROTO_NONE) proxyType = const_cast<proto::ProtoObject*>(proxyType->addParent(ctx, pyType));
        proxyType->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__name__"), ctx->fromUTF8String("ProxyType"));
        proxyType->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__call__"), ctx->fromMethod(proxyType, py_weakref_proxy));
        mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "ProxyType"), proxyType);

        proto::ProtoObject* callableProxyType = const_cast<proto::ProtoObject*>(ctx->newObject(true));
        if (pyType && pyType != PROTO_NONE) callableProxyType = const_cast<proto::ProtoObject*>(callableProxyType->addParent(ctx, pyType));
        callableProxyType->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__name__"), ctx->fromUTF8String("CallableProxyType"));
        callableProxyType->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__call__"), ctx->fromMethod(callableProxyType, py_weakref_proxy));
        mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "CallableProxyType"), callableProxyType);
    }

    return mod;
}

} // namespace weakref
} // namespace protoPython
