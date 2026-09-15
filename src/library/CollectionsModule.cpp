#include <protoPython/CollectionsModule.h>
#include <protoPython/PythonEnvironment.h>
#include <atomic>
#include <deque>
#include <mutex>

namespace protoPython {
namespace collections {

// Thread safety: per-instance mutex in DequeState protects mutable shared state (user-level locking, not GIL).
// L-Shape: acceptable; see docs/L_SHAPE_ARCHITECTURE.md. See also docs/GIL_FREE_AUDIT.md.
// Native state of a deque. The items are a ProtoList in the instance's
// __deque_items__ attribute, so the GC traces them: a std::deque of raw
// pointers behind the external pointer holding this state was invisible to it.
// Mutations publish a new list with a compare-and-swap, as list mutators do.
struct DequeState {
    std::atomic<long> maxlen{-1}; // CPython's maxlen; -1 when unbounded
};

static const proto::ProtoObject* py_collections_dummy(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    return self->newChild(ctx, true);
}

static const proto::ProtoObject* py_tuple_getter_get(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (!posArgs || posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* instance = posArgs->getAt(ctx, 0);
    // Class-level access (`NT.a`): Python's descriptor protocol passes
    // None as `instance` — return self so callers can introspect the
    // descriptor.  Previously this fell through and returned PROTO_NONE,
    // so the namedtuple class itself looked like every field had been
    // wiped out by the time consumer code asked.
    if (!instance || instance == PROTO_NONE) return self;
    const proto::ProtoObject* indexObj = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__index__"));
    if (!indexObj) return PROTO_NONE;
    long long index = indexObj->asLong(ctx);
    // namedtuple instances may either be a raw ProtoTuple shape or a
    // wrapped Python tuple/list object that stores the sequence in
    // __data__.  Try both so we work for either layout.
    const proto::ProtoTuple* tup = instance->asTuple(ctx);
    if (!tup) {
        const proto::ProtoObject* data = instance->getAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "__data__"));
        if (data) tup = data->asTuple(ctx);
    }
    if (tup && index >= 0 && (size_t)index < tup->getSize(ctx)) {
        return tup->getAt(ctx, (size_t)index);
    }
    return PROTO_NONE;
}

// Module-level prototype for _tuplegetter instances.  The Python descriptor
// protocol consults `type(descriptor).__get__`, so the descriptor's TYPE
// must carry the __get__ binding — putting it on the instance alone (the
// previous shape) silently bypasses descriptor invocation and `nt.field`
// resolves to the raw descriptor object.  Visible at platform.uname()
// where uname_result.release etc. are namedtuple fields backed by
// _tuplegetter — accessing .release returned the descriptor and breaks
// any downstream string operation (.split / .startswith / etc.).
static const proto::ProtoObject* g_tuplegetter_proto = nullptr;

static const proto::ProtoObject* py_tuplegetter(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (!posArgs || posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* index = posArgs->getAt(ctx, 0);
    if (!g_tuplegetter_proto) {
        // Lazy init: build the prototype the first time anyone calls
        // _tuplegetter.  __get__ lives on the prototype so it is found by
        // type(instance).__get__ during descriptor protocol resolution.
        proto::ProtoObject* proto = const_cast<proto::ProtoObject*>(ctx->newObject(false));
        const proto::ProtoString* nameKey = PythonEnvironment::getInternedString(ctx, "__name__");
        const proto::ProtoString* nameVal = PythonEnvironment::getInternedString(ctx, "_tuplegetter");
        proto = const_cast<proto::ProtoObject*>(proto->setAttribute(ctx, nameKey, nameVal->asObject(ctx)));
        proto = const_cast<proto::ProtoObject*>(proto->setAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "__get__"),
            ctx->fromMethod(proto, py_tuple_getter_get)));
        g_tuplegetter_proto = proto;
    }
    const proto::ProtoObject* descriptor = g_tuplegetter_proto->newChild(ctx, true);
    descriptor = descriptor->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__index__"), index);
    descriptor = descriptor->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__class__"), g_tuplegetter_proto);
    return descriptor;
}

static void deque_finalizer(void* ptr) {
    delete static_cast<DequeState*>(ptr);
}

static DequeState* get_deque_state(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    const proto::ProtoString* key = PythonEnvironment::getInternalString(ctx, "__deque_ptr__");
    const proto::ProtoObject* ptrObj = self->getAttribute(ctx, key);
    if (ptrObj) {
        const proto::ProtoExternalPointer* ext = ptrObj->asExternalPointer(ctx);
        if (ext) {
            return static_cast<DequeState*>(ext->getPointer(ctx));
        }
    }
    return nullptr;
}

// Native deque methods are also reached through the type instead of bound to
// an instance: iter() and reversed() on a subclass instance, deque.append(d,
// x), and instance creation calling __init__. Those calls pass the deque as
// the first positional argument (the list methods handle the same unbound
// form). Moves the receiver into `self` and returns its state; the method's
// own arguments start at `argOff`.
static DequeState* resolve_deque_receiver(proto::ProtoContext* ctx,
                                          const proto::ProtoObject*& self,
                                          const proto::ProtoList* posArgs,
                                          unsigned long& argOff) {
    argOff = 0;
    DequeState* state = self ? get_deque_state(ctx, self) : nullptr;
    if (!state && posArgs && posArgs->getSize(ctx) > 0) {
        const proto::ProtoObject* first = posArgs->getAt(ctx, 0);
        DequeState* firstState = first ? get_deque_state(ctx, first) : nullptr;
        if (firstState) {
            self = first;
            state = firstState;
            argOff = 1;
        }
    }
    return state;
}

static const proto::ProtoString* deque_items_name(proto::ProtoContext* ctx) {
    return PythonEnvironment::getInternalString(ctx, "__deque_items__");
}

static const proto::ProtoString* deque_snapshot_name(proto::ProtoContext* ctx) {
    return PythonEnvironment::getInternalString(ctx, "__deque_items_snapshot__");
}

// The deque's items, and in `raw` the attribute value they were read from:
// the snapshot a mutation publishes against.
static const proto::ProtoList* deque_items(proto::ProtoContext* ctx, const proto::ProtoObject* self,
                                           const proto::ProtoObject*& raw) {
    raw = self ? self->getOwnAttributeDirect(ctx, deque_items_name(ctx)) : nullptr;
    const proto::ProtoList* items = raw ? raw->asList(ctx) : nullptr;
    return items ? items : ctx->newList();
}

// Installs `next` only while the items are still `expected` (publishListData
// does the same for lists); false means another thread changed them first and
// the caller recomputes from a fresh snapshot.
static bool deque_publish(proto::ProtoContext* ctx, const proto::ProtoObject* self,
                          const proto::ProtoObject* expected, const proto::ProtoList* next) {
    const proto::ProtoString* name = deque_items_name(ctx);
    if (self->setAttributeIfEqual(ctx, name, expected, next->asObject(ctx))) return true;
    const proto::ProtoObject* own = self->getOwnAttributeDirect(ctx, name);
    if (own != expected && own != nullptr && own != PROTO_NONE) return false;
    const_cast<proto::ProtoObject*>(self)->setAttribute(ctx, name, next->asObject(ctx));
    return true;
}

// maxlen: adding at one end drops items from the other.
static const proto::ProtoList* deque_trim_front(proto::ProtoContext* ctx, const DequeState* state,
                                                const proto::ProtoList* items) {
    const long maxlen = state->maxlen.load();
    while (maxlen >= 0 && items->getSize(ctx) > static_cast<unsigned long>(maxlen)) {
        items = items->removeFirst(ctx);
    }
    return items;
}

static const proto::ProtoList* deque_trim_back(proto::ProtoContext* ctx, const DequeState* state,
                                               const proto::ProtoList* items) {
    const long maxlen = state->maxlen.load();
    while (maxlen >= 0 && items->getSize(ctx) > static_cast<unsigned long>(maxlen)) {
        items = items->removeLast(ctx);
    }
    return items;
}

// The items of `iterable`, collected before the deque is changed: iterating
// runs Python code, which a retried publish must not repeat. nullptr, with the
// exception pending, when iterating raises.
static const proto::ProtoList* deque_collect(proto::ProtoContext* ctx, PythonEnvironment* env,
                                             const proto::ProtoObject* iterable) {
    const proto::ProtoList* items = ctx->newList();
    const proto::ProtoObject* it = env->iter(iterable);
    if (!it) return env->hasPendingException() ? nullptr : items;
    PythonEnvironment::TransientPin pinIt(env, it);
    for (;;) {
        const proto::ProtoObject* item = env->next(it);
        if (env->hasPendingException()) {
            if (!env->isStopIteration(ctx, env->peekPendingException())) return nullptr;
            env->clearPendingException();
            break;
        }
        if (!item) break;
        items = items->appendLast(ctx, item);
    }
    return items;
}

static const proto::ProtoObject* py_deque_append(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwArgs) {

    unsigned long argOff = 0;
    DequeState* state = resolve_deque_receiver(ctx, self, posArgs, argOff);
    if (!state) {
        // CPython: descriptor doesn't apply to a non-deque object.
        // Match the standard "descriptor 'X' for 'Y' objects doesn't
        // apply to a 'Z' object" shape.
        protoPython::PythonEnvironment* env =
            protoPython::PythonEnvironment::fromContext(ctx);
        if (env) env->raiseTypeError(ctx,
            "descriptor 'append' for 'collections.deque' objects "
            "doesn't apply to a non-deque object");
        return nullptr;
    }
    if (posArgs->getSize(ctx) > argOff) {
        const proto::ProtoObject* value = posArgs->getAt(ctx, static_cast<int>(argOff));
        for (;;) {
            const proto::ProtoObject* raw = nullptr;
            const proto::ProtoList* items = deque_items(ctx, self, raw);
            if (deque_publish(ctx, self, raw, deque_trim_front(ctx, state, items->appendLast(ctx, value)))) break;
        }
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_deque_appendleft(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwArgs) {

    unsigned long argOff = 0;
    DequeState* state = resolve_deque_receiver(ctx, self, posArgs, argOff);
    if (!state) {
        protoPython::PythonEnvironment* env =
            protoPython::PythonEnvironment::fromContext(ctx);
        if (env) env->raiseTypeError(ctx,
            "descriptor 'appendleft' for 'collections.deque' objects "
            "doesn't apply to a non-deque object");
        return nullptr;
    }
    if (posArgs->getSize(ctx) > argOff) {
        const proto::ProtoObject* value = posArgs->getAt(ctx, static_cast<int>(argOff));
        for (;;) {
            const proto::ProtoObject* raw = nullptr;
            const proto::ProtoList* items = deque_items(ctx, self, raw);
            if (deque_publish(ctx, self, raw, deque_trim_back(ctx, state, items->appendFirst(ctx, value)))) break;
        }
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_deque_pop(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwArgs) {

    unsigned long argOff = 0;
    DequeState* state = resolve_deque_receiver(ctx, self, posArgs, argOff);
    if (!state) {
        protoPython::PythonEnvironment* env =
            protoPython::PythonEnvironment::fromContext(ctx);
        if (env) env->raiseTypeError(ctx,
            "descriptor 'pop' for 'collections.deque' objects "
            "doesn't apply to a non-deque object");
        return nullptr;
    }
    for (;;) {
        const proto::ProtoObject* raw = nullptr;
        const proto::ProtoList* items = deque_items(ctx, self, raw);
        if (items->getSize(ctx) == 0) {
            protoPython::PythonEnvironment* env =
                protoPython::PythonEnvironment::fromContext(ctx);
            if (env) env->raiseIndexError(ctx, "pop from an empty deque");
            return nullptr;
        }
        const proto::ProtoObject* val = items->getLast(ctx);
        if (deque_publish(ctx, self, raw, items->removeLast(ctx))) return val;
    }
}

static const proto::ProtoObject* py_deque_popleft(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwArgs) {

    unsigned long argOff = 0;
    DequeState* state = resolve_deque_receiver(ctx, self, posArgs, argOff);
    if (!state) {
        protoPython::PythonEnvironment* env =
            protoPython::PythonEnvironment::fromContext(ctx);
        if (env) env->raiseTypeError(ctx,
            "descriptor 'popleft' for 'collections.deque' objects "
            "doesn't apply to a non-deque object");
        return nullptr;
    }
    for (;;) {
        const proto::ProtoObject* raw = nullptr;
        const proto::ProtoList* items = deque_items(ctx, self, raw);
        if (items->getSize(ctx) == 0) {
            protoPython::PythonEnvironment* env =
                protoPython::PythonEnvironment::fromContext(ctx);
            if (env) env->raiseIndexError(ctx, "pop from an empty deque");
            return nullptr;
        }
        const proto::ProtoObject* val = items->getFirst(ctx);
        if (deque_publish(ctx, self, raw, items->removeFirst(ctx))) return val;
    }
}

static const proto::ProtoObject* py_deque_len(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwArgs) {

    unsigned long argOff = 0;
    DequeState* state = resolve_deque_receiver(ctx, self, posArgs, argOff);
    if (state) {
        const proto::ProtoObject* raw = nullptr;
        return ctx->fromInteger(static_cast<long long>(deque_items(ctx, self, raw)->getSize(ctx)));
    }
    return ctx->fromInteger(0);
}

static const proto::ProtoObject* py_deque_getitem(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwArgs*/) {
    unsigned long argOff = 0;
    DequeState* state = resolve_deque_receiver(ctx, self, posArgs, argOff);
    if (!posArgs || posArgs->getSize(ctx) <= argOff) return PROTO_NONE;
    const proto::ProtoObject* idxObj = posArgs->getAt(ctx, static_cast<int>(argOff));
    long long idx = 0;
    if (idxObj->isInteger(ctx)) idx = idxObj->asLong(ctx);
    else if (idxObj == PROTO_TRUE) idx = 1;
    else if (idxObj == PROTO_FALSE) idx = 0;
    else return PROTO_NONE;
    if (!state) return PROTO_NONE;
    const proto::ProtoObject* raw = nullptr;
    const proto::ProtoList* items = deque_items(ctx, self, raw);
    long long n = static_cast<long long>(items->getSize(ctx));
    if (idx < 0) idx += n;
    if (idx < 0 || idx >= n) {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        if (env) env->raiseIndexError(ctx, "deque index out of range");
        return nullptr;
    }
    return items->getAt(ctx, static_cast<int>(idx));
}

// deque.remove(value): remove the first occurrence of value (Python ==
// comparison). Raises ValueError when missing.  threading.Condition.notify
// calls this on its waiters deque to drop the lock that was just released.
// deque.count(x): the number of elements equal to x, compared as list.count
// does (identity first, then ==; an exception from __eq__ propagates).
static const proto::ProtoObject* py_deque_count(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwArgs*/) {
    unsigned long argOff = 0;
    DequeState* state = resolve_deque_receiver(ctx, self, posArgs, argOff);
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!state) {
        if (env) env->raiseTypeError(ctx,
            "descriptor 'count' for 'collections.deque' objects "
            "doesn't apply to a non-deque object");
        return nullptr;
    }
    if (!posArgs || posArgs->getSize(ctx) != argOff + 1) {
        if (env) env->raiseTypeError(ctx, "deque.count() takes exactly one argument ("
            + std::to_string(posArgs ? posArgs->getSize(ctx) - argOff : 0) + " given)");
        return nullptr;
    }
    const proto::ProtoObject* value = posArgs->getAt(ctx, static_cast<int>(argOff));
    // The receiver: the deque itself, also in the unbound form.
    const proto::ProtoObject* receiver = argOff > 0 ? posArgs->getAt(ctx, 0) : self;
    const proto::ProtoObject* raw = nullptr;
    const proto::ProtoList* items = deque_items(ctx, receiver, raw);
    // A snapshot: `items` is an immutable list, so __eq__ calls that mutate
    // the deque do not disturb the walk.
    PythonEnvironment::TransientPin pinItems(env, items ? items->asObject(ctx) : nullptr);
    long long count = 0;
    const unsigned long n = items ? items->getSize(ctx) : 0;
    for (unsigned long i = 0; i < n; ++i) {
        const proto::ProtoObject* item = items->getAt(ctx, static_cast<int>(i));
        if (item == value) { ++count; continue; }
        if (!env) continue;
        if (env->objectsEqual(ctx, item, value)) {
            ++count;
        } else if (env->hasPendingException()) {
            return nullptr;
        }
    }
    return ctx->fromInteger(count);
}

static const proto::ProtoObject* py_deque_remove(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwArgs*/) {
    unsigned long argOff = 0;
    DequeState* state = resolve_deque_receiver(ctx, self, posArgs, argOff);
    if (!state) {
        protoPython::PythonEnvironment* env =
            protoPython::PythonEnvironment::fromContext(ctx);
        if (env) env->raiseTypeError(ctx,
            "descriptor 'remove' for 'collections.deque' objects "
            "doesn't apply to a non-deque object");
        return nullptr;
    }
    if (!posArgs || posArgs->getSize(ctx) <= argOff) return PROTO_NONE;
    const proto::ProtoObject* value = posArgs->getAt(ctx, static_cast<int>(argOff));
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    for (;;) {
        const proto::ProtoObject* raw = nullptr;
        const proto::ProtoList* items = deque_items(ctx, self, raw);
        const unsigned long n = items->getSize(ctx);
        long found = -1;
        for (unsigned long i = 0; i < n && found < 0; ++i) {
            const proto::ProtoObject* item = items->getAt(ctx, static_cast<int>(i));
            bool eq = item == value;
            if (!eq && env) {
                const proto::ProtoObject* r = env->compareObjects(ctx, item, value, 0);
                if (!r && env->hasPendingException()) return nullptr;
                eq = (r == PROTO_TRUE);
            }
            if (eq) found = static_cast<long>(i);
        }
        if (found < 0) break;
        if (deque_publish(ctx, self, raw, items->removeAt(ctx, static_cast<int>(found)))) return PROTO_NONE;
    }
    if (env) env->raiseValueError(ctx,
        PythonEnvironment::getInternedString(ctx, "deque.remove(x): x not in deque")->asObject(ctx));
    return nullptr;
}

static const proto::ProtoObject* py_deque_clear(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwArgs*/) {
    unsigned long argOff = 0;
    DequeState* state = resolve_deque_receiver(ctx, self, posArgs, argOff);
    if (!state) {
        protoPython::PythonEnvironment* env =
            protoPython::PythonEnvironment::fromContext(ctx);
        if (env) env->raiseTypeError(ctx,
            "descriptor 'clear' for 'collections.deque' objects "
            "doesn't apply to a non-deque object");
        return nullptr;
    }
    for (;;) {
        const proto::ProtoObject* raw = nullptr;
        deque_items(ctx, self, raw);
        if (deque_publish(ctx, self, raw, ctx->newList())) return PROTO_NONE;
    }
}

static const proto::ProtoObject* py_deque_extend(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwArgs*/) {
    unsigned long argOff = 0;
    DequeState* state = resolve_deque_receiver(ctx, self, posArgs, argOff);
    if (!state) {
        protoPython::PythonEnvironment* env =
            protoPython::PythonEnvironment::fromContext(ctx);
        if (env) env->raiseTypeError(ctx,
            "descriptor 'extend' for 'collections.deque' objects "
            "doesn't apply to a non-deque object");
        return nullptr;
    }
    if (!posArgs || posArgs->getSize(ctx) <= argOff) return PROTO_NONE;
    const proto::ProtoObject* iterable = posArgs->getAt(ctx, static_cast<int>(argOff));
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return PROTO_NONE;
    const proto::ProtoList* incoming = deque_collect(ctx, env, iterable);
    if (!incoming) return nullptr;
    for (;;) {
        const proto::ProtoObject* raw = nullptr;
        const proto::ProtoList* items = deque_items(ctx, self, raw);
        if (deque_publish(ctx, self, raw, deque_trim_front(ctx, state, items->extend(ctx, incoming)))) {
            return PROTO_NONE;
        }
    }
}

static const proto::ProtoObject* py_deque_extendleft(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwArgs*/) {
    unsigned long argOff = 0;
    DequeState* state = resolve_deque_receiver(ctx, self, posArgs, argOff);
    if (!state) {
        protoPython::PythonEnvironment* env =
            protoPython::PythonEnvironment::fromContext(ctx);
        if (env) env->raiseTypeError(ctx,
            "descriptor 'extendleft' for 'collections.deque' objects "
            "doesn't apply to a non-deque object");
        return nullptr;
    }
    if (!posArgs || posArgs->getSize(ctx) <= argOff) return PROTO_NONE;
    const proto::ProtoObject* iterable = posArgs->getAt(ctx, static_cast<int>(argOff));
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return PROTO_NONE;
    const proto::ProtoList* incoming = deque_collect(ctx, env, iterable);
    if (!incoming) return nullptr;
    const unsigned long n = incoming->getSize(ctx);
    for (;;) {
        const proto::ProtoObject* raw = nullptr;
        const proto::ProtoList* items = deque_items(ctx, self, raw);
        for (unsigned long i = 0; i < n; ++i) {
            items = items->appendFirst(ctx, incoming->getAt(ctx, static_cast<int>(i)));
        }
        if (deque_publish(ctx, self, raw, deque_trim_back(ctx, state, items))) return PROTO_NONE;
    }
}

static const proto::ProtoObject* py_module_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink; (void)positionalParameters; (void)keywordParameters;
    const proto::ProtoObject* name = self->getAttribute(context, PythonEnvironment::getInternalString(context, "__name__"));
    std::string s = "<module '";
    if (name && name->isString(context)) {
        std::string n; name->asString(context)->toUTF8String(context, n);
        s += n;
    } else {
        s += "unknown";
    }
    s += "'>";
    return PythonEnvironment::getInternedString(context, s.c_str())->asObject(context);
}

static const proto::ProtoObject* py_deque_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink; (void)positionalParameters; (void)keywordParameters;
    unsigned long argOff = 0;
    DequeState* state = resolve_deque_receiver(context, self, positionalParameters, argOff);
    // CPython names the subclass, D([1, 2]), and shows a maxlen.
    std::string s = "deque";
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(context);
    const proto::ProtoObject* cls = (env && self) ? env->getType(context, self) : nullptr;
    const proto::ProtoObject* nm = cls ? cls->getAttribute(context, env->getNameString()) : nullptr;
    if (nm && nm->isString(context)) {
        std::string clsName;
        nm->asString(context)->toUTF8String(context, clsName);
        if (!clsName.empty()) s = clsName;
    }
    s += "([";
    long maxlen = -1;
    if (state) {
        const proto::ProtoObject* raw = nullptr;
        const proto::ProtoList* items = deque_items(context, self, raw);
        for (unsigned long i = 0; i < items->getSize(context); ++i) {
            if (i > 0) s += ", ";
            s += protoPython::PythonEnvironment::reprObject(context, items->getAt(context, static_cast<int>(i)));
        }
        maxlen = state->maxlen.load();
    }
    s += "]";
    if (maxlen >= 0) s += ", maxlen=" + std::to_string(maxlen);
    s += ")";
    return PythonEnvironment::getInternedString(context, s.c_str())->asObject(context);
}

static const proto::ProtoObject* py_deque_iter(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwArgs) {
    (void)parentLink; (void)posArgs; (void)kwArgs;
    
    unsigned long argOff = 0;
    DequeState* state = resolve_deque_receiver(ctx, self, posArgs, argOff);
    if (!state) return PROTO_NONE;
    const proto::ProtoString* itProtoS = PythonEnvironment::getInternalString(ctx, "__deque_iterator_proto__");
    const proto::ProtoObject* itProto = self->getAttribute(ctx, itProtoS);
    // A deque subclass instance does not reach it through its own chain:
    // look it up on the type.
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if ((!itProto || itProto == PROTO_NONE) && env) {
        itProto = env->getAttribute(ctx, env->getType(ctx, self), itProtoS, false);
    }
    if (!itProto || itProto == PROTO_NONE) return PROTO_NONE;

    const proto::ProtoObject* instance = itProto->newChild(ctx, true);
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__deque_obj__"), self);
    const proto::ProtoObject* raw = nullptr;
    deque_items(ctx, self, raw);
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__deque_idx__"), ctx->fromInteger(0));
    // The iterator reads from, and checks for mutation against, this snapshot.
    instance = instance->setAttribute(ctx, deque_snapshot_name(ctx), raw ? raw : PROTO_NONE);
    return instance;
}

static const proto::ProtoObject* py_deque_reversed(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwArgs) {
    (void)parentLink; (void)posArgs; (void)kwArgs;
    
    unsigned long argOff = 0;
    DequeState* state = resolve_deque_receiver(ctx, self, posArgs, argOff);
    if (!state) return PROTO_NONE;
    const proto::ProtoString* itProtoS = PythonEnvironment::getInternalString(ctx, "__deque_reverse_iterator_proto__");
    const proto::ProtoObject* itProto = self->getAttribute(ctx, itProtoS);
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if ((!itProto || itProto == PROTO_NONE) && env) {
        itProto = env->getAttribute(ctx, env->getType(ctx, self), itProtoS, false);
    }
    if (!itProto || itProto == PROTO_NONE) return PROTO_NONE;

    const proto::ProtoObject* instance = itProto->newChild(ctx, true);
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__deque_obj__"), self);
    const proto::ProtoObject* raw = nullptr;
    const proto::ProtoList* items = deque_items(ctx, self, raw);
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__deque_idx__"),
        ctx->fromInteger(static_cast<long long>(items->getSize(ctx)) - 1));
    instance = instance->setAttribute(ctx, deque_snapshot_name(ctx), raw ? raw : PROTO_NONE);
    return instance;
}

static const proto::ProtoObject* py_deque_iterator_next(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwArgs) {
    (void)parentLink; (void)posArgs; (void)kwArgs;
    
    const proto::ProtoObject* dequeObj = self->getAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__deque_obj__"));
    const proto::ProtoObject* idxObj = self->getAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__deque_idx__"));
    const proto::ProtoObject* mutationObj = self->getAttribute(ctx, deque_snapshot_name(ctx));
    if (!dequeObj || !idxObj || !mutationObj) return nullptr;
    
    DequeState* state = get_deque_state(ctx, dequeObj);
    if (!state) return nullptr;
    
    const proto::ProtoObject* itemsRaw = nullptr;
    const proto::ProtoList* items = deque_items(ctx, dequeObj, itemsRaw);
    if ((itemsRaw ? itemsRaw : PROTO_NONE) != mutationObj) {
        protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(ctx);
        if (env) env->raiseRuntimeError(ctx, "deque mutated during iteration");
        return nullptr;
    }

    long long idx = idxObj->asLong(ctx);
    if (idx < 0 || static_cast<unsigned long>(idx) >= items->getSize(ctx)) {
        return nullptr;
    }
    
    const proto::ProtoObject* val = items->getAt(ctx, static_cast<int>(idx));
    self->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__deque_idx__"), ctx->fromInteger(idx + 1));
    return val;
}

static const proto::ProtoObject* py_deque_reverse_iterator_next(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwArgs) {
    (void)parentLink; (void)posArgs; (void)kwArgs;
    
    const proto::ProtoObject* dequeObj = self->getAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__deque_obj__"));
    const proto::ProtoObject* idxObj = self->getAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__deque_idx__"));
    const proto::ProtoObject* mutationObj = self->getAttribute(ctx, deque_snapshot_name(ctx));
    if (!dequeObj || !idxObj || !mutationObj) return nullptr;
    
    DequeState* state = get_deque_state(ctx, dequeObj);
    if (!state) return nullptr;
    
    const proto::ProtoObject* itemsRaw = nullptr;
    const proto::ProtoList* items = deque_items(ctx, dequeObj, itemsRaw);
    if ((itemsRaw ? itemsRaw : PROTO_NONE) != mutationObj) {
        protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(ctx);
        if (env) env->raiseRuntimeError(ctx, "deque mutated during iteration");
        return nullptr;
    }

    long long idx = idxObj->asLong(ctx);
    if (idx < 0 || static_cast<unsigned long>(idx) >= items->getSize(ctx)) {
        return nullptr;
    }
    
    const proto::ProtoObject* val = items->getAt(ctx, static_cast<int>(idx));
    self->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__deque_idx__"), ctx->fromInteger(idx - 1));
    return val;
}

static const proto::ProtoObject* py_deque_new(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList* kwArgs) {
    const proto::ProtoObject* cls = self;
    if (!cls && posArgs && posArgs->getSize(ctx) > 0) {
        cls = posArgs->getAt(ctx, 0);
    }

    if (!cls) {
        protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(ctx);
        const proto::ProtoObject* mod = env->resolve("_collections", ctx);
        if (mod) {
            cls = mod->getAttribute(ctx, PythonEnvironment::getInternalString(ctx, "deque"));
        }
    }
    if (!cls) return PROTO_NONE;

    (void)parentLink; (void)kwArgs;
    const proto::ProtoObject* instance = cls->newChild(ctx, true);
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__class__"), cls);
    DequeState* state = new DequeState();
    const proto::ProtoString* key = PythonEnvironment::getInternalString(ctx, "__deque_ptr__");
    instance = instance->setAttribute(ctx, key,
                                    ctx->fromExternalPointer(state, deque_finalizer));
    instance = instance->setAttribute(ctx, deque_items_name(ctx), ctx->newList()->asObject(ctx));
    
    // CPython: deque.__new__ ignores its arguments. deque.__init__, which
    // instance creation runs next with the same arguments, consumes the
    // iterable and maxlen; consuming them here too would exhaust a generator.

    return instance;
}

// deque.__init__(iterable=(), maxlen=None): sets maxlen, clears the deque and
// extends it from iterable, like CPython. Instance creation and
// `deque.__init__(d, ...)` pass the deque as the first argument (see
// resolve_deque_receiver).
static const proto::ProtoObject* py_deque_init(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwArgs) {
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(ctx);
    if (!env) return PROTO_NONE;
    unsigned long argOff = 0;
    DequeState* state = resolve_deque_receiver(ctx, self, posArgs, argOff);
    if (!state) {
        env->raiseTypeError(ctx,
            "descriptor '__init__' for 'collections.deque' objects "
            "doesn't apply to a non-deque object");
        return nullptr;
    }
    unsigned long nargs = posArgs ? posArgs->getSize(ctx) : 0;
    if (nargs > argOff + 2) {
        env->raiseTypeError(ctx, "deque expected at most 2 arguments, got "
            + std::to_string(nargs - argOff));
        return nullptr;
    }
    const proto::ProtoObject* iterable = nargs > argOff
        ? posArgs->getAt(ctx, static_cast<int>(argOff)) : nullptr;
    const proto::ProtoObject* maxlenObj = nargs > argOff + 1
        ? posArgs->getAt(ctx, static_cast<int>(argOff + 1)) : PROTO_NONE;
    if (kwArgs) {
        unsigned long iterableH = PythonEnvironment::getInternedString(ctx, "iterable")->getHash(ctx);
        unsigned long maxlenH = PythonEnvironment::getInternedString(ctx, "maxlen")->getHash(ctx);
        if (kwArgs->has(ctx, iterableH)) iterable = kwArgs->getAt(ctx, iterableH);
        if (kwArgs->has(ctx, maxlenH)) maxlenObj = kwArgs->getAt(ctx, maxlenH);
    }
    if (!maxlenObj || maxlenObj == env->getNonePrototype() || maxlenObj->isNone(ctx)) {
        maxlenObj = PROTO_NONE;
    }
    long maxlen = -1;
    if (maxlenObj != PROTO_NONE) {
        if (!maxlenObj->isInteger(ctx)) {
            env->raiseTypeError(ctx, "an integer is required");
            return nullptr;
        }
        if (maxlenObj->asLong(ctx) < 0) {
            env->raiseValueError(ctx,
                PythonEnvironment::getInternedString(ctx, "maxlen must be non-negative")->asObject(ctx));
            return nullptr;
        }
        maxlen = static_cast<long>(maxlenObj->asLong(ctx));
    }
    state->maxlen.store(maxlen);
    // CPython clears the deque before extending it, so d.__init__(d) empties d.
    const proto::ProtoList* incoming = ctx->newList();
    if (iterable && iterable != self) {
        incoming = deque_collect(ctx, env, iterable);
        if (!incoming) return nullptr;
    }
    for (;;) {
        const proto::ProtoObject* raw = nullptr;
        deque_items(ctx, self, raw);
        if (deque_publish(ctx, self, raw, deque_trim_front(ctx, state, incoming))) return PROTO_NONE;
    }
}

// A deque's items as a Python list: comparisons delegate to list comparison
// (lexicographic, with the items' own __eq__ / __lt__), over one snapshot of
// the items.
static const proto::ProtoObject* deque_as_list(proto::ProtoContext* ctx, PythonEnvironment* env,
                                               const proto::ProtoObject* deque) {
    const proto::ProtoObject* raw = nullptr;
    const proto::ProtoList* items = deque_items(ctx, deque, raw);
    proto::ProtoObject* listObj = const_cast<proto::ProtoObject*>(env->getListPrototype()->newChild(ctx, true));
    listObj = const_cast<proto::ProtoObject*>(listObj->setAttribute(ctx, env->getDataString(), items->asObject(ctx)));
    return listObj;
}

// deque == / != / < / <= / > / >= another deque (op as in compareObjects:
// 0 eq, 1 ne, 2 lt, 3 le, 4 gt, 5 ge); NotImplemented for any other operand.
static const proto::ProtoObject* deque_compare(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ProtoList* posArgs, int op) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return PROTO_NONE;
    unsigned long argOff = 0;
    DequeState* state = resolve_deque_receiver(ctx, self, posArgs, argOff);
    if (!state || !posArgs || posArgs->getSize(ctx) <= argOff) return env->getNotImplementedPrototype();
    const proto::ProtoObject* other = posArgs->getAt(ctx, static_cast<int>(argOff));
    DequeState* otherState = other ? get_deque_state(ctx, other) : nullptr;
    if (!otherState) return env->getNotImplementedPrototype();
    const proto::ProtoObject* a = deque_as_list(ctx, env, self);
    PythonEnvironment::TransientPin pinA(env, a);
    const proto::ProtoObject* b = deque_as_list(ctx, env, other);
    PythonEnvironment::TransientPin pinB(env, b);
    return env->compareObjects(ctx, a, b, op);
}

static const proto::ProtoObject* py_deque_eq(proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    return deque_compare(ctx, self, posArgs, 0);
}
static const proto::ProtoObject* py_deque_ne(proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    return deque_compare(ctx, self, posArgs, 1);
}
static const proto::ProtoObject* py_deque_lt(proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    return deque_compare(ctx, self, posArgs, 2);
}
static const proto::ProtoObject* py_deque_le(proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    return deque_compare(ctx, self, posArgs, 3);
}
static const proto::ProtoObject* py_deque_gt(proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    return deque_compare(ctx, self, posArgs, 4);
}
static const proto::ProtoObject* py_deque_ge(proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    return deque_compare(ctx, self, posArgs, 5);
}

// fget of the read-only `maxlen` property: None when unbounded.
static const proto::ProtoObject* py_deque_maxlen_get(proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    unsigned long argOff = 0;
    DequeState* state = resolve_deque_receiver(ctx, self, posArgs, argOff);
    if (!state) return PROTO_NONE;
    const long maxlen = state->maxlen.load();
    return maxlen < 0 ? PROTO_NONE : ctx->fromInteger(maxlen);
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx, protoPython::PythonEnvironment* env) {
    const proto::ProtoObject* module = env && env->getObjectPrototype() ? env->getObjectPrototype()->newChild(ctx, true) : ctx->newObject(false);
    const proto::ProtoObject* dequePrototype = env && env->getObjectPrototype() ? env->getObjectPrototype()->newChild(ctx, true) : ctx->newObject(false);
    
    if (env && env->getTypePrototype()) {
        dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__class__"), env->getTypePrototype());
    }
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__name__"), PythonEnvironment::getInternedString(ctx, "deque")->asObject(ctx));
    // R5-81: __mro__ = (deque, object) so reflection over the deque
    // type sees the canonical chain instead of `(object,)`.
    if (env && env->getObjectPrototype()) {
        const proto::ProtoString* mroS = PythonEnvironment::getInternedString(ctx, "__mro__");
        const proto::ProtoList* mroList = ctx->newList()
            ->appendLast(ctx, dequePrototype)
            ->appendLast(ctx, env->getObjectPrototype());
        dequePrototype = dequePrototype->setAttribute(ctx, mroS,
            ctx->newTupleFromList(mroList)->asObject(ctx));
    }
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__new__"),
                                                 ctx->fromMethod(nullptr, py_deque_new));
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__init__"),
                                                 ctx->fromMethod(nullptr, py_deque_init));
    
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "append"), 
                                                 ctx->fromMethod(nullptr, py_deque_append));
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "appendleft"), 
                                                 ctx->fromMethod(nullptr, py_deque_appendleft));
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "pop"), 
                                                 ctx->fromMethod(nullptr, py_deque_pop));
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "popleft"), 
                                                 ctx->fromMethod(nullptr, py_deque_popleft));
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__len__"),
                                                 ctx->fromMethod(nullptr, py_deque_len));
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__reversed__"),
                                                 ctx->fromMethod(nullptr, py_deque_reversed));
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "remove"),
                                                 ctx->fromMethod(nullptr, py_deque_remove));
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "count"),
                                                 ctx->fromMethod(nullptr, py_deque_count));
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__getitem__"),
                                                 ctx->fromMethod(nullptr, py_deque_getitem));
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "clear"),
                                                 ctx->fromMethod(nullptr, py_deque_clear));
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "extend"),
                                                 ctx->fromMethod(nullptr, py_deque_extend));
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "extendleft"),
                                                 ctx->fromMethod(nullptr, py_deque_extendleft));
    // Comparisons between deques; deques are mutable, so unhashable.
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__eq__"),
                                                 ctx->fromMethod(nullptr, py_deque_eq));
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__ne__"),
                                                 ctx->fromMethod(nullptr, py_deque_ne));
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__lt__"),
                                                 ctx->fromMethod(nullptr, py_deque_lt));
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__le__"),
                                                 ctx->fromMethod(nullptr, py_deque_le));
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__gt__"),
                                                 ctx->fromMethod(nullptr, py_deque_gt));
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__ge__"),
                                                 ctx->fromMethod(nullptr, py_deque_ge));
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__hash__"), PROTO_NONE);
    // maxlen is a read-only property over the native state.
    if (env && env->getBuiltins()) {
        const proto::ProtoObject* propertyType = env->getBuiltins()->getAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "property"));
        if (propertyType && propertyType != PROTO_NONE) {
            const proto::ProtoObject* maxlenProp = env->callObject(propertyType,
                {ctx->fromMethod(nullptr, py_deque_maxlen_get)});
            if (maxlenProp && maxlenProp != PROTO_NONE) {
                dequePrototype = dequePrototype->setAttribute(ctx,
                    PythonEnvironment::getInternedString(ctx, "maxlen"), maxlenProp);
            }
        }
    }
    
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__new__"),
                                                 ctx->fromMethod(nullptr, py_deque_new));
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__init__"),
                                                 ctx->fromMethod(nullptr, py_deque_init));
    
    // Store prototype in module
    module = module->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__deque_prototype__"), dequePrototype);

    module = module->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "deque"), dequePrototype);

    // native _tuplegetter for namedtuple
    module = module->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "_tuplegetter"), ctx->fromMethod(nullptr, py_tuplegetter));

    // defaultdict is implemented in Python (collections/__init__.py) on top
    // of dict.__missing__. The native constructor registered here was a bare
    // method whose prototype lived on an unreferenced object, so
    // defaultdict(list) returned None.
    // OrderedDict is implemented in Python as well, on the insertion order
    // dict already keeps. The native constructor registered here returned a
    // plain object that was not a dict and had no move_to_end.



    const proto::ProtoObject* deque_iterator = ctx->newObject(false);
    deque_iterator = deque_iterator->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__name__"), PythonEnvironment::getInternedString(ctx, "_deque_iterator")->asObject(ctx));
    deque_iterator = deque_iterator->setAttribute(ctx, env->getNextString(),
                                                 ctx->fromMethod(nullptr, py_deque_iterator_next));
    deque_iterator = deque_iterator->setAttribute(ctx, env->getIterString(),
                                                 ctx->fromMethod(nullptr, py_collections_dummy)); // self iter
    // R5-82: __mro__ for _deque_iterator.
    if (env && env->getObjectPrototype()) {
        const proto::ProtoString* mroS = PythonEnvironment::getInternedString(ctx, "__mro__");
        const proto::ProtoList* mroList = ctx->newList()
            ->appendLast(ctx, deque_iterator)
            ->appendLast(ctx, env->getObjectPrototype());
        deque_iterator = deque_iterator->setAttribute(ctx, mroS,
            ctx->newTupleFromList(mroList)->asObject(ctx));
    }
    
    dequePrototype = dequePrototype->setAttribute(ctx, env->getIterString(),
                                                 ctx->fromMethod(nullptr, py_deque_iter));
    dequePrototype = dequePrototype->setAttribute(ctx, env->getReprString(),
                                                 ctx->fromMethod(nullptr, py_deque_repr));
    dequePrototype = dequePrototype->setAttribute(ctx, env->getStrString(),
                                                 ctx->fromMethod(nullptr, py_deque_repr));
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__deque_iterator_proto__"), deque_iterator);

    const proto::ProtoObject* deque_reverse_iterator = ctx->newObject(false);
    deque_reverse_iterator = deque_reverse_iterator->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__name__"), PythonEnvironment::getInternedString(ctx, "_deque_reverse_iterator")->asObject(ctx));
    deque_reverse_iterator = deque_reverse_iterator->setAttribute(ctx, env->getNextString(),
                                                  ctx->fromMethod(nullptr, py_deque_reverse_iterator_next));
    deque_reverse_iterator = deque_reverse_iterator->setAttribute(ctx, env->getIterString(),
                                                  ctx->fromMethod(nullptr, py_collections_dummy)); // self iter
    // R5-82: __mro__ for _deque_reverse_iterator.
    if (env && env->getObjectPrototype()) {
        const proto::ProtoString* mroS = PythonEnvironment::getInternedString(ctx, "__mro__");
        const proto::ProtoList* mroList = ctx->newList()
            ->appendLast(ctx, deque_reverse_iterator)
            ->appendLast(ctx, env->getObjectPrototype());
        deque_reverse_iterator = deque_reverse_iterator->setAttribute(ctx, mroS,
            ctx->newTupleFromList(mroList)->asObject(ctx));
    }
    
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__deque_reverse_iterator_proto__"), deque_reverse_iterator);

    module = module->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "_deque_iterator"), deque_iterator);

    // Dummy _count_elements for Counter
    module = module->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "_count_elements"),
                                 ctx->fromMethod(nullptr, py_collections_dummy));

    // Set __class__ on the module for better diagnostics
    module = module->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__name__"), PythonEnvironment::getInternedString(ctx, "_collections")->asObject(ctx));
    module = module->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__repr__"), ctx->fromMethod(nullptr, py_module_repr));

    return module;
}

} // namespace collections
} // namespace protoPython
