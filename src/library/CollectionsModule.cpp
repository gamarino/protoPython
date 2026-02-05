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
};

static void deque_finalizer(void* ptr) {
    delete static_cast<DequeState*>(ptr);
}

static DequeState* get_deque_state(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    const proto::ProtoObject* ptrObj = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__deque_ptr__"));
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
    if (state && posArgs->getSize(ctx) > 0) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->data.push_back(posArgs->getAt(ctx, 0));
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
    if (state && posArgs->getSize(ctx) > 0) {
        std::lock_guard<std::mutex> lock(state->mutex);
        state->data.push_front(posArgs->getAt(ctx, 0));
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

static const proto::ProtoObject* py_defaultdict_getitem(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(ctx, "__data__");
    const proto::ProtoObject* data = self->getAttribute(ctx, dataName);
    if (!data || !data->asSparseList(ctx)) return PROTO_NONE;
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;

    const proto::ProtoObject* key = posArgs->getAt(ctx, 0);
    unsigned long hash = key->getHash(ctx);
    const proto::ProtoObject* value = data->asSparseList(ctx)->getAt(ctx, hash);
    if (value) return value;

    const proto::ProtoObject* factory = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "default_factory"));
    if (!factory || factory == PROTO_NONE) {
        protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(ctx);
        if (env) env->raiseKeyError(ctx, key);
        return PROTO_NONE;
    }
    const proto::ProtoObject* callAttr = factory->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__call__"));
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
    const proto::ProtoString* keysName = proto::ProtoString::fromUTF8String(ctx, "__keys__");
    const proto::ProtoObject* keysObj = self->getAttribute(ctx, keysName);
    const proto::ProtoList* keysList = keysObj && keysObj->asList(ctx) ? keysObj->asList(ctx) : ctx->newList();
    keysList = keysList->appendLast(ctx, key);
    self->setAttribute(ctx, keysName, keysList->asObject(ctx));
    return value;
}

static const proto::ProtoObject* py_defaultdict_new(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoObject* proto = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__defaultdict_prototype__"));
    if (!proto) return PROTO_NONE;

    const proto::ProtoObject* d = proto->newChild(ctx, true);
    d = d->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__data__"), ctx->newSparseList()->asObject(ctx));
    d = d->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__keys__"), ctx->newList()->asObject(ctx));
    const proto::ProtoObject* factory = posArgs->getSize(ctx) > 0 ? posArgs->getAt(ctx, 0) : PROTO_NONE;
    d = d->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "default_factory"), factory ? factory : PROTO_NONE);
    return d;
}

static const proto::ProtoObject* py_ordereddict_new(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoObject* d = ctx->newObject(true);
    d = d->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__data__"), ctx->newSparseList()->asObject(ctx));
    d = d->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__keys__"), ctx->newList()->asObject(ctx));
    return d;
}

static const proto::ProtoObject* py_deque_new(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self, // This is the module
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwArgs) {
    
    const proto::ProtoObject* proto = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__deque_prototype__"));
    if (!proto) return PROTO_NONE;

    const proto::ProtoObject* instance = proto->newChild(ctx, true);
    DequeState* state = new DequeState();
    instance = instance->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__deque_ptr__"), 
                                    ctx->fromExternalPointer(state, deque_finalizer));
    
    return instance;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx, const proto::ProtoObject* dictPrototype) {
    const proto::ProtoObject* module = ctx->newObject(true);
    const proto::ProtoObject* dequePrototype = ctx->newObject(true);
    
    dequePrototype = dequePrototype->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "append"), 
                                                 ctx->fromMethod(const_cast<proto::ProtoObject*>(dequePrototype), py_deque_append));
    dequePrototype = dequePrototype->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "appendleft"), 
                                                 ctx->fromMethod(const_cast<proto::ProtoObject*>(dequePrototype), py_deque_appendleft));
    dequePrototype = dequePrototype->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "pop"), 
                                                 ctx->fromMethod(const_cast<proto::ProtoObject*>(dequePrototype), py_deque_pop));
    dequePrototype = dequePrototype->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "popleft"), 
                                                 ctx->fromMethod(const_cast<proto::ProtoObject*>(dequePrototype), py_deque_popleft));
    dequePrototype = dequePrototype->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__len__"), 
                                                 ctx->fromMethod(const_cast<proto::ProtoObject*>(dequePrototype), py_deque_len));
    
    // Store prototype in module
    module = module->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__deque_prototype__"), dequePrototype);

    module = module->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "deque"),
                                 ctx->fromMethod(const_cast<proto::ProtoObject*>(module), py_deque_new));

    const proto::ProtoString* py_getitem = proto::ProtoString::fromUTF8String(ctx, "__getitem__");
    const proto::ProtoObject* defaultdictPrototype = ctx->newObject(true);
    if (dictPrototype) {
        defaultdictPrototype = defaultdictPrototype->addParent(ctx, dictPrototype);
    }
    defaultdictPrototype = defaultdictPrototype->setAttribute(ctx, py_getitem,
        ctx->fromMethod(const_cast<proto::ProtoObject*>(defaultdictPrototype), py_defaultdict_getitem));

    const proto::ProtoObject* defaultdictMod = ctx->newObject(true);
    defaultdictMod = defaultdictMod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__defaultdict_prototype__"), defaultdictPrototype);

    const proto::ProtoObject* ordereddictMod = ctx->newObject(true);

    module = module->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "defaultdict"),
                                 ctx->fromMethod(const_cast<proto::ProtoObject*>(defaultdictMod), py_defaultdict_new));
    module = module->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "OrderedDict"),
                                 ctx->fromMethod(const_cast<proto::ProtoObject*>(ordereddictMod), py_ordereddict_new));

    return module;
}

} // namespace collections
} // namespace protoPython
