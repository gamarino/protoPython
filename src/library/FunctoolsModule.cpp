#include <protoPython/FunctoolsModule.h>
#include <protoPython/PythonEnvironment.h>
#include <string>

namespace protoPython {
namespace functools {

static const proto::ProtoObject* py_partial_call(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments* posArgs,
    const proto::ProtoKeywordArguments*) {
    const proto::ProtoObject* func = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__partial_func__"));
    const proto::ProtoObject* frozenObj = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__partial_args__"));
    if (!func) return PROTO_NONE;

    const proto::ProtoList* args = ctx->newList();
    if (frozenObj && ::protoPython::listStorage(ctx, frozenObj)) {
        const proto::ProtoList* frozen = ::protoPython::listStorage(ctx, frozenObj);
        for (proto::proto_ulong i = 0; i < frozen->getSize(ctx); ++i)
            args = args->appendLast(ctx, frozen->getAt(ctx, static_cast<int>(i)));
    }
    for (proto::proto_ulong i = 0; i < posArgs->getSize(ctx); ++i)
        args = args->appendLast(ctx, posArgs->getAt(ctx, static_cast<int>(i)));

    const proto::ProtoObject* callAttr = func->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__call__"));
    if (!callAttr || !callAttr->asMethod(ctx)) return PROTO_NONE;
    return callAttr->asMethod(ctx)(ctx, func, nullptr, argsView(proto::ProtoListArguments(ctx, args)), nullptr);
}

static const proto::ProtoObject* py_partial(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments* posArgs,
    const proto::ProtoKeywordArguments*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* func = posArgs->getAt(ctx, 0);
    const proto::ProtoList* frozen = ctx->newList();
    for (proto::proto_ulong i = 1; i < posArgs->getSize(ctx); ++i)
        frozen = frozen->appendLast(ctx, posArgs->getAt(ctx, static_cast<int>(i)));

    const proto::ProtoObject* partialProto = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__partial_proto__"));
    if (!partialProto) return PROTO_NONE;
    const proto::ProtoObject* p = partialProto->newChild(ctx, true);
    p = p->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__partial_func__"), func);
    p = p->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__partial_args__"), frozen->asObject(ctx));
    return p;
}

// reduce(function, iterable, /[, initial]): CPython 3.14's signature, with
// initial also accepted by keyword.  Wrong arity, a non-iterable and an
// empty iterable without an initial value raise TypeError; they returned
// None (or ignored initial= given by keyword).
static const proto::ProtoObject* py_reduce(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink*,
    const proto::ProtoArguments* posArgs,
    const proto::ProtoKeywordArguments* kwArgs) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return PROTO_NONE;
    const proto::proto_ulong argc = posArgs ? posArgs->getSize(ctx) : 0;
    const proto::proto_ulong kwc = kwArgs ? kwArgs->getSize(ctx) : 0;
    const proto::ProtoString* initialS = PythonEnvironment::getInternedString(ctx, "initial");
    const proto::ProtoObject* initialKw = nullptr;
    if (kwc) {
        const proto::proto_ulong h = initialS->getHash(ctx);
        if (kwc != 1 || !kwArgs->has(ctx, h)) {
            env->raiseTypeError(ctx, "reduce() got an unexpected keyword argument");
            return nullptr;
        }
        initialKw = kwArgs->getAt(ctx, h);
    }
    if (argc < 2) {
        env->raiseTypeError(ctx, argc == 0
            ? "reduce() missing required argument 'function' (pos 1)"
            : "reduce() missing required argument 'iterable' (pos 2)");
        return nullptr;
    }
    if (argc > 3 || (argc == 3 && initialKw)) {
        env->raiseTypeError(ctx, argc > 3
            ? "reduce() takes at most 3 arguments (" + std::to_string(argc + kwc) + " given)"
            : "argument for reduce() given by name ('initial') and position (3)");
        return nullptr;
    }
    const proto::ProtoObject* func = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* iterable = posArgs->getAt(ctx, 1);

    // env->iter raises TypeError for a non-iterable.
    const proto::ProtoObject* it = env->iter(iterable);
    if (!it) return nullptr;

    // Pin iterator and accumulator across the loop: env->next runs user
    // __next__ which can trigger GC; env->callObject runs user reducer
    // which can also trigger GC. Both `it` and `res` live only on the
    // C++ stack of this function — invisible to the GC unless pinned.
    PythonEnvironment::TransientPin pinIt(env, it);

    const proto::ProtoObject* res = (argc == 3) ? posArgs->getAt(ctx, 2) : initialKw;
    if (!res) {
        res = env->next(it);
        if (!res) {
            env->raiseTypeError(ctx, "reduce() of empty iterable with no initial value");
            return nullptr;
        }
    }

    while (const proto::ProtoObject* item = env->next(it)) {
        // Pin the running accumulator while we call the user reducer.
        // res may have been computed by a previous callObject and lives
        // only in this local across the next callObject's allocations.
        PythonEnvironment::TransientPin pinRes(env, res);
        res = env->callObject(func, {res, item});
    }
    return res;
}

// _functools.cmp_to_key(mycmp): a builtin, as in CPython, so a class that
// stores it (TestCmpToKeyC: `cmp_to_key = c_functools.cmp_to_key`) does
// not bind it as a method.  The key class it returns is defined in
// lib/_functools_keywrapper.py.
static const proto::ProtoObject* py_cmp_to_key(
    proto::ProtoContext* ctx,
    const proto::ProtoObject*,
    const proto::ParentLink*,
    const proto::ProtoArguments* posArgs,
    const proto::ProtoKeywordArguments* kwargs) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return PROTO_NONE;
    const proto::proto_ulong npos = posArgs ? posArgs->getSize(ctx) : 0;
    const proto::proto_ulong nkw = kwargs ? kwargs->getSize(ctx) : 0;
    const proto::proto_ulong mycmpKey = PythonEnvironment::getInternedString(ctx, "mycmp")->getHash(ctx);
    const proto::ProtoObject* mycmp = nullptr;
    if (npos + nkw > 1) {
        env->raiseTypeError(ctx, "cmp_to_key() takes at most 1 argument (" + std::to_string(npos + nkw) + " given)");
        return nullptr;
    }
    if (npos == 1) {
        mycmp = posArgs->getAt(ctx, 0);
    } else if (nkw == 1 && kwargs->has(ctx, mycmpKey)) {
        mycmp = kwargs->getAt(ctx, mycmpKey);
    } else if (nkw == 1) {
        env->raiseTypeError(ctx, "cmp_to_key() got an unexpected keyword argument");
        return nullptr;
    } else {
        env->raiseTypeError(ctx, "cmp_to_key() missing required argument 'mycmp' (pos 1)");
        return nullptr;
    }
    const proto::ProtoObject* helper = env->importModule("_functools_keywrapper");
    if (!helper || helper == PROTO_NONE) return nullptr;
    const proto::ProtoObject* make = env->getAttribute(ctx, helper,
        PythonEnvironment::getInternedString(ctx, "cmp_to_key"));
    if (!make || make == PROTO_NONE) return nullptr;
    return env->callObject(make, {mycmp});
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
