#include <protoCore.h>
#include <protoPython/PythonEnvironment.h>

// Weak references via *presence-set registry*. Uses protoCore
// primitives only — no C++ mutexes, no raw pointers, no finalizer
// hooks. ProtoCore's existing setAttribute / SparseList atomicity
// is the only synchronisation in play, matching protoPython's
// "no language-level sync, only at protoCore" architectural rule.
//
// Model
// -----
// The module owns an `__active__` SparseList that holds the
// currently-active set of objects keyed by obj-identity (the
// protoCore handle hashed to an integer). `weakref.ref(obj)` adds
// `obj` to `__active__` and returns a small handle that records
// the key. Calling the handle does a key-based lookup:
//   - If found → return the object.
//   - If absent → return None, OR call `_reload_hook` if the app
//     has registered one.
//
// Liveness comes from `__active__` membership: the active set is
// the only strong reference protoPython holds; user-handles store
// only the integer key. Once the app evicts (`weakref._evict(obj)`)
// AND the user has no other reference, the object is collectable
// normally. Optional reload lets evicted entries be re-materialised
// — a capability CPython's auto-on-GC weakref cannot express.
//
// Module state
// ------------
// The module is mutable so __active__ updates write through in
// place via setAttribute (immutable would silently discard each
// rebind because setAttribute returns a fresh wrapper that
// sys.modules' pinned reference doesn't see). protoCore's
// setAttribute is the only synchronisation point and is itself
// thread-safe.
//
// Known issue (2026-05-08): with this mutable-module design,
// `test.support.import_helper.import_fresh_module(...)` regresses
// test_grammar.test_var_annot_in_module with "'type' object has
// no attribute 'append'". The error path runs during fresh import
// of test.typinganndata.ann_module3 and does not reproduce when
// the weakref module is built with newObject(false) [the previous
// stub]. The interaction is presumed to be a latent bug somewhere
// in the import or class-instantiation path, exposed by but not
// caused by the contents of this file. Tracked as a follow-up.

namespace protoPython {
namespace weakref {

static const proto::ProtoString* sym(proto::ProtoContext* ctx, const char* n) {
    return proto::ProtoString::createSymbol(ctx, n);
}

// Stable per-object key — protoCore handles don't move, so the
// pointer cast is a stable identity for the object's lifetime in
// `__active__`.
static unsigned long obj_key(const proto::ProtoObject* obj) {
    return static_cast<unsigned long>(reinterpret_cast<uintptr_t>(obj));
}

static const proto::ProtoSparseList* active_get(
    proto::ProtoContext* ctx, const proto::ProtoObject* mod) {
    if (!mod || mod == PROTO_NONE) return ctx->newSparseList();
    const proto::ProtoObject* a = mod->getAttribute(ctx, sym(ctx, "__active__"));
    if (a && a != PROTO_NONE) {
        const proto::ProtoSparseList* sl = a->asSparseList(ctx);
        if (sl) return sl;
    }
    return ctx->newSparseList();
}

static void active_set(proto::ProtoContext* ctx,
                       const proto::ProtoObject* mod,
                       const proto::ProtoSparseList* sl) {
    if (!mod || mod == PROTO_NONE) return;
    const_cast<proto::ProtoObject*>(mod)->setAttribute(ctx,
        sym(ctx, "__active__"), sl->asObject(ctx));
}

// Lookup helper: read self._wr_key, look up in module's active set,
// fall through to optional reload hook. Pulled out so both the
// instance __call__ branch and the dual-purpose ref() entry can
// share it.
static const proto::ProtoObject* lookup_via_handle(
    proto::ProtoContext* ctx, const proto::ProtoObject* handle) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env || !handle) return PROTO_NONE;

    const proto::ProtoObject* keyObj = handle->getAttribute(ctx, sym(ctx, "_wr_key"));
    if (!keyObj || !keyObj->isInteger(ctx)) return PROTO_NONE;
    unsigned long key = static_cast<unsigned long>(keyObj->asLong(ctx));

    const proto::ProtoObject* mod = env->lookupName("_weakref");
    if (!mod || mod == PROTO_NONE) return PROTO_NONE;

    const proto::ProtoSparseList* active = active_get(ctx, mod);
    const proto::ProtoObject* found = active->getAt(ctx, key);
    if (found && found != PROTO_NONE) return found;

    const proto::ProtoObject* hook = mod->getAttribute(ctx, sym(ctx, "_reload_hook"));
    if (hook && hook != PROTO_NONE && hook != PROTO_FALSE) {
        std::vector<const proto::ProtoObject*> args;
        args.push_back(ctx->fromInteger(static_cast<long>(key)));
        const proto::ProtoObject* reloaded = env->callObject(hook, args);
        if (reloaded && reloaded != PROTO_NONE) {
            active = active->setAt(ctx, key, reloaded);
            active_set(ctx, mod, active);
            return reloaded;
        }
    }
    return PROTO_NONE;
}

// py_weakref_ref serves both the "create new ref" path and the
// "call existing ref to dereference it" path:
//   - `weakref.ref(obj)` → posArgs = [obj] (or [refType, obj] when
//     class.__call__ prepends cls). Action: register obj in
//     __active__, return handle.
//   - `r()` (where r is a handle) → posArgs = []. Action: lookup
//     r._wr_key in __active__, return target / reload-result / None.
// The disambiguator is `n == 0` (the create path always has at
// least one arg — the target).
static const proto::ProtoObject* py_weakref_ref(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    unsigned long n = posArgs ? posArgs->getSize(ctx) : 0;

    // Dereference path.
    if (n == 0) return lookup_via_handle(ctx, self);

    const proto::ProtoObject* mod = env ? env->lookupName("_weakref") : nullptr;
    const proto::ProtoObject* refType = (mod && mod != PROTO_NONE)
        ? mod->getAttribute(ctx, sym(ctx, "ReferenceType")) : nullptr;

    // Detect cls-prepended call. CPython's type.__call__ passes the
    // class as the first positional arg to __new__. The class might
    // be ReferenceType itself OR a user-defined subclass — e.g.
    // importlib's `_WeakValueDictionary._KeyedRef(_weakref.ref)`
    // subclasses our type and calls `_weakref.ref.__new__(type, …)`,
    // which delivers `type=KeyedRef` (a SUBCLASS, not refType
    // directly). Direct equality misses that case and treated the
    // KeyedRef class itself as the target — registering KeyedRef in
    // the active set, then returning it from setdefault, then
    // exploding on `.append` because a class isn't a list. The
    // canonical detector is "first arg is a class" (has __mro__ as
    // an own attribute), not pointer-equal to refType.
    // "First arg is a class" detector. Two cases must both classify
    // as class-prepended-call:
    //   1. refType itself — built in C++, with pyType as parent and
    //      `__new__`/`__call__` set, but NO `__mro__` as an own attr
    //      (protoCore's addParent doesn't synthesise it). hasOwnAttr
    //      misses this.
    //   2. KeyedRef — built in Python via `class KeyedRef(ref)`, has
    //      `__mro__` as an own attr (the protoPython compiler emits it).
    // Both share `type(arg) == type` (the metaclass). That's the
    // canonical check.
    unsigned long base = 0;
    if (n >= 2 && env) {
        const proto::ProtoObject* a0 = posArgs->getAt(ctx, 0);
        if (a0 && a0 != PROTO_NONE) {
            const proto::ProtoObject* a0Type = env->getType(ctx, a0);
            const proto::ProtoObject* typeProto = env->getTypePrototype();
            if (a0Type == typeProto) base = 1;
        }
    }
    if (n <= base) return PROTO_NONE;

    const proto::ProtoObject* target = posArgs->getAt(ctx, static_cast<int>(base));
    if (!target || target == PROTO_NONE) return PROTO_NONE;

    // STRUCT-41: reject targets whose type cannot grow a weak reference —
    // mirrors py_object_get_weakref's classification.  CPython raises
    // `TypeError: cannot create weak reference to 'X' object` when the
    // instance type is an immutable-primitive built-in (int, str, …) or
    // declares `__slots__` without `__weakref__`.
    if (env) {
        const proto::ProtoObject* tp = env->getType(ctx, target);
        auto blocksWeakref = [&](const proto::ProtoObject* c) -> bool {
            return c == env->getIntPrototype()
                || c == env->getBoolPrototype()
                || c == env->getFloatPrototype()
                || c == env->getComplexPrototype()
                || c == env->getStrPrototype()
                || c == env->getBytesPrototype()
                || c == env->getTuplePrototype()
                || c == env->getFrozensetPrototype()
                || c == env->getTypePrototype();
        };
        std::string tname = "object";
        if (tp) {
            const proto::ProtoObject* nm = tp->getAttribute(ctx,
                PythonEnvironment::getInternedString(ctx, "__name__"));
            if (nm && nm->isString(ctx)) nm->asString(ctx)->toUTF8String(ctx, tname);
        }
        // STRUCT-291: walk MRO for both blockers and explicit
        // __weakref__ opt-ins.  A subclass of a blocked primitive
        // can still enable weakref via `__slots__=['__weakref__']`;
        // mirror py_object_get_weakref's logic.
        const proto::ProtoString* mroS = PythonEnvironment::getInternedString(ctx, "__mro__");
        const proto::ProtoObject* mroAttr = tp ? env->getAttribute(ctx, tp, mroS, false) : nullptr;
        const proto::ProtoTuple* mroT = mroAttr ? mroAttr->asTuple(ctx) : nullptr;
        bool hasBlocker = false;
        bool hasExplicitWeakrefSlot = false;
        const proto::ProtoString* slotsChkS =
            PythonEnvironment::getInternedString(ctx, "__slots__");
        auto baseDeclaresWeakrefBlk = [&](const proto::ProtoObject* base) -> bool {
            if (!base || base->hasOwnAttribute(ctx, slotsChkS) != PROTO_TRUE) return false;
            const proto::ProtoObject* sv = base->getOwnAttributeDirect(ctx, slotsChkS);
            if (!sv) return false;
            const proto::ProtoList* sL = sv->asList(ctx);
            const proto::ProtoTuple* sT = sv->asTuple(ctx);
            unsigned long n = sL ? sL->getSize(ctx)
                              : (sT ? sT->getSize(ctx) : 0);
            for (unsigned long i = 0; i < n; ++i) {
                const proto::ProtoObject* item = sL
                    ? sL->getAt(ctx, static_cast<int>(i))
                    : sT->getAt(ctx, static_cast<int>(i));
                if (!item || !item->isString(ctx)) continue;
                std::string nm; item->asString(ctx)->toUTF8String(ctx, nm);
                if (nm == "__weakref__") return true;
            }
            if (sv->isString(ctx)) {
                std::string nm; sv->asString(ctx)->toUTF8String(ctx, nm);
                if (nm == "__weakref__") return true;
            }
            return false;
        };
        if (mroT) {
            for (unsigned long i = 0; i < mroT->getSize(ctx); ++i) {
                const proto::ProtoObject* anc = mroT->getAt(ctx, static_cast<int>(i));
                if (!anc || anc == PROTO_NONE) continue;
                if (blocksWeakref(anc)) hasBlocker = true;
                if (baseDeclaresWeakrefBlk(anc)) hasExplicitWeakrefSlot = true;
            }
        } else if (tp) {
            if (blocksWeakref(tp)) hasBlocker = true;
            if (baseDeclaresWeakrefBlk(tp)) hasExplicitWeakrefSlot = true;
        }
        bool blocked = (hasBlocker && !hasExplicitWeakrefSlot);
        if (!blocked && tp) {
            // STRUCT-262: walk MRO for weakref availability — a base
            // that declares `__weakref__` in __slots__ or that has no
            // __slots__ at all enables weakref on instances of `tp`.
            const proto::ProtoString* slotsS =
                PythonEnvironment::getInternedString(ctx, "__slots__");
            auto containsWeakref = [&](const proto::ProtoObject* base) -> bool {
                if (base->hasOwnAttribute(ctx, slotsS) != PROTO_TRUE) return false;
                const proto::ProtoObject* slotsAttr =
                    base->getOwnAttributeDirect(ctx, slotsS);
                if (!slotsAttr) return false;
                const proto::ProtoList* slotsL = slotsAttr->asList(ctx);
                const proto::ProtoTuple* slotsTp = slotsAttr->asTuple(ctx);
                unsigned long nn = slotsL ? slotsL->getSize(ctx)
                                  : (slotsTp ? slotsTp->getSize(ctx) : 0);
                for (unsigned long i = 0; i < nn; ++i) {
                    const proto::ProtoObject* item = slotsL
                        ? slotsL->getAt(ctx, static_cast<int>(i))
                        : slotsTp->getAt(ctx, static_cast<int>(i));
                    if (!item || !item->isString(ctx)) continue;
                    std::string nm; item->asString(ctx)->toUTF8String(ctx, nm);
                    if (nm == "__weakref__") return true;
                }
                if (slotsAttr->isString(ctx)) {
                    std::string nm; slotsAttr->asString(ctx)->toUTF8String(ctx, nm);
                    if (nm == "__weakref__") return true;
                }
                return false;
            };
            bool weakrefAvailable = false;
            if (mroT) {
                for (unsigned long i = 0; i < mroT->getSize(ctx); ++i) {
                    const proto::ProtoObject* base = mroT->getAt(ctx, i);
                    if (!base || base == PROTO_NONE) continue;
                    if (base == env->getObjectPrototype()) continue;
                    if (containsWeakref(base)) { weakrefAvailable = true; break; }
                    if (base->hasOwnAttribute(ctx, slotsS) != PROTO_TRUE) {
                        weakrefAvailable = true; break;
                    }
                }
            } else if (tp != env->getObjectPrototype()) {
                if (containsWeakref(tp)
                    || tp->hasOwnAttribute(ctx, slotsS) != PROTO_TRUE) {
                    weakrefAvailable = true;
                }
            }
            if (!weakrefAvailable) blocked = true;
        }
        if (blocked) {
            env->raiseTypeError(ctx,
                "cannot create weak reference to '" + tname + "' object");
            return nullptr;
        }
    }

    unsigned long key = obj_key(target);

    if (mod && mod != PROTO_NONE) {
        const proto::ProtoSparseList* active = active_get(ctx, mod);
        active = active->setAt(ctx, key, target);
        active_set(ctx, mod, active);
    }

    proto::ProtoObject* refObj = const_cast<proto::ProtoObject*>(ctx->newObject(false));
    if (refType && refType != PROTO_NONE) {
        refObj = const_cast<proto::ProtoObject*>(refObj->addParent(ctx, refType));
    }
    refObj = const_cast<proto::ProtoObject*>(refObj->setAttribute(ctx,
        sym(ctx, "_wr_key"), ctx->fromInteger(static_cast<long>(key))));
    if (n > base + 1) {
        refObj = const_cast<proto::ProtoObject*>(refObj->setAttribute(ctx,
            sym(ctx, "callback"), posArgs->getAt(ctx, static_cast<int>(base + 1))));
    }
    return refObj;
}

static const proto::ProtoObject* py_weakref_proxy(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    return py_weakref_ref(ctx, self, nullptr, posArgs, nullptr);
}

static const proto::ProtoObject* py_weakref_evict(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env || !posArgs || posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* arg = posArgs->getAt(ctx, 0);
    unsigned long key = arg->isInteger(ctx)
        ? static_cast<unsigned long>(arg->asLong(ctx))
        : obj_key(arg);
    const proto::ProtoObject* mod = env->lookupName("_weakref");
    if (!mod || mod == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoSparseList* active = active_get(ctx, mod);
    active = active->removeAt(ctx, key);
    active_set(ctx, mod, active);
    return PROTO_NONE;
}

static const proto::ProtoObject* py_weakref_getweakrefcount(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env || !posArgs || posArgs->getSize(ctx) < 1) return ctx->fromInteger(0);
    const proto::ProtoObject* mod = env->lookupName("_weakref");
    if (!mod || mod == PROTO_NONE) return ctx->fromInteger(0);
    unsigned long key = obj_key(posArgs->getAt(ctx, 0));
    const proto::ProtoSparseList* active = active_get(ctx, mod);
    const proto::ProtoObject* found = active->getAt(ctx, key);
    return ctx->fromInteger((found && found != PROTO_NONE) ? 1 : 0);
}

static const proto::ProtoObject* py_weakref_getweakrefs(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return ctx->newList()->asObject(ctx);
}

static const proto::ProtoObject* py_weakref_remove_dead_weakref(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (!posArgs || posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* dct = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* key = posArgs->getAt(ctx, 1);
    if (dct && key) {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        if (env) env->delItem(dct, key);
    }
    return PROTO_NONE;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    proto::ProtoObject* mod = const_cast<proto::ProtoObject*>(ctx->newObject(true));
    mod->setAttribute(ctx, sym(ctx, "__active__"),
        ctx->newSparseList()->asObject(ctx));
    mod->setAttribute(ctx, sym(ctx, "_reload_hook"), PROTO_NONE);
    mod->setAttribute(ctx, sym(ctx, "getweakrefcount"),
        ctx->fromMethod(mod, py_weakref_getweakrefcount));
    mod->setAttribute(ctx, sym(ctx, "getweakrefs"),
        ctx->fromMethod(mod, py_weakref_getweakrefs));
    mod->setAttribute(ctx, sym(ctx, "_remove_dead_weakref"),
        ctx->fromMethod(mod, py_weakref_remove_dead_weakref));
    mod->setAttribute(ctx, sym(ctx, "_evict"),
        ctx->fromMethod(mod, py_weakref_evict));

    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (env) {
        const proto::ProtoObject* pyType = env->lookupName("type");

        proto::ProtoObject* refType = const_cast<proto::ProtoObject*>(ctx->newObject(false));
        if (pyType && pyType != PROTO_NONE)
            refType = const_cast<proto::ProtoObject*>(refType->addParent(ctx, pyType));
        refType = const_cast<proto::ProtoObject*>(refType->setAttribute(ctx,
            sym(ctx, "__name__"),
            PythonEnvironment::getInternedString(ctx, "weakref")->asObject(ctx)));
        refType = const_cast<proto::ProtoObject*>(refType->setAttribute(ctx,
            sym(ctx, "__new__"), ctx->fromMethod(nullptr, py_weakref_ref)));
        refType = const_cast<proto::ProtoObject*>(refType->setAttribute(ctx,
            sym(ctx, "__call__"), ctx->fromMethod(refType, py_weakref_ref)));
        mod->setAttribute(ctx, sym(ctx, "ReferenceType"), refType);
        mod->setAttribute(ctx, sym(ctx, "ref"), refType);

        proto::ProtoObject* proxyType = const_cast<proto::ProtoObject*>(ctx->newObject(false));
        if (pyType && pyType != PROTO_NONE)
            proxyType = const_cast<proto::ProtoObject*>(proxyType->addParent(ctx, pyType));
        proxyType = const_cast<proto::ProtoObject*>(proxyType->setAttribute(ctx,
            sym(ctx, "__name__"),
            PythonEnvironment::getInternedString(ctx, "weakproxy")->asObject(ctx)));
        proxyType = const_cast<proto::ProtoObject*>(proxyType->setAttribute(ctx,
            sym(ctx, "__new__"), ctx->fromMethod(nullptr, py_weakref_proxy)));
        proxyType = const_cast<proto::ProtoObject*>(proxyType->setAttribute(ctx,
            sym(ctx, "__call__"), ctx->fromMethod(proxyType, py_weakref_proxy)));
        mod->setAttribute(ctx, sym(ctx, "ProxyType"), proxyType);
        mod->setAttribute(ctx, sym(ctx, "proxy"), proxyType);

        proto::ProtoObject* callableProxyType = const_cast<proto::ProtoObject*>(ctx->newObject(false));
        if (pyType && pyType != PROTO_NONE)
            callableProxyType = const_cast<proto::ProtoObject*>(callableProxyType->addParent(ctx, pyType));
        callableProxyType = const_cast<proto::ProtoObject*>(callableProxyType->setAttribute(ctx,
            sym(ctx, "__name__"),
            PythonEnvironment::getInternedString(ctx, "weakcallableproxy")->asObject(ctx)));
        callableProxyType = const_cast<proto::ProtoObject*>(callableProxyType->setAttribute(ctx,
            sym(ctx, "__new__"), ctx->fromMethod(nullptr, py_weakref_proxy)));
        callableProxyType = const_cast<proto::ProtoObject*>(callableProxyType->setAttribute(ctx,
            sym(ctx, "__call__"), ctx->fromMethod(callableProxyType, py_weakref_proxy)));
        mod->setAttribute(ctx, sym(ctx, "CallableProxyType"), callableProxyType);
    }

    return mod;
}

} // namespace weakref
} // namespace protoPython
