#include <protoPython/CollectionsAbcModule.h>

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

    return mod;
}

} // namespace collections_abc
} // namespace protoPython
