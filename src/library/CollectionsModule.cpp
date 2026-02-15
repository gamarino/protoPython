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
    if (state && posArgs->getSize(ctx) > 0) {
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

static const proto::ProtoObject* py_module_repr(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    (void)parentLink; (void)positionalParameters; (void)keywordParameters;
    const proto::ProtoObject* name = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__name__"));
    std::string s = "<module '";
    if (name && name->isString(context)) {
        std::string n; name->asString(context)->toUTF8String(context, n);
        s += n;
    } else {
        s += "unknown";
    }
    s += "'>";
    return context->fromUTF8String(s.c_str());
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
    return context->fromUTF8String(s.c_str());
}

static const proto::ProtoObject* py_deque_iter(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwArgs) {
    (void)parentLink; (void)posArgs; (void)kwArgs;
    
    const proto::ProtoObject* itProto = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__deque_iterator_proto__"));
    if (!itProto) return PROTO_NONE;
    
    DequeState* state = get_deque_state(ctx, self);
    if (!state) return PROTO_NONE;

    const proto::ProtoObject* instance = itProto->newChild(ctx, true);
    instance = instance->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__deque_obj__"), self);
    instance = instance->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__deque_idx__"), ctx->fromInteger(0));
    instance = instance->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__deque_mutation__"), ctx->fromInteger(state->mutationCount));
    return instance;
}

static const proto::ProtoObject* py_deque_reversed(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwArgs) {
    (void)parentLink; (void)posArgs; (void)kwArgs;
    
    const proto::ProtoObject* itProto = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__deque_reverse_iterator_proto__"));
    if (!itProto) return PROTO_NONE;
    
    DequeState* state = get_deque_state(ctx, self);
    if (!state) return PROTO_NONE;

    const proto::ProtoObject* instance = itProto->newChild(ctx, true);
    instance = instance->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__deque_obj__"), self);
    instance = instance->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__deque_idx__"), ctx->fromInteger(state->data.size() - 1));
    instance = instance->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__deque_mutation__"), ctx->fromInteger(state->mutationCount));
    return instance;
}

static const proto::ProtoObject* py_deque_iterator_next(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwArgs) {
    (void)parentLink; (void)posArgs; (void)kwArgs;
    
    const proto::ProtoObject* dequeObj = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__deque_obj__"));
    const proto::ProtoObject* idxObj = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__deque_idx__"));
    const proto::ProtoObject* mutationObj = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__deque_mutation__"));
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
    self->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__deque_idx__"), ctx->fromInteger(idx + 1));
    return val;
}

static const proto::ProtoObject* py_deque_reverse_iterator_next(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwArgs) {
    (void)parentLink; (void)posArgs; (void)kwArgs;
    
    const proto::ProtoObject* dequeObj = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__deque_obj__"));
    const proto::ProtoObject* idxObj = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__deque_idx__"));
    const proto::ProtoObject* mutationObj = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__deque_mutation__"));
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
    self->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__deque_idx__"), ctx->fromInteger(idx - 1));
    return val;
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
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwArgs) {
    (void)parentLink; (void)kwArgs;
    const proto::ProtoObject* instance = self->newChild(ctx, true);
    instance = instance->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__class__"), self);
    DequeState* state = new DequeState();
    instance = instance->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__deque_ptr__"), 
                                    ctx->fromExternalPointer(state, deque_finalizer));
    
    if (posArgs->getSize(ctx) > 0) {
        const proto::ProtoObject* iterable = posArgs->getAt(ctx, 0);
        protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(ctx);
        const proto::ProtoString* iterS = env ? env->getIterString() : proto::ProtoString::fromUTF8String(ctx, "__iter__");
        const proto::ProtoObject* iterM = iterable->getAttribute(ctx, iterS);
        if (iterM && iterM->asMethod(ctx)) {
            const proto::ProtoList* emptyL = env ? env->getEmptyList() : ctx->newList();
            const proto::ProtoObject* it = iterM->asMethod(ctx)(ctx, iterable, nullptr, emptyL, nullptr);
            if (it && it != PROTO_NONE) {
                const proto::ProtoString* nextS = env ? env->getNextString() : proto::ProtoString::fromUTF8String(ctx, "__next__");
                const proto::ProtoObject* nextM = it->getAttribute(ctx, nextS);
                if (nextM && nextM->asMethod(ctx)) {
                    for (;;) {
                        const proto::ProtoObject* item = nextM->asMethod(ctx)(ctx, it, nullptr, emptyL, nullptr);
                        if (!item) break;
                        state->data.push_back(item);
                    }
                }
            }
        }
    }
    
    return instance;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx, protoPython::PythonEnvironment* env) {
    const proto::ProtoObject* module = ctx->newObject(true);
    const proto::ProtoObject* dequePrototype = ctx->newObject(true);
    
    if (env && env->getObjectPrototype()) {
        module = module->addParent(ctx, env->getObjectPrototype());
        dequePrototype = dequePrototype->addParent(ctx, env->getObjectPrototype());
    }
    
    if (env && env->getTypePrototype()) {
        dequePrototype = dequePrototype->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__class__"), env->getTypePrototype());
    }
    dequePrototype = dequePrototype->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__name__"), ctx->fromUTF8String("deque"));
    dequePrototype = dequePrototype->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__call__"),
                                                 ctx->fromMethod(nullptr, py_deque_new));
    
    dequePrototype = dequePrototype->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "append"), 
                                                 ctx->fromMethod(nullptr, py_deque_append));
    dequePrototype = dequePrototype->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "appendleft"), 
                                                 ctx->fromMethod(nullptr, py_deque_appendleft));
    dequePrototype = dequePrototype->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "pop"), 
                                                 ctx->fromMethod(nullptr, py_deque_pop));
    dequePrototype = dequePrototype->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "popleft"), 
                                                 ctx->fromMethod(nullptr, py_deque_popleft));
    dequePrototype = dequePrototype->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__len__"), 
                                                 ctx->fromMethod(nullptr, py_deque_len));
    dequePrototype = dequePrototype->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__reversed__"), 
                                                 ctx->fromMethod(nullptr, py_deque_reversed));
    
    // Store prototype in module
    module = module->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__deque_prototype__"), dequePrototype);

    module = module->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "deque"),
                                 ctx->fromMethod(nullptr, py_deque_new));

    const proto::ProtoString* py_getitem = proto::ProtoString::fromUTF8String(ctx, "__getitem__");
    const proto::ProtoObject* defaultdictPrototype = ctx->newObject(true);
    if (env && env->getDictPrototype()) {
        defaultdictPrototype = defaultdictPrototype->addParent(ctx, env->getDictPrototype());
    }
    if (env && env->getTypePrototype()) {
        defaultdictPrototype = defaultdictPrototype->setAttribute(ctx, py_getitem,
            ctx->fromMethod(nullptr, py_defaultdict_getitem));
        defaultdictPrototype = defaultdictPrototype->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__class__"), env->getTypePrototype());
    }

    const proto::ProtoObject* defaultdictMod = ctx->newObject(true);
    defaultdictMod = defaultdictMod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__defaultdict_prototype__"), defaultdictPrototype);

    const proto::ProtoObject* ordereddictMod = ctx->newObject(true);

    module = module->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "defaultdict"),
                                 ctx->fromMethod(nullptr, py_defaultdict_new));
    module = module->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "OrderedDict"),
                                 ctx->fromMethod(nullptr, py_ordereddict_new));
    module = module->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "deque"), dequePrototype);

    // Dummy _deque_iterator and _tuplegetter to satisfy collections/__init__.py
    const proto::ProtoObject* tuplegetter = ctx->newObject(true);
    tuplegetter = tuplegetter->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__name__"), ctx->fromUTF8String("_tuplegetter"));
    // No env available here for objectPrototype easily without changing signature, 
    // but we can at least avoid self-reference.
    module = module->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "_tuplegetter"), tuplegetter);

    const proto::ProtoObject* deque_iterator = ctx->newObject(true);
    deque_iterator = deque_iterator->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__name__"), ctx->fromUTF8String("_deque_iterator"));
    deque_iterator = deque_iterator->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__next__"),
                                                 ctx->fromMethod(nullptr, py_deque_iterator_next));
    deque_iterator = deque_iterator->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter__"),
                                                 ctx->fromMethod(nullptr, py_collections_dummy)); // self iter
    
    dequePrototype = dequePrototype->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter__"),
                                                 ctx->fromMethod(nullptr, py_deque_iter));
    dequePrototype = dequePrototype->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__repr__"),
                                                 ctx->fromMethod(nullptr, py_deque_repr));
    dequePrototype = dequePrototype->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__str__"),
                                                 ctx->fromMethod(nullptr, py_deque_repr));
    dequePrototype = dequePrototype->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__deque_iterator_proto__"), deque_iterator);

    const proto::ProtoObject* deque_reverse_iterator = ctx->newObject(true);
    deque_reverse_iterator = deque_reverse_iterator->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__name__"), ctx->fromUTF8String("_deque_reverse_iterator"));
    deque_reverse_iterator = deque_reverse_iterator->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__next__"),
                                                  ctx->fromMethod(nullptr, py_deque_reverse_iterator_next));
    deque_reverse_iterator = deque_reverse_iterator->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter__"),
                                                  ctx->fromMethod(nullptr, py_collections_dummy)); // self iter
    
    dequePrototype = dequePrototype->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__deque_reverse_iterator_proto__"), deque_reverse_iterator);

    module = module->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "_deque_iterator"), deque_iterator);

    // Dummy _count_elements for Counter
    module = module->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "_count_elements"),
                                 ctx->fromMethod(nullptr, py_collections_dummy));

    // Set __class__ on the module for better diagnostics
    module = module->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__name__"), ctx->fromUTF8String("_collections"));
    module = module->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__repr__"), ctx->fromMethod(nullptr, py_module_repr));

    return module;
}

} // namespace collections
} // namespace protoPython
