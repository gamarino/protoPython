#include <protoPython/CollectionsAbcModule.h>
#include <protoPython/DiagUtils.h>
#include <protoPython/PythonEnvironment.h>
#include <cstring>
#include <protoCore.h>

namespace protoPython {

// Forward declaration — implemented in ExecutionEngine.cpp
extern const proto::ProtoObject* invokePythonCallable(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* callable,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kwargs);
static const proto::ProtoObject* py_check_methods(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* sparseArgs) {


    if (!posArgs || posArgs->getSize(ctx) < 2) return PROTO_FALSE;
    const proto::ProtoObject* C = posArgs->getAt(ctx, 0);
    // STRUCT-132: descriptor-aware __mro__ read.
    PythonEnvironment* envABC = PythonEnvironment::fromContext(ctx);
    const proto::ProtoString* mroABCS = proto::ProtoString::createSymbol(ctx, "__mro__");
    const proto::ProtoObject* mro = envABC
        ? envABC->getAttribute(ctx, C, mroABCS, false)
        : C->getAttribute(ctx, mroABCS);
    if (!mro) return PROTO_FALSE;
    const proto::ProtoList* mroList = mro->asList(ctx);
    if (!mroList) return PROTO_FALSE;

    for (size_t i = 1; i < posArgs->getSize(ctx); ++i) {
        const proto::ProtoObject* methodObj = posArgs->getAt(ctx, i);
        const proto::ProtoString* methodName = methodObj ? methodObj->asString(ctx) : nullptr;
        if (!methodName) continue;

        bool found = false;
        for (size_t j = 0; j < mroList->getSize(ctx); ++j) {
            const proto::ProtoObject* base = mroList->getAt(ctx, j);
            const proto::ProtoObject* dict = base->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__dict__"));
            if (dict && dict->hasAttribute(ctx, methodName)) {
                found = true;
                break;
            }
        }
        if (!found) return PROTO_FALSE;
    }
    return PROTO_TRUE;
}




namespace collections_abc {

// Helper: iterate a mapping (using __iter__ + __getitem__) and collect (key, value) tuples.
// Returns a ProtoList of 2-element tuples, or nullptr on error.
static const proto::ProtoList* collectMappingItems(proto::ProtoContext* ctx, const proto::ProtoObject* mapping) {
    if (!mapping || mapping == PROTO_NONE) return nullptr;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoList* emptyL = ctx->newList();

    auto callMethod = [&](const proto::ProtoObject* method, const proto::ProtoObject* receiver,
                          const proto::ProtoList* args) -> const proto::ProtoObject* {
        if (!method || method == PROTO_NONE) return nullptr;
        if (method->asMethod(ctx)) {
            return method->asMethod(ctx)(ctx, const_cast<proto::ProtoObject*>(receiver), nullptr, args, nullptr);
        }
        const proto::ProtoList* selfArgs = ctx->newList()->appendLast(ctx, receiver);
        unsigned long n = args ? args->getSize(ctx) : 0;
        for (unsigned long j = 0; j < n; ++j) selfArgs = selfArgs->appendLast(ctx, args->getAt(ctx, j));
        // Use the extern helper declared in ExecutionEngine.cpp
        return invokePythonCallable(ctx, method, selfArgs, nullptr);
    };

    const proto::ProtoString* iterS = PythonEnvironment::getInternalString(ctx, "__iter__");
    const proto::ProtoObject* iterM = mapping->getAttribute(ctx, iterS);
    if (!iterM || iterM == PROTO_NONE) return nullptr;
    const proto::ProtoObject* itObj = callMethod(iterM, mapping, emptyL);
    if (!itObj || itObj == PROTO_NONE) return nullptr;

    // Pin the derived iterator: callMethod calls user __next__ which can
    // trigger GC; without a pin the iterator backing cells can be freed
    // mid-loop (the deep-recursion UAF class fixed in #92's session).
    PythonEnvironment::TransientPin pinIt(env, itObj);

    const proto::ProtoString* nextS = PythonEnvironment::getInternalString(ctx, "__next__");
    const proto::ProtoObject* nextM = itObj->getAttribute(ctx, nextS);
    if (!nextM || nextM == PROTO_NONE) return nullptr;

    const proto::ProtoList* result = ctx->newList();
    for (;;) {
        const proto::ProtoObject* key = callMethod(nextM, itObj, emptyL);
        if (env && env->peekPendingException()) {
            if (env->isStopIteration(ctx, env->peekPendingException()))
                env->clearPendingException();
            break;
        }
        if (!key || key == PROTO_NONE) break;
        // Get value via __getitem__. Pin `key` while we call getItem,
        // since getItem may call user code that triggers GC.
        PythonEnvironment::TransientPin pinKey(env, key);
        const proto::ProtoObject* val = env ? env->getItem(mapping, key, ctx) : PROTO_NONE;
        if (env && env->peekPendingException()) { env->clearPendingException(); val = PROTO_NONE; }
        // Yield as (key, value) tuple
        const proto::ProtoList* pair = ctx->newList()->appendLast(ctx, key)->appendLast(ctx, val ? val : PROTO_NONE);
        result = result->appendLast(ctx, ctx->newTupleFromList(pair)->asObject(ctx));
    }
    return result;
}

/** Mapping.items(): returns a list of (key, value) tuples */
static const proto::ProtoObject* py_mapping_items(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    const proto::ProtoObject* mapping = self;
    // If self is None/null, try args[0]
    if (!mapping || mapping == PROTO_NONE) {
        if (posArgs && posArgs->getSize(ctx) > 0) mapping = posArgs->getAt(ctx, 0);
    }
    const proto::ProtoList* items = collectMappingItems(ctx, mapping);
    return items ? items->asObject(ctx) : ctx->newList()->asObject(ctx);
}

/** Mapping.keys(): returns a list of keys */
static const proto::ProtoObject* py_mapping_keys(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    const proto::ProtoObject* mapping = self;
    if (!mapping || mapping == PROTO_NONE) {
        if (posArgs && posArgs->getSize(ctx) > 0) mapping = posArgs->getAt(ctx, 0);
    }
    if (!mapping || mapping == PROTO_NONE) return ctx->newList()->asObject(ctx);
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoList* emptyL = ctx->newList();

    auto callM = [&](const proto::ProtoObject* method, const proto::ProtoObject* recv) -> const proto::ProtoObject* {
        if (!method || method == PROTO_NONE) return nullptr;
        if (method->asMethod(ctx)) return method->asMethod(ctx)(ctx, const_cast<proto::ProtoObject*>(recv), nullptr, emptyL, nullptr);
        const proto::ProtoList* sa = ctx->newList()->appendLast(ctx, recv);
        return invokePythonCallable(ctx, method, sa, nullptr);
    };

    const proto::ProtoString* iterS = PythonEnvironment::getInternalString(ctx, "__iter__");
    const proto::ProtoObject* iterM = mapping->getAttribute(ctx, iterS);
    const proto::ProtoObject* itObj = iterM ? callM(iterM, mapping) : nullptr;
    if (!itObj || itObj == PROTO_NONE) return ctx->newList()->asObject(ctx);

    // Pin the derived iterator across user __next__ callbacks.
    PythonEnvironment::TransientPin pinIt(env, itObj);

    const proto::ProtoString* nextS = PythonEnvironment::getInternalString(ctx, "__next__");
    const proto::ProtoObject* nextM = itObj->getAttribute(ctx, nextS);
    if (!nextM || nextM == PROTO_NONE) return ctx->newList()->asObject(ctx);

    const proto::ProtoList* result = ctx->newList();
    for (;;) {
        const proto::ProtoObject* key = callM(nextM, itObj);
        if (env && env->peekPendingException()) {
            if (env->isStopIteration(ctx, env->peekPendingException())) env->clearPendingException();
            break;
        }
        if (!key || key == PROTO_NONE) break;
        result = result->appendLast(ctx, key);
    }
    return result->asObject(ctx);
}

/** Mapping.values(): returns a list of values */
static const proto::ProtoObject* py_mapping_values(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    const proto::ProtoObject* mapping = self;
    if (!mapping || mapping == PROTO_NONE) {
        if (posArgs && posArgs->getSize(ctx) > 0) mapping = posArgs->getAt(ctx, 0);
    }
    const proto::ProtoList* items = collectMappingItems(ctx, mapping);
    if (!items) return ctx->newList()->asObject(ctx);
    const proto::ProtoList* result = ctx->newList();
    for (unsigned long i = 0; i < items->getSize(ctx); ++i) {
        const proto::ProtoObject* pair = items->getAt(ctx, i);
        if (pair && pair->isTuple(ctx) && pair->asTuple(ctx)->getSize(ctx) >= 2)
            result = result->appendLast(ctx, pair->asTuple(ctx)->getAt(ctx, 1));
    }
    return result->asObject(ctx);
}

/** Mapping.__contains__(self, key): key in self by attempting self[key]
 *  and treating KeyError as absence. Mirrors the Python reference impl
 *  in _collections_abc.Mapping. */
static const proto::ProtoObject* py_mapping_contains(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!self || !posArgs || posArgs->getSize(ctx) < 1) return PROTO_FALSE;
    const proto::ProtoObject* key = posArgs->getAt(ctx, 0);

    const proto::ProtoString* getItemS = PythonEnvironment::getInternalString(ctx, "__getitem__");
    const proto::ProtoObject* getM = self->getAttribute(ctx, getItemS);
    if (!getM || getM == PROTO_NONE) return PROTO_FALSE;

    const proto::ProtoList* args = ctx->newList()->appendLast(ctx, key);
    const proto::ProtoObject* val = nullptr;
    if (getM->asMethod(ctx)) {
        val = getM->asMethod(ctx)(ctx, const_cast<proto::ProtoObject*>(self), nullptr, args, nullptr);
    } else {
        const proto::ProtoList* selfArgs = ctx->newList()->appendLast(ctx, self)->appendLast(ctx, key);
        val = invokePythonCallable(ctx, getM, selfArgs, nullptr);
    }
    if (env && env->peekPendingException()) {
        // Any exception during __getitem__ (typically KeyError) means the
        // key is not present; clear and return False. Mirrors the .py
        // reference's `try: self[key] except KeyError: return False`. We
        // intentionally absorb broader exceptions here because the ABC
        // stub is the only contract — the original exception was raised
        // out of the user's own __getitem__, and `key in container` per
        // CPython only propagates non-LookupError exceptions; this is the
        // closest behaviour we can offer without exception-class plumbing
        // through the ABC stub.
        env->clearPendingException();
        return PROTO_FALSE;
    }
    (void)val;
    return PROTO_TRUE;
}

/** Minimal __call__ for ABC stub: return new child (for isinstance/callable use). */
static const proto::ProtoObject* py_abc_call(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    return self->newChild(ctx, true);
}

/** MutableMapping.update(self, other=(), **kwargs): mirrors the .py
 *  reference. Without this, every UserDict / ChainMap / OrderedDict
 *  inheriting MutableMapping silently drops its initial data. */
static const proto::ProtoObject* py_mutable_mapping_update(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwArgs) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!self) return PROTO_NONE;

    const proto::ProtoString* setItemS = PythonEnvironment::getInternalString(ctx, "__setitem__");
    const proto::ProtoString* getItemS = PythonEnvironment::getInternalString(ctx, "__getitem__");
    const proto::ProtoString* keysS    = PythonEnvironment::getInternalString(ctx, "keys");
    const proto::ProtoString* iterS    = PythonEnvironment::getInternalString(ctx, "__iter__");
    const proto::ProtoString* nextS    = PythonEnvironment::getInternalString(ctx, "__next__");

    auto invokeBound = [&](const proto::ProtoObject* method,
                           const proto::ProtoObject* receiver,
                           const proto::ProtoList* args) -> const proto::ProtoObject* {
        if (!method || method == PROTO_NONE) return nullptr;
        if (method->asMethod(ctx)) {
            return method->asMethod(ctx)(ctx, const_cast<proto::ProtoObject*>(receiver),
                                          nullptr, args, nullptr);
        }
        // Python callable — prepend receiver as self.
        const proto::ProtoList* selfArgs = ctx->newList()->appendLast(ctx, receiver);
        unsigned long n = args ? args->getSize(ctx) : 0;
        for (unsigned long j = 0; j < n; ++j) selfArgs = selfArgs->appendLast(ctx, args->getAt(ctx, j));
        return invokePythonCallable(ctx, method, selfArgs, nullptr);
    };

    auto storeKV = [&](const proto::ProtoObject* k, const proto::ProtoObject* v) {
        const proto::ProtoObject* setM = self->getAttribute(ctx, setItemS);
        if (!setM || setM == PROTO_NONE) return;
        const proto::ProtoList* setArgs = ctx->newList()->appendLast(ctx, k)->appendLast(ctx, v);
        invokeBound(setM, self, setArgs);
    };

    if (posArgs && posArgs->getSize(ctx) >= 1) {
        const proto::ProtoObject* other = posArgs->getAt(ctx, 0);
        if (other && other != PROTO_NONE) {
            // dict / object exposing .keys() and __getitem__
            const proto::ProtoObject* keysM = other->getAttribute(ctx, keysS);
            const proto::ProtoObject* getM = other->getAttribute(ctx, getItemS);
            bool useKeys = (keysM && keysM != PROTO_NONE);
            if (useKeys) {
                const proto::ProtoObject* keysObj = invokeBound(keysM, other, ctx->newList());
                if (keysObj && keysObj != PROTO_NONE) {
                    // Pin keysObj across the iteration: it lives only on
                    // the C++ stack from here, but every invokeBound call
                    // below may trigger GC.
                    PythonEnvironment::TransientPin pinKeys(env, keysObj);
                    // Iterate keysObj
                    const proto::ProtoList* lst = keysObj->asList(ctx);
                    if (!lst) {
                        // try __iter__
                        const proto::ProtoObject* iterM2 = keysObj->getAttribute(ctx, iterS);
                        const proto::ProtoObject* it = iterM2 ? invokeBound(iterM2, keysObj, ctx->newList()) : nullptr;
                        if (it) {
                            // Pin the derived iterator across user __next__.
                            PythonEnvironment::TransientPin pinIt(env, it);
                            const proto::ProtoObject* nextM2 = it->getAttribute(ctx, nextS);
                            for (;;) {
                                const proto::ProtoObject* k = nextM2 ? invokeBound(nextM2, it, ctx->newList()) : nullptr;
                                if (env && env->peekPendingException()) {
                                    if (env->isStopIteration(ctx, env->peekPendingException()))
                                        env->clearPendingException();
                                    break;
                                }
                                if (!k || k == PROTO_NONE) break;
                                // Pin k while we call getitem (user code).
                                PythonEnvironment::TransientPin pinK(env, k);
                                const proto::ProtoObject* v = nullptr;
                                if (getM && getM != PROTO_NONE) {
                                    const proto::ProtoList* ga = ctx->newList()->appendLast(ctx, k);
                                    v = invokeBound(getM, other, ga);
                                }
                                if (v) storeKV(k, v);
                            }
                        }
                    } else {
                        unsigned long n = lst->getSize(ctx);
                        for (unsigned long i = 0; i < n; ++i) {
                            const proto::ProtoObject* k = lst->getAt(ctx, static_cast<int>(i));
                            const proto::ProtoObject* v = nullptr;
                            if (getM && getM != PROTO_NONE) {
                                const proto::ProtoList* ga = ctx->newList()->appendLast(ctx, k);
                                v = invokeBound(getM, other, ga);
                            }
                            if (v) storeKV(k, v);
                        }
                    }
                }
            } else {
                // Iterable of (k, v) pairs
                const proto::ProtoObject* iterM3 = other->getAttribute(ctx, iterS);
                if (iterM3 && iterM3 != PROTO_NONE) {
                    const proto::ProtoObject* it = invokeBound(iterM3, other, ctx->newList());
                    if (it) {
                        // Pin the iterator across user __next__.
                        PythonEnvironment::TransientPin pinIt(env, it);
                        const proto::ProtoObject* nextM3 = it->getAttribute(ctx, nextS);
                        for (;;) {
                            const proto::ProtoObject* pair = nextM3 ? invokeBound(nextM3, it, ctx->newList()) : nullptr;
                            if (env && env->peekPendingException()) {
                                if (env->isStopIteration(ctx, env->peekPendingException()))
                                    env->clearPendingException();
                                break;
                            }
                            if (!pair || pair == PROTO_NONE) break;
                            const proto::ProtoList* pl = pair->asList(ctx);
                            const proto::ProtoTuple* pt = pair->asTuple(ctx);
                            if (pt) pl = pt->asList(ctx);
                            if (pl && pl->getSize(ctx) >= 2) {
                                storeKV(pl->getAt(ctx, 0), pl->getAt(ctx, 1));
                            }
                        }
                    }
                }
            }
        }
    }
    // NOTE: **kwargs path intentionally minimal — the SparseList carrier
    // exposes processElements but not a direct (key,value) walk by ProtoString
    // here. Most update() callers use the positional form; revisit if a test
    // case needs MutableMapping-subclass.update(**kw) wired through.
    (void)kwArgs;
    return env ? env->getNonePrototype() : PROTO_NONE;
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

    const proto::ProtoString* mroName = proto::ProtoString::createSymbol(ctx, "__mro__");
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

// Invoke `recv.<methodName>(*args)` via the same dispatch path that
// the bytecode interpreter uses for explicit method calls — explicit
// attribute lookup, then asMethod-or-invokePythonCallable. Distinct
// from `env->getItem/setItem/delItem` which use protoCore's internal
// `Object.call(...)` and do NOT walk Python MRO for class instances:
// invoking `__delitem__` via env->delItem on a UserDict is silently
// a no-op because the dunder lives on UserDict's class, not the
// instance. Returns nullptr if the method is missing or raised.
static const proto::ProtoObject* abc_invoke_method(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* recv,
    const proto::ProtoString* methodName,
    const proto::ProtoList* args) {
    if (!recv || !methodName) return nullptr;
    const proto::ProtoObject* method = recv->getAttribute(ctx, methodName);
    if (!method || method == PROTO_NONE) return nullptr;
    if (method->asMethod(ctx)) {
        return method->asMethod(ctx)(ctx, const_cast<proto::ProtoObject*>(recv), nullptr, args, nullptr);
    }
    const proto::ProtoList* sa = ctx->newList()->appendLast(ctx, recv);
    unsigned long n = args ? args->getSize(ctx) : 0;
    for (unsigned long j = 0; j < n; ++j) sa = sa->appendLast(ctx, args->getAt(ctx, j));
    return invokePythonCallable(ctx, method, sa, nullptr);
}

// CPython's `_collections_abc.MutableMapping.pop` mixin: lookup via
// `__getitem__`, delete via `__delitem__`. With the default-argument
// fallback, a missing key returns the default; without one it
// re-raises KeyError. Replaces the silent self.newChild() no-op that
// previously made `mapping.pop(k)` a guaranteed data-loss bug.
static const proto::ProtoObject* py_mutable_mapping_pop(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env || !self || !args || args->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* key = args->getAt(ctx, 0);
    bool hasDefault = args->getSize(ctx) >= 2;
    const proto::ProtoObject* def = hasDefault ? args->getAt(ctx, 1) : PROTO_NONE;

    const proto::ProtoString* getItemS = PythonEnvironment::getInternedString(ctx, "__getitem__");
    const proto::ProtoString* delItemS = PythonEnvironment::getInternedString(ctx, "__delitem__");

    const proto::ProtoList* gargs = ctx->newList()->appendLast(ctx, key);
    const proto::ProtoObject* val = abc_invoke_method(ctx, self, getItemS, gargs);
    if (env->hasPendingException()) {
        const proto::ProtoObject* exc = env->peekPendingException();
        const proto::ProtoObject* keyError = env->lookupName("KeyError");
        if (keyError && env->isException(exc, keyError)) {
            if (hasDefault) {
                env->clearPendingException();
                return def;
            }
            return nullptr;
        }
        return nullptr;
    }
    if (!val) {
        if (hasDefault) return def;
        env->raiseKeyError(ctx, key);
        return nullptr;
    }
    PythonEnvironment::TransientPin pinVal(env, val);
    const proto::ProtoList* dargs = ctx->newList()->appendLast(ctx, key);
    abc_invoke_method(ctx, self, delItemS, dargs);
    if (env->hasPendingException()) return nullptr;
    return val;
}

// CPython's `MutableMapping.popitem` mixin: take an arbitrary
// (key, value), remove it, return as a 2-tuple. Raises KeyError when
// the mapping is empty.
static const proto::ProtoObject* py_mutable_mapping_popitem(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env || !self) return PROTO_NONE;
    const proto::ProtoObject* it = env->iter(self);
    if (!it || env->hasPendingException()) return nullptr;
    PythonEnvironment::TransientPin pinIt(env, it);
    const proto::ProtoObject* key = env->next(it);
    if (!key || env->hasPendingException()) {
        if (env->hasPendingException() && env->isStopIteration(ctx, env->peekPendingException()))
            env->clearPendingException();
        env->raiseKeyError(ctx, PythonEnvironment::getInternedString(ctx, "popitem(): mapping is empty")->asObject(ctx));
        return nullptr;
    }
    PythonEnvironment::TransientPin pinKey(env, key);
    const proto::ProtoString* getItemS = PythonEnvironment::getInternedString(ctx, "__getitem__");
    const proto::ProtoString* delItemS = PythonEnvironment::getInternedString(ctx, "__delitem__");
    const proto::ProtoList* gargs = ctx->newList()->appendLast(ctx, key);
    const proto::ProtoObject* val = abc_invoke_method(ctx, self, getItemS, gargs);
    if (!val || env->hasPendingException()) return nullptr;
    PythonEnvironment::TransientPin pinVal(env, val);
    const proto::ProtoList* dargs = ctx->newList()->appendLast(ctx, key);
    abc_invoke_method(ctx, self, delItemS, dargs);
    if (env->hasPendingException()) return nullptr;
    const proto::ProtoList* pair = ctx->newList()->appendLast(ctx, key)->appendLast(ctx, val);
    return env->newTuple(pair);
}

// CPython's `MutableMapping.clear` mixin: pop until empty.
static const proto::ProtoObject* py_mutable_mapping_clear(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env || !self) return PROTO_NONE;
    for (;;) {
        py_mutable_mapping_popitem(ctx, self, nullptr, nullptr, nullptr);
        if (!env->hasPendingException()) continue;
        const proto::ProtoObject* exc = env->peekPendingException();
        const proto::ProtoObject* keyError = env->lookupName("KeyError");
        if (keyError && env->isException(exc, keyError)) {
            env->clearPendingException();
            return PROTO_NONE;
        }
        return nullptr;
    }
}

// CPython's `MutableMapping.setdefault` mixin: lookup; on KeyError,
// store and return default.
static const proto::ProtoObject* py_mutable_mapping_setdefault(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env || !self || !args || args->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* key = args->getAt(ctx, 0);
    const proto::ProtoObject* def = args->getSize(ctx) >= 2 ? args->getAt(ctx, 1) : PROTO_NONE;

    const proto::ProtoString* getItemS = PythonEnvironment::getInternedString(ctx, "__getitem__");
    const proto::ProtoString* setItemS = PythonEnvironment::getInternedString(ctx, "__setitem__");
    const proto::ProtoList* gargs = ctx->newList()->appendLast(ctx, key);
    const proto::ProtoObject* val = abc_invoke_method(ctx, self, getItemS, gargs);
    if (env->hasPendingException()) {
        const proto::ProtoObject* exc = env->peekPendingException();
        const proto::ProtoObject* keyError = env->lookupName("KeyError");
        if (keyError && env->isException(exc, keyError)) {
            env->clearPendingException();
            const proto::ProtoList* sargs = ctx->newList()->appendLast(ctx, key)->appendLast(ctx, def);
            abc_invoke_method(ctx, self, setItemS, sargs);
            if (env->hasPendingException()) return nullptr;
            return def;
        }
        return nullptr;
    }
    if (!val) {
        const proto::ProtoList* sargs = ctx->newList()->appendLast(ctx, key)->appendLast(ctx, def);
        abc_invoke_method(ctx, self, setItemS, sargs);
        if (env->hasPendingException()) return nullptr;
        return def;
    }
    return val;
}

// CPython's `MutableSequence.append` mixin: insert at end.
static const proto::ProtoObject* py_mutable_sequence_append(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env || !self || !args || args->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* value = args->getAt(ctx, 0);

    const proto::ProtoString* lenS = PythonEnvironment::getInternedString(ctx, "__len__");
    const proto::ProtoString* insS = PythonEnvironment::getInternedString(ctx, "insert");
    const proto::ProtoObject* lenM = self->getAttribute(ctx, lenS);
    const proto::ProtoObject* insM = self->getAttribute(ctx, insS);
    if (!lenM || lenM == PROTO_NONE || !insM || insM == PROTO_NONE) {
        env->raiseTypeError(ctx, "append: subclass must provide __len__ and insert");
        return nullptr;
    }
    auto invoke = [&](const proto::ProtoObject* m, const proto::ProtoObject* recv,
                      const proto::ProtoList* a) -> const proto::ProtoObject* {
        if (m->asMethod(ctx))
            return m->asMethod(ctx)(ctx, const_cast<proto::ProtoObject*>(recv), nullptr, a, nullptr);
        const proto::ProtoList* sa = ctx->newList()->appendLast(ctx, recv);
        unsigned long n = a ? a->getSize(ctx) : 0;
        for (unsigned long j = 0; j < n; ++j) sa = sa->appendLast(ctx, a->getAt(ctx, j));
        return invokePythonCallable(ctx, m, sa, nullptr);
    };
    const proto::ProtoObject* lenObj = invoke(lenM, self, ctx->newList());
    if (!lenObj || env->hasPendingException()) return nullptr;
    long n = lenObj->asLong(ctx);
    const proto::ProtoList* a = ctx->newList()->appendLast(ctx, ctx->fromInteger(n))->appendLast(ctx, value);
    invoke(insM, self, a);
    return PROTO_NONE;
}

// CPython's `MutableSequence.extend` mixin: append every element of an
// iterable. Pins the iterator across user code per GC discipline.
static const proto::ProtoObject* py_mutable_sequence_extend(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env || !self || !args || args->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* iterable = args->getAt(ctx, 0);

    const proto::ProtoObject* it = env->iter(iterable);
    if (!it || env->hasPendingException()) return nullptr;
    PythonEnvironment::TransientPin pinIt(env, it);
    for (;;) {
        const proto::ProtoObject* v = env->next(it);
        if (!v) {
            if (env->hasPendingException() && env->isStopIteration(ctx, env->peekPendingException()))
                env->clearPendingException();
            break;
        }
        PythonEnvironment::TransientPin pinV(env, v);
        const proto::ProtoList* a = ctx->newList()->appendLast(ctx, v);
        py_mutable_sequence_append(ctx, self, nullptr, a, nullptr);
        if (env->hasPendingException()) return nullptr;
    }
    return PROTO_NONE;
}

// Generic stub for ABC methods that have NO default implementation in
// CPython's `_collections_abc` (insert, remove, reverse, index, count,
// Set boolean ops, etc.). Replaces the previous self.newChild() no-op
// with a clear TypeError so silent corruption becomes a loud failure.
#define ABC_ABSTRACT_STUB(name) \
static const proto::ProtoObject* py_abc_abstract_##name( \
    proto::ProtoContext* ctx, const proto::ProtoObject*, \
    const proto::ParentLink*, const proto::ProtoList*, \
    const proto::ProtoSparseList*) { \
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx); \
    if (env) env->raiseTypeError(ctx, std::string("abstract method '") + #name \
        + "' on _collections_abc class must be overridden by the subclass"); \
    return nullptr; \
}

ABC_ABSTRACT_STUB(insert)
ABC_ABSTRACT_STUB(remove)
ABC_ABSTRACT_STUB(reverse)
ABC_ABSTRACT_STUB(index)
ABC_ABSTRACT_STUB(count)

static const proto::ProtoObject* py_abc_get(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* args,
    const proto::ProtoSparseList*) {
    if (!self || !args || args->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* key = args->getAt(ctx, 0);
    const proto::ProtoObject* def = (args->getSize(ctx) > 1) ? args->getAt(ctx, 1) : PROTO_NONE;
    
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return def;

    const proto::ProtoObject* val = env->getItem(self, key, ctx);
    if (val && val != PROTO_NONE) return val;
    if (val == PROTO_NONE) return val; // If found and is None, return None
    
    if (env->hasPendingException()) {
        const proto::ProtoObject* exc = env->peekPendingException();
        const proto::ProtoObject* keyError = env->lookupName("KeyError");
        if (keyError && env->isException(exc, keyError)) {
            env->clearPendingException();
            return def;
        }
    }
    return def;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    auto createAbc = [&](const char* name) {
        proto::ProtoObject* abc = const_cast<proto::ProtoObject*>(ctx->newObject(false));
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        
        const proto::ProtoString* s_call = env ? env->getCallString() : PythonEnvironment::getInternedString(ctx, "__call__");
        const proto::ProtoString* s_name = env ? env->getNameString() : PythonEnvironment::getInternedString(ctx, "__name__");
        const proto::ProtoString* s_class = env ? env->getClassString() : PythonEnvironment::getInternedString(ctx, "__class__");
        const proto::ProtoString* s_register = PythonEnvironment::getInternedString(ctx, "register");
        const proto::ProtoString* s_is_py_class = PythonEnvironment::getInternedString(ctx, "__is_python_class__");
        const proto::ProtoString* s_bases = env ? env->getBasesString() : PythonEnvironment::getInternedString(ctx, "__bases__");

        abc = const_cast<proto::ProtoObject*>(abc->setAttribute(ctx, s_call, ctx->fromMethod(nullptr, py_abc_call)));
        abc = const_cast<proto::ProtoObject*>(abc->setAttribute(ctx, s_register, ctx->fromMethod(nullptr, py_abc_register)));
        abc = const_cast<proto::ProtoObject*>(abc->setAttribute(ctx, s_name, PythonEnvironment::getInternedString(ctx, name)->asObject(ctx)));
        
        // Set __class__ to type Prototype
        if (env) {
            abc = const_cast<proto::ProtoObject*>(abc->setAttribute(ctx, s_class, env->getTypePrototype()));
        }

        // Explicitly mark as a Python class for getType heuristic
        abc = const_cast<proto::ProtoObject*>(abc->setAttribute(ctx, s_is_py_class, PROTO_TRUE));

        // 1. Add dummy methods to satisfy collections/__init__.py inheritance of methods
        const char* methods[] = {
            "update", "get", "keys", "values", "items", "pop", "popitem", "clear",
            "setdefault", "index", "count", "append", "extend", "insert", "remove", "reverse",
            "__eq__", "__ne__", "__lt__", "__gt__", "__ge__",
            "__iter__", "__len__", "__contains__", "__hash__"
        };
        bool isMapping = (strcmp(name, "Mapping") == 0 || strcmp(name, "MutableMapping") == 0);
        bool isMutableMapping = (strcmp(name, "MutableMapping") == 0);
        bool isMutableSequence = (strcmp(name, "MutableSequence") == 0);
        for (const char* m : methods) {
            const proto::ProtoObject* impl = ctx->fromMethod(nullptr, py_abc_call);
            if (isMapping && strcmp(m, "get") == 0) impl = ctx->fromMethod(nullptr, py_abc_get);
            else if (isMapping && strcmp(m, "items") == 0) impl = ctx->fromMethod(nullptr, py_mapping_items);
            else if (isMapping && strcmp(m, "keys") == 0) impl = ctx->fromMethod(nullptr, py_mapping_keys);
            else if (isMapping && strcmp(m, "values") == 0) impl = ctx->fromMethod(nullptr, py_mapping_values);
            else if (isMutableMapping && strcmp(m, "update") == 0) impl = ctx->fromMethod(nullptr, py_mutable_mapping_update);
            else if (isMutableMapping && strcmp(m, "pop") == 0) impl = ctx->fromMethod(nullptr, py_mutable_mapping_pop);
            else if (isMutableMapping && strcmp(m, "popitem") == 0) impl = ctx->fromMethod(nullptr, py_mutable_mapping_popitem);
            else if (isMutableMapping && strcmp(m, "clear") == 0) impl = ctx->fromMethod(nullptr, py_mutable_mapping_clear);
            else if (isMutableMapping && strcmp(m, "setdefault") == 0) impl = ctx->fromMethod(nullptr, py_mutable_mapping_setdefault);
            else if (isMutableSequence && strcmp(m, "append") == 0) impl = ctx->fromMethod(nullptr, py_mutable_sequence_append);
            else if (isMutableSequence && strcmp(m, "extend") == 0) impl = ctx->fromMethod(nullptr, py_mutable_sequence_extend);
            else if (isMutableSequence && strcmp(m, "insert") == 0) impl = ctx->fromMethod(nullptr, py_abc_abstract_insert);
            else if (isMutableSequence && strcmp(m, "remove") == 0) impl = ctx->fromMethod(nullptr, py_abc_abstract_remove);
            else if (isMutableSequence && strcmp(m, "reverse") == 0) impl = ctx->fromMethod(nullptr, py_abc_abstract_reverse);
            else if ((isMutableSequence || strcmp(name, "Sequence") == 0) && strcmp(m, "index") == 0) impl = ctx->fromMethod(nullptr, py_abc_abstract_index);
            else if ((isMutableSequence || strcmp(name, "Sequence") == 0) && strcmp(m, "count") == 0) impl = ctx->fromMethod(nullptr, py_abc_abstract_count);
            else if (isMapping && strcmp(m, "__contains__") == 0) impl = ctx->fromMethod(nullptr, py_mapping_contains);
            abc = const_cast<proto::ProtoObject*>(abc->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, m), impl));
        }

        // 2. Inherit from object to have a valid MRO
        if (env) {
            const proto::ProtoObject* objProto = env->getObjectPrototype();
            if (objProto) {
                abc = const_cast<proto::ProtoObject*>(abc->addParent(ctx, objProto));

                // Set __bases__ to (object,)
                const proto::ProtoList* bases = ctx->newList();
                bases = bases->appendLast(ctx, objProto);
                abc = const_cast<proto::ProtoObject*>(abc->setAttribute(ctx, s_bases, env->newTuple(bases)));

                // Set __mro__ = (abc, object) so subclass MRO computation includes this ABC.
                const proto::ProtoList* mroList = ctx->newList();
                mroList = mroList->appendLast(ctx, abc);
                mroList = mroList->appendLast(ctx, objProto);
                abc = const_cast<proto::ProtoObject*>(abc->setAttribute(ctx,
                    PythonEnvironment::getInternedString(ctx, "__mro__"),
                    ctx->newTupleFromList(mroList)->asObject(ctx)));
            }
        }

        return abc;
    };

    const proto::ProtoObject* mod = ctx->newObject(false);
    const char* names[] = {
        "Hashable", "Iterable", "Iterator", "Reversible", "Sized", "Container", "Collection",
        "Mapping", "MutableMapping", "Sequence", "MutableSequence",
        "Set", "MutableSet", "Callable", "Awaitable", "Coroutine",
        "AsyncIterable", "AsyncIterator", "AsyncGenerator", "Generator",
        "KeysView", "ValuesView", "ItemsView", "MappingView", "ByteString",
        // Buffer was added to _collections_abc in Python 3.12 (PEP 688).
        // test_collections.py imports it directly: `from _collections_abc
        // import Buffer`. Expose the same minimal ABC stub the others use.
        "Buffer"
    };

    for (const char* name : names) {
        const proto::ProtoString* sName = PythonEnvironment::getInternedString(ctx, name);
        mod = mod->setAttribute(ctx, sName, createAbc(name));
    }

    const proto::ProtoObject* checkMethodsFunc = ctx->fromMethod(nullptr, py_check_methods);
    const proto::ProtoString* sCheck = PythonEnvironment::getInternedString(ctx, "_check_methods");
    mod = mod->setAttribute(ctx, sCheck, checkMethodsFunc);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "check_methods"), checkMethodsFunc);

    const proto::ProtoObject* abcMeta = createAbc("ABCMeta");
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "ABCMeta"), abcMeta);

    static const auto py_abstractmethod = [](proto::ProtoContext*, const proto::ProtoObject*, const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*) {
        if (args->getSize(nullptr) < 1) return PROTO_NONE;
        return args->getAt(nullptr, 0);
    };
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "abstractmethod"), ctx->fromMethod(nullptr, py_abstractmethod));

    const proto::ProtoList* allList = ctx->newList();
    for (const char* name : names) {
        allList = allList->appendLast(ctx, PythonEnvironment::getInternedString(ctx, name)->asObject(ctx));
    }
    allList = allList->appendLast(ctx, PythonEnvironment::getInternedString(ctx, "_check_methods")->asObject(ctx));
    allList = allList->appendLast(ctx, PythonEnvironment::getInternedString(ctx, "ABCMeta")->asObject(ctx));
    const proto::ProtoObject* allObj = env ? env->newList(allList) : allList->asObject(ctx);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__all__"), allObj);

    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__name__"), PythonEnvironment::getInternedString(ctx, "_collections_abc")->asObject(ctx));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__loader__"), PROTO_NONE); 
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__executed__"), PROTO_TRUE); 

    return mod;
}

} // namespace collections_abc
} // namespace protoPython
