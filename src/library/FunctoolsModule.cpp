#include <protoPython/FunctoolsModule.h>
#include <protoPython/PythonEnvironment.h>

namespace protoPython {
namespace functools {

static const proto::ProtoObject* py_partial_call(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    const proto::ProtoObject* func = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__partial_func__"));
    const proto::ProtoObject* frozenObj = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__partial_args__"));
    if (!func) return PROTO_NONE;

    const proto::ProtoList* args = ctx->newList();
    if (frozenObj && frozenObj->asList(ctx)) {
        const proto::ProtoList* frozen = frozenObj->asList(ctx);
        for (unsigned long i = 0; i < frozen->getSize(ctx); ++i)
            args = args->appendLast(ctx, frozen->getAt(ctx, static_cast<int>(i)));
    }
    for (unsigned long i = 0; i < posArgs->getSize(ctx); ++i)
        args = args->appendLast(ctx, posArgs->getAt(ctx, static_cast<int>(i)));

    const proto::ProtoObject* callAttr = func->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__call__"));
    if (!callAttr || !callAttr->asMethod(ctx)) return PROTO_NONE;
    return callAttr->asMethod(ctx)(ctx, func, nullptr, args, nullptr);
}

static const proto::ProtoObject* py_partial(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* func = posArgs->getAt(ctx, 0);
    const proto::ProtoList* frozen = ctx->newList();
    for (unsigned long i = 1; i < posArgs->getSize(ctx); ++i)
        frozen = frozen->appendLast(ctx, posArgs->getAt(ctx, static_cast<int>(i)));

    const proto::ProtoObject* partialProto = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__partial_proto__"));
    if (!partialProto) return PROTO_NONE;
    const proto::ProtoObject* p = partialProto->newChild(ctx, true);
    p = p->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__partial_func__"), func);
    p = p->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__partial_args__"), frozen->asObject(ctx));
    return p;
}

static const proto::ProtoObject* py_cmp_to_key(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    // Just return a wrapper (minimal implementation for now)
    return posArgs->getAt(ctx, 0); 
}

static const proto::ProtoObject* py_reduce(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* func = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* iterable = posArgs->getAt(ctx, 1);

    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* it = env ? env->iter(iterable) : nullptr;
    if (!it) return PROTO_NONE;

    // Pin iterator and accumulator across the loop: env->next runs user
    // __next__ which can trigger GC; env->callObject runs user reducer
    // which can also trigger GC. Both `it` and `res` live only on the
    // C++ stack of this function — invisible to the GC unless pinned.
    PythonEnvironment::TransientPin pinIt(env, it);

    const proto::ProtoObject* res = (posArgs->getSize(ctx) > 2) ? posArgs->getAt(ctx, 2) : nullptr;
    if (!res) {
        res = env->next(it);
        if (!res) return PROTO_NONE;
    }

    while (const proto::ProtoObject* item = env->next(it)) {
        // Pin the running accumulator while we call the user reducer.
        // res may have been computed by a previous callObject and lives
        // only in this local across the next callObject's allocations.
        PythonEnvironment::TransientPin pinRes(env, res);
        res = env->callObject(func, {res, item});
        if (env->hasPendingException()) return nullptr;
    }
    return res;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(false);
    
    // Create Placeholder singleton and its type
    const proto::ProtoObject* placeholderType = ctx->newObject(false);
    const proto::ProtoObject* placeholder = ctx->newObject(false);
    placeholder = placeholder->addParent(ctx, placeholderType);

    const proto::ProtoObject* partialProto = ctx->newObject(false);
    partialProto = partialProto->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__call__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(partialProto), py_partial_call));
    
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__partial_proto__"), partialProto);
    // STRUCT-293: do NOT export `partial`, `Placeholder`, or
    // `_PlaceholderType` from _functools.  The native `py_partial`
    // shim above returns a plain `<object>` cell (not a class
    // instance), so `type(partial(int, '5'))` evaluates to a non-class
    // and any `issubclass(type(p), ...)` call downstream (pickle proto
    // 2/3 in particular) raises `TypeError: issubclass() arg 1 must
    // be a class`.  Withholding the symbols makes the
    // `from _functools import partial, Placeholder, _PlaceholderType`
    // line in functools.py fail with ImportError, which the surrounding
    // try/except swallows, and the pure-Python `partial` class
    // definition above the import wins.  The Python class is a real
    // user class so `type(p)` is the class itself.
    (void)placeholder;
    (void)placeholderType;
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "cmp_to_key"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_cmp_to_key));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "reduce"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_reduce));
    
    return mod;
}

} // namespace functools
} // namespace protoPython
