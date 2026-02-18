#include <protoPython/CollectionsAbcModule.h>
#include <protoPython/PythonEnvironment.h>

namespace protoPython {
namespace collections_abc {

/** Minimal __call__ for ABC stub: return new child (for isinstance/callable use). */
static const proto::ProtoObject* py_abc_call(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    return self->newChild(ctx, true);
}

/** Minimal register for ABC stub: just return the argument. */
static const proto::ProtoObject* py_abc_register(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return self;
    return posArgs->getAt(ctx, 0);
}

/** _check_methods(C, *methods) implementation for internal use by os.py/io.py etc. */
static const proto::ProtoObject* py_abc_check_methods(
    proto::ProtoContext* ctx,
    const proto::ProtoObject*,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    
    if (args->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* C = args->getAt(ctx, 0);

    // Get NotImplemented singleton
    const proto::ProtoObject* notImplemented = PROTO_NONE;
    if (auto* env = PythonEnvironment::fromContext(ctx)) {
        notImplemented = env->resolve("NotImplemented");
    }
    if (!notImplemented) notImplemented = PROTO_NONE;

    const proto::ProtoString* mroName = proto::ProtoString::fromUTF8String(ctx, "__mro__");
    const proto::ProtoObject* mroObj = C->getAttribute(ctx, mroName);
    if (!mroObj || !mroObj->asList(ctx)) return notImplemented;

    const proto::ProtoList* mro = mroObj->asList(ctx);

    // Loop through methods (args 1..N)
    for (unsigned long i = 1; i < args->getSize(ctx); ++i) {
        const proto::ProtoObject* methodObj = args->getAt(ctx, i);
        if (!methodObj->isString(ctx)) continue;
        
        unsigned long methodHash = methodObj->getHash(ctx);
        bool found = false;

        for (unsigned long j = 0; j < mro->getSize(ctx); ++j) {
            const proto::ProtoObject* B = mro->getAt(ctx, j);
            const proto::ProtoSparseList* attrs = B->getOwnAttributes(ctx);
            // fprintf(stderr, "  Checking class %lu in MRO\n", j);
            
            if (attrs && attrs->has(ctx, methodHash)) {
                // fprintf(stderr, "  Found in class %lu\n", j);
                const proto::ProtoObject* val = attrs->getAt(ctx, methodHash);
                if (val == PROTO_NONE) return notImplemented; 
                found = true;
                break;
            }
        }
        if (!found) return notImplemented;
    }
    return PROTO_TRUE;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    auto createAbc = [&](const char* name) {
        proto::ProtoObject* abc = const_cast<proto::ProtoObject*>(ctx->newObject(true));
        abc->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__call__"),
            ctx->fromMethod(abc, py_abc_call));
        abc->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "register"),
            ctx->fromMethod(abc, py_abc_register));
        abc->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__name__"),
            ctx->fromUTF8String(name));
        
        // Set __class__ to self for diagnostic clarity (raiseAttributeError uses it)
        abc->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__class__"), abc);

        // Add dummy methods to satisfy collections/__init__.py inheritance of methods
        const char* methods[] = {
            "update", "get", "keys", "values", "items", "pop", "popitem", "clear",
            "setdefault", "index", "count", "append", "extend", "insert", "remove", "reverse",
            "__eq__", "__ne__", "__lt__", "__le__", "__gt__", "__ge__",
            "__iter__", "__len__", "__contains__", "__hash__"
        };
        for (const char* m : methods) {
            abc->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, m),
                ctx->fromMethod(abc, py_abc_call)); // Reuse py_abc_call as a dummy method
        }
        return abc;
    };

    const proto::ProtoObject* mod = ctx->newObject(true);
    const char* names[] = {
        "Hashable", "Iterable", "Iterator", "Sized", "Container", "Collection",
        "Mapping", "MutableMapping", "Sequence", "MutableSequence",
        "Set", "MutableSet", "Callable", "Awaitable", "Coroutine",
        "AsyncIterable", "AsyncIterator", "AsyncGenerator", "Generator",
        "KeysView", "ValuesView", "ItemsView", "MappingView"
    };

    for (const char* name : names) {
        mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, name), createAbc(name));
    }

    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "_check_methods"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_abc_check_methods));

    return mod;
}

} // namespace collections_abc
} // namespace protoPython
