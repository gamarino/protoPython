#include <protoPython/CollectionsModule.h>
#include <protoPython/PythonEnvironment.h>
#include <deque>
#include <mutex>

namespace protoPython {
namespace collections {

// Thread safety: per-instance mutex in DequeState protects mutable shared state (user-level locking, not GIL).
// L-Shape: acceptable; see docs/L_SHAPE_ARCHITECTURE.md. See also docs/GIL_FREE_AUDIT.md.
struct DequeState {
    std::deque<const proto::ProtoObject*> data;
    std::mutex mutex;
    size_t mutationCount = 0;
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

static const proto::ProtoObject* py_deque_append(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwArgs) {

    DequeState* state = get_deque_state(ctx, self);
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
    if (posArgs->getSize(ctx) > 0) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->data.push_back(posArgs->getAt(ctx, 0));
        state->mutationCount++;
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_deque_appendleft(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwArgs) {

    DequeState* state = get_deque_state(ctx, self);
    if (!state) {
        protoPython::PythonEnvironment* env =
            protoPython::PythonEnvironment::fromContext(ctx);
        if (env) env->raiseTypeError(ctx,
            "descriptor 'appendleft' for 'collections.deque' objects "
            "doesn't apply to a non-deque object");
        return nullptr;
    }
    if (posArgs->getSize(ctx) > 0) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->data.push_front(posArgs->getAt(ctx, 0));
        state->mutationCount++;
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_deque_pop(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwArgs) {
    
    DequeState* state = get_deque_state(ctx, self);
    if (state) {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->data.empty()) {
            const proto::ProtoObject* val = state->data.back();
            state->data.pop_back();
            state->mutationCount++;
            return val;
        }
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_deque_popleft(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwArgs) {
    
    DequeState* state = get_deque_state(ctx, self);
    if (state) {
        std::lock_guard<std::mutex> lock(state->mutex);
        if (!state->data.empty()) {
            const proto::ProtoObject* val = state->data.front();
            state->data.pop_front();
            state->mutationCount++;
            return val;
        }
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_deque_len(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwArgs) {

    DequeState* state = get_deque_state(ctx, self);
    if (state) {
        std::lock_guard<std::mutex> lock(state->mutex);
        return ctx->fromInteger(state->data.size());
    }
    return ctx->fromInteger(0);
}

static const proto::ProtoObject* py_deque_getitem(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwArgs*/) {
    if (!posArgs || posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* idxObj = posArgs->getAt(ctx, 0);
    long long idx = 0;
    if (idxObj->isInteger(ctx)) idx = idxObj->asLong(ctx);
    else if (idxObj == PROTO_TRUE) idx = 1;
    else if (idxObj == PROTO_FALSE) idx = 0;
    else return PROTO_NONE;
    DequeState* state = get_deque_state(ctx, self);
    if (!state) return PROTO_NONE;
    std::lock_guard<std::mutex> lock(state->mutex);
    long long n = static_cast<long long>(state->data.size());
    if (idx < 0) idx += n;
    if (idx < 0 || idx >= n) {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        if (env) env->raiseIndexError(ctx, "deque index out of range");
        return nullptr;
    }
    return state->data[static_cast<size_t>(idx)];
}

// deque.remove(value): remove the first occurrence of value (Python ==
// comparison). Raises ValueError when missing.  threading.Condition.notify
// calls this on its waiters deque to drop the lock that was just released.
static const proto::ProtoObject* py_deque_remove(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwArgs*/) {
    if (!posArgs || posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* value = posArgs->getAt(ctx, 0);
    DequeState* state = get_deque_state(ctx, self);
    if (!state) return PROTO_NONE;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    std::lock_guard<std::mutex> lock(state->mutex);
    for (auto it = state->data.begin(); it != state->data.end(); ++it) {
        bool eq = false;
        if (*it == value) {
            eq = true;
        } else if (env) {
            const proto::ProtoObject* r = env->compareObjects(ctx, *it, value, 0);
            eq = (r == PROTO_TRUE);
        }
        if (eq) {
            state->data.erase(it);
            state->mutationCount++;
            return PROTO_NONE;
        }
    }
    if (env) env->raiseValueError(ctx,
        PythonEnvironment::getInternedString(ctx, "deque.remove(x): x not in deque")->asObject(ctx));
    return nullptr;
}

static const proto::ProtoObject* py_deque_clear(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*posArgs*/,
    const proto::ProtoSparseList* /*kwArgs*/) {
    DequeState* state = get_deque_state(ctx, self);
    if (state) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->data.clear();
        state->mutationCount++;
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_deque_extend(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwArgs*/) {
    if (!posArgs || posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* iterable = posArgs->getAt(ctx, 0);
    DequeState* state = get_deque_state(ctx, self);
    if (!state) return PROTO_NONE;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return PROTO_NONE;
    const proto::ProtoObject* it = env->iter(iterable);
    if (!it) return PROTO_NONE;
    PythonEnvironment::TransientPin pinIt(env, it);
    while (true) {
        const proto::ProtoObject* item = env->next(it);
        if (!item) break;
        std::lock_guard<std::mutex> lock(state->mutex);
        state->data.push_back(item);
        state->mutationCount++;
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_deque_extendleft(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwArgs*/) {
    if (!posArgs || posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* iterable = posArgs->getAt(ctx, 0);
    DequeState* state = get_deque_state(ctx, self);
    if (!state) return PROTO_NONE;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return PROTO_NONE;
    const proto::ProtoObject* it = env->iter(iterable);
    if (!it) return PROTO_NONE;
    PythonEnvironment::TransientPin pinIt(env, it);
    while (true) {
        const proto::ProtoObject* item = env->next(it);
        if (!item) break;
        std::lock_guard<std::mutex> lock(state->mutex);
        state->data.push_front(item);
        state->mutationCount++;
    }
    return PROTO_NONE;
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
    DequeState* state = get_deque_state(context, self);
    std::string s = "deque([";
    if (state) {
        std::lock_guard<std::mutex> lock(state->mutex);
        for (size_t i = 0; i < state->data.size(); ++i) {
            if (i > 0) s += ", ";
            s += protoPython::PythonEnvironment::reprObject(context, state->data[i]);
        }
    }
    s += "])";
    return PythonEnvironment::getInternedString(context, s.c_str())->asObject(context);
}

static const proto::ProtoObject* py_deque_iter(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwArgs) {
    (void)parentLink; (void)posArgs; (void)kwArgs;
    
    const proto::ProtoObject* itProto = self->getAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__deque_iterator_proto__"));
    if (!itProto) return PROTO_NONE;
    
    DequeState* state = get_deque_state(ctx, self);
    if (!state) return PROTO_NONE;

    const proto::ProtoObject* instance = itProto->newChild(ctx, true);
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__deque_obj__"), self);
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__deque_idx__"), ctx->fromInteger(0));
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__deque_mutation__"), ctx->fromInteger(state->mutationCount));
    return instance;
}

static const proto::ProtoObject* py_deque_reversed(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwArgs) {
    (void)parentLink; (void)posArgs; (void)kwArgs;
    
    const proto::ProtoObject* itProto = self->getAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__deque_reverse_iterator_proto__"));
    if (!itProto) return PROTO_NONE;
    
    DequeState* state = get_deque_state(ctx, self);
    if (!state) return PROTO_NONE;

    const proto::ProtoObject* instance = itProto->newChild(ctx, true);
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__deque_obj__"), self);
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__deque_idx__"), ctx->fromInteger(state->data.size() - 1));
    instance = instance->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__deque_mutation__"), ctx->fromInteger(state->mutationCount));
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
    const proto::ProtoObject* mutationObj = self->getAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__deque_mutation__"));
    if (!dequeObj || !idxObj || !mutationObj) return nullptr;
    
    DequeState* state = get_deque_state(ctx, dequeObj);
    if (!state) return nullptr;
    
    if (mutationObj->asLong(ctx) != (long long)state->mutationCount) {
        protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(ctx);
        if (env) env->raiseRuntimeError(ctx, "deque mutated during iteration");
        return nullptr;
    }

    long long idx = idxObj->asLong(ctx);
    std::lock_guard<std::mutex> lock(state->mutex);
    if (idx < 0 || static_cast<size_t>(idx) >= state->data.size()) {
        return nullptr;
    }
    
    const proto::ProtoObject* val = state->data[static_cast<size_t>(idx)];
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
    const proto::ProtoObject* mutationObj = self->getAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__deque_mutation__"));
    if (!dequeObj || !idxObj || !mutationObj) return nullptr;
    
    DequeState* state = get_deque_state(ctx, dequeObj);
    if (!state) return nullptr;
    
    if (mutationObj->asLong(ctx) != (long long)state->mutationCount) {
        protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(ctx);
        if (env) env->raiseRuntimeError(ctx, "deque mutated during iteration");
        return nullptr;
    }

    long long idx = idxObj->asLong(ctx);
    std::lock_guard<std::mutex> lock(state->mutex);
    if (idx < 0 || static_cast<size_t>(idx) >= state->data.size()) {
        return nullptr;
    }
    
    const proto::ProtoObject* val = state->data[static_cast<size_t>(idx)];
    self->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__deque_idx__"), ctx->fromInteger(idx - 1));
    return val;
}

static const proto::ProtoObject* py_defaultdict_getitem(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* dataName = PythonEnvironment::getInternalString(ctx, "__data__");
    const proto::ProtoObject* data = self->getAttribute(ctx, dataName);
    if (!data || !data->asSparseList(ctx)) return PROTO_NONE;
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;

    const proto::ProtoObject* key = posArgs->getAt(ctx, 0);
    unsigned long hash = key->getHash(ctx);
    const proto::ProtoObject* value = data->asSparseList(ctx)->getAt(ctx, hash);
    if (value) return value;

    const proto::ProtoObject* factory = self->getAttribute(ctx, PythonEnvironment::getInternalString(ctx, "default_factory"));
    if (!factory || factory == PROTO_NONE) {
        protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(ctx);
        if (env) env->raiseKeyError(ctx, key);
        return PROTO_NONE;
    }
    const proto::ProtoObject* callAttr = factory->getAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__call__"));
    if (!callAttr || !callAttr->asMethod(ctx)) {
        protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(ctx);
        if (env) env->raiseKeyError(ctx, key);
        return PROTO_NONE;
    }
    const proto::ProtoList* empty = ctx->newList();
    value = callAttr->asMethod(ctx)(ctx, factory, nullptr, empty, nullptr);
    if (!value) return PROTO_NONE;

    const proto::ProtoSparseList* newSparse = data->asSparseList(ctx)->setAt(ctx, hash, value);
    self->setAttribute(ctx, dataName, newSparse->asObject(ctx));
    const proto::ProtoString* keysName = PythonEnvironment::getInternalString(ctx, "__keys__");
    const proto::ProtoObject* keysObj = self->getAttribute(ctx, keysName);
    const proto::ProtoList* keysList = keysObj && keysObj->asList(ctx) ? keysObj->asList(ctx) : ctx->newList();
    keysList = keysList->appendLast(ctx, key);
    self->setAttribute(ctx, keysName, keysList->asObject(ctx));
    return value;
}

static const proto::ProtoObject* py_defaultdict_new(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoObject* proto = self->getAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__defaultdict_prototype__"));
    if (!proto) return PROTO_NONE;

    const proto::ProtoObject* d = proto->newChild(ctx, true);
    d = d->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__data__"), ctx->newSparseList()->asObject(ctx));
    d = d->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__keys__"), ctx->newList()->asObject(ctx));
    const proto::ProtoObject* factory = posArgs->getSize(ctx) > 0 ? posArgs->getAt(ctx, 0) : PROTO_NONE;
    d = d->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "default_factory"), factory ? factory : PROTO_NONE);
    return d;
}

static const proto::ProtoObject* py_ordereddict_new(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoObject* d = ctx->newObject(false);
    d = d->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__data__"), ctx->newSparseList()->asObject(ctx));
        d = d->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__keys__"), ctx->newList()->asObject(ctx));
    return d;
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
    
    if (posArgs->getSize(ctx) > 1) {
        const proto::ProtoObject* iterable = posArgs->getAt(ctx, 1);
        protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(ctx);
        if (env && iterable && iterable != PROTO_NONE) {
            const proto::ProtoObject* it = env->iter(iterable);
            if (it && !env->hasPendingException()) {
                // Pin the derived iterator across user __next__ callbacks.
                // Without this, deep recursion through user generators
                // can free `it` mid-loop. See audit/03-gc-roots.md F3.1.
                protoPython::PythonEnvironment::TransientPin pinIt(env, it);
                while (true) {
                    const proto::ProtoObject* item = env->next(it);
                    if (env->hasPendingException()) {
                        if (env->isStopIteration(ctx, env->peekPendingException())) {
                            env->clearPendingException();
                        }
                        break;
                    }
                    if (!item) break;
                    state->data.push_back(item);
                }
            }
        }
    }
    
    return instance;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx, protoPython::PythonEnvironment* env) {
    const proto::ProtoObject* module = env && env->getObjectPrototype() ? env->getObjectPrototype()->newChild(ctx, true) : ctx->newObject(false);
    const proto::ProtoObject* dequePrototype = env && env->getObjectPrototype() ? env->getObjectPrototype()->newChild(ctx, true) : ctx->newObject(false);
    
    if (env && env->getTypePrototype()) {
        dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__class__"), env->getTypePrototype());
    }
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__name__"), PythonEnvironment::getInternedString(ctx, "deque")->asObject(ctx));
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__new__"),
                                                 ctx->fromMethod(nullptr, py_deque_new));
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__init__"),
                                                 env->getNotImplementedPrototype()); // We handle init in new for now or just skip it
    
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
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__getitem__"),
                                                 ctx->fromMethod(nullptr, py_deque_getitem));
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "clear"),
                                                 ctx->fromMethod(nullptr, py_deque_clear));
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "extend"),
                                                 ctx->fromMethod(nullptr, py_deque_extend));
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "extendleft"),
                                                 ctx->fromMethod(nullptr, py_deque_extendleft));
    
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__new__"),
                                                 ctx->fromMethod(nullptr, py_deque_new));
    dequePrototype = dequePrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__init__"),
                                                 env->getNotImplementedPrototype()); // We handle init in new for now or just skip it
    
    // Store prototype in module
    module = module->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__deque_prototype__"), dequePrototype);

    module = module->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "deque"), dequePrototype);

    // native _tuplegetter for namedtuple
    module = module->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "_tuplegetter"), ctx->fromMethod(nullptr, py_tuplegetter));

    const proto::ProtoString* py_getitem = PythonEnvironment::getInternalString(ctx, "__getitem__");
    const proto::ProtoObject* defaultdictPrototype = env && env->getDictPrototype() ? env->getDictPrototype()->newChild(ctx, true) : ctx->newObject(false);
    if (env && env->getTypePrototype()) {
        defaultdictPrototype = defaultdictPrototype->setAttribute(ctx, py_getitem,
            ctx->fromMethod(nullptr, py_defaultdict_getitem));
        defaultdictPrototype = defaultdictPrototype->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__class__"), env->getTypePrototype());
    }

    const proto::ProtoObject* defaultdictMod = ctx->newObject(false);
    defaultdictMod = defaultdictMod->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__defaultdict_prototype__"), defaultdictPrototype);

    const proto::ProtoObject* ordereddictMod = ctx->newObject(false);

    module = module->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "defaultdict"),
                                 ctx->fromMethod(nullptr, py_defaultdict_new));
    module = module->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "OrderedDict"),
                                 ctx->fromMethod(nullptr, py_ordereddict_new));



    const proto::ProtoObject* deque_iterator = ctx->newObject(false);
    deque_iterator = deque_iterator->setAttribute(ctx, PythonEnvironment::getInternalString(ctx, "__name__"), PythonEnvironment::getInternedString(ctx, "_deque_iterator")->asObject(ctx));
    deque_iterator = deque_iterator->setAttribute(ctx, env->getNextString(),
                                                 ctx->fromMethod(nullptr, py_deque_iterator_next));
    deque_iterator = deque_iterator->setAttribute(ctx, env->getIterString(),
                                                 ctx->fromMethod(nullptr, py_collections_dummy)); // self iter
    
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
