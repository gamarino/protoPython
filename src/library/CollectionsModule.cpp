#include <protoPython/CollectionsModule.h>
#include <deque>
#include <mutex>

namespace protoPython {
namespace collections {

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

static const proto::ProtoObject* py_defaultdict_new(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoObject* d = ctx->newObject(true);
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

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
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

    const proto::ProtoObject* defaultdictMod = ctx->newObject(true);
    const proto::ProtoObject* ordereddictMod = ctx->newObject(true);

    module = module->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "defaultdict"),
                                 ctx->fromMethod(const_cast<proto::ProtoObject*>(defaultdictMod), py_defaultdict_new));
    module = module->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "OrderedDict"),
                                 ctx->fromMethod(const_cast<proto::ProtoObject*>(ordereddictMod), py_ordereddict_new));

    return module;
}

} // namespace collections
} // namespace protoPython
