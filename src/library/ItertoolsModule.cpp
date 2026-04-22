#include <protoPython/PythonEnvironment.h>
#include <protoPython/ItertoolsModule.h>
#include <string>

namespace protoPython {
namespace itertools {

static const proto::ProtoObject* py_count_next(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* cur = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__count_cur__"));
    const proto::ProtoObject* step = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__count_step__"));
    if (!cur || !cur->isInteger(ctx) || !step) return PROTO_NONE;
    long long v = cur->asLong(ctx);
    long long s = step->isInteger(ctx) ? step->asLong(ctx) : 1;
    self->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__count_cur__"), ctx->fromInteger(v + s));
    return ctx->fromInteger(v);
}

static const proto::ProtoObject* py_iter_self(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    return self;
}

static const proto::ProtoObject* py_batched_stub(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    // batched(iterable, n) stub
    return ctx->newList()->asObject(ctx);
}

static const proto::ProtoObject* py_count(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    long long start = 0, step = 1;
    if (posArgs->getSize(ctx) >= 1 && posArgs->getAt(ctx, 0)->isInteger(ctx))
        start = posArgs->getAt(ctx, 0)->asLong(ctx);
    if (posArgs->getSize(ctx) >= 2 && posArgs->getAt(ctx, 1)->isInteger(ctx))
        step = posArgs->getAt(ctx, 1)->asLong(ctx);

    const proto::ProtoObject* proto = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__count_proto__"));
    if (!proto) return PROTO_NONE;
    const proto::ProtoObject* c = proto->newChild(ctx, true);
    c = c->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__count_cur__"), ctx->fromInteger(start));
    c = c->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__count_step__"), ctx->fromInteger(step));
    return c;
}

static const proto::ProtoObject* py_islice_next(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* it = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__islice_it__"));
    const proto::ProtoObject* stopObj = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__islice_stop__"));
    const proto::ProtoObject* idxObj = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__islice_idx__"));
    if (!it || !stopObj || !idxObj) return PROTO_NONE;
    long long idx = idxObj->asLong(ctx);
    long long stop = stopObj->asLong(ctx);
    if (idx >= stop) return nullptr;

    const proto::ProtoObject* nextM = it->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__next__"));
    if (!nextM || !nextM->asMethod(ctx)) return nullptr;
    const proto::ProtoObject* val = nextM->asMethod(ctx)(ctx, it, nullptr, ctx->newList(), nullptr);
    if (!val || val == PROTO_NONE) return nullptr;
    self->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__islice_idx__"), ctx->fromInteger(idx + 1));
    return val;
}

static const proto::ProtoObject* py_islice(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* iterable = posArgs->getAt(ctx, 0);
    long long start = 0, stop = 0, step = 1;
    if (posArgs->getSize(ctx) == 2) {
        stop = posArgs->getAt(ctx, 1)->asLong(ctx);
    } else {
        start = posArgs->getAt(ctx, 1)->asLong(ctx);
        stop = posArgs->getAt(ctx, 2)->asLong(ctx);
        if (posArgs->getSize(ctx) >= 4) step = posArgs->getAt(ctx, 3)->asLong(ctx);
    }

    const proto::ProtoObject* iterM = iterable->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__iter__"));
    if (!iterM || !iterM->asMethod(ctx)) return PROTO_NONE;
    const proto::ProtoObject* it = iterM->asMethod(ctx)(ctx, iterable, nullptr, ctx->newList(), nullptr);
    if (!it) return PROTO_NONE;

    for (long long i = 0; i < start; ++i) {
        const proto::ProtoObject* nextM = it->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__next__"));
        if (!nextM || !nextM->asMethod(ctx)) break;
        const proto::ProtoObject* v = nextM->asMethod(ctx)(ctx, it, nullptr, ctx->newList(), nullptr);
        if (!v || v == PROTO_NONE) break;
    }

    const proto::ProtoObject* proto = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__islice_proto__"));
    if (!proto) return PROTO_NONE;
    const proto::ProtoObject* sl = proto->newChild(ctx, true);
    sl = sl->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__islice_it__"), it);
    sl = sl->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__islice_stop__"), ctx->fromInteger(stop));
    sl = sl->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__islice_idx__"), ctx->fromInteger(start));
    return sl;
}

static const proto::ProtoObject* py_chain_next(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* itersObj = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__chain_iters__"));
    const proto::ProtoObject* idxObj = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__chain_idx__"));
    if (!itersObj || !itersObj->asList(ctx) || !idxObj || !idxObj->isInteger(ctx)) return nullptr;
    const proto::ProtoList* iters = itersObj->asList(ctx);
    long long idx = idxObj->asLong(ctx);
    unsigned long n = iters->getSize(ctx);
    while (static_cast<unsigned long>(idx) < n) {
        const proto::ProtoObject* it = iters->getAt(ctx, static_cast<int>(idx));
        const proto::ProtoObject* nextM = it ? it->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__next__")) : nullptr;
        if (!nextM || !nextM->asMethod(ctx)) {
            idx++;
            self->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__chain_idx__"), ctx->fromInteger(idx));
            continue;
        }
        const proto::ProtoObject* val = nextM->asMethod(ctx)(ctx, it, nullptr, ctx->newList(), nullptr);
        if (val && val != PROTO_NONE) return val;
        idx++;
        self->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__chain_idx__"), ctx->fromInteger(idx));
    }
    return nullptr;
}

static const proto::ProtoObject* py_repeat_next(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* obj = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__repeat_obj__"));
    const proto::ProtoObject* timesObj = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__repeat_times__"));
    const proto::ProtoObject* countObj = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__repeat_count__"));
    if (!obj || !timesObj || !countObj) return PROTO_NONE;
    if (timesObj != PROTO_NONE && timesObj->isInteger(ctx)) {
        long long times = timesObj->asLong(ctx);
        long long count = countObj->isInteger(ctx) ? countObj->asLong(ctx) : 0;
        if (count >= times) return nullptr;
        self->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__repeat_count__"), ctx->fromInteger(count + 1));
    }
    return obj;
}

static const proto::ProtoObject* py_repeat(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* obj = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* times = PROTO_NONE;
    if (posArgs->getSize(ctx) >= 2) times = posArgs->getAt(ctx, 1);

    const proto::ProtoObject* proto = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__repeat_proto__"));
    if (!proto) return PROTO_NONE;
    const proto::ProtoObject* r = proto->newChild(ctx, true);
    r = r->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__repeat_obj__"), obj);
    r = r->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__repeat_times__"), times ? times : PROTO_NONE);
    r = r->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__repeat_count__"), ctx->fromInteger(0));
    return r;
}

static const proto::ProtoObject* py_cycle_next(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* it = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__cycle_it__"));
    const proto::ProtoObject* cacheObj = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__cycle_cache__"));
    const proto::ProtoObject* idxObj = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__cycle_idx__"));
    if (!it || !cacheObj || !cacheObj->asList(ctx) || !idxObj) return PROTO_NONE;
    const proto::ProtoList* cache = cacheObj->asList(ctx);
    long long idx = idxObj->asLong(ctx);
    if (idx < static_cast<long long>(cache->getSize(ctx))) {
        const proto::ProtoObject* val = cache->getAt(ctx, static_cast<int>(idx));
        self->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__cycle_idx__"), ctx->fromInteger(idx + 1));
        return val;
    }
    const proto::ProtoObject* nextM = it->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__next__"));
    if (!nextM || !nextM->asMethod(ctx)) return nullptr;
    const proto::ProtoObject* val = nextM->asMethod(ctx)(ctx, it, nullptr, ctx->newList(), nullptr);
    if (!val || val == PROTO_NONE) {
        if (cache->getSize(ctx) == 0) return nullptr;
        self->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__cycle_idx__"), ctx->fromInteger(1));
        return cache->getAt(ctx, 0);
    }
    const proto::ProtoList* newCache = cache->appendLast(ctx, val);
    self->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__cycle_cache__"), newCache->asObject(ctx));
    self->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__cycle_idx__"), ctx->fromInteger(newCache->getSize(ctx)));
    return val;
}

static const proto::ProtoObject* py_cycle(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* iterable = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* iterM = iterable->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__iter__"));
    if (!iterM || !iterM->asMethod(ctx)) return PROTO_NONE;
    const proto::ProtoObject* it = iterM->asMethod(ctx)(ctx, iterable, nullptr, ctx->newList(), nullptr);
    if (!it) return PROTO_NONE;
    const proto::ProtoObject* proto = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__cycle_proto__"));
    if (!proto) return PROTO_NONE;
    const proto::ProtoObject* c = proto->newChild(ctx, true);
    c = c->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__cycle_it__"), it);
    c = c->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__cycle_cache__"), ctx->newList()->asObject(ctx));
    c = c->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__cycle_idx__"), ctx->fromInteger(0));
    return c;
}

static const proto::ProtoObject* py_filterfalse_next(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* pred = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__filterfalse_pred__"));
    const proto::ProtoObject* it = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__filterfalse_it__"));
    if (!it) return nullptr;
    const proto::ProtoObject* nextM = it->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__next__"));
    if (!nextM || !nextM->asMethod(ctx)) return nullptr;

    for (;;) {
        const proto::ProtoObject* val = nextM->asMethod(ctx)(ctx, it, nullptr, ctx->newList(), nullptr);
        if (!val || val == PROTO_NONE) return nullptr;

        bool ok;
        if (pred == PROTO_NONE) {
            ok = (val == PROTO_FALSE || (val->isInteger(ctx) && val->asLong(ctx) == 0));
        } else {
            const proto::ProtoList* args = ctx->newList()->appendLast(ctx, val);
            const proto::ProtoObject* res = pred->call(ctx, nullptr, PythonEnvironment::getInternedString(ctx, "__call__"), pred, args, nullptr);
            ok = (!res || res == PROTO_NONE || res == PROTO_FALSE || (res->isInteger(ctx) && res->asLong(ctx) == 0));
        }

        if (ok) return val;
    }
}

static const proto::ProtoObject* py_filterfalse(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    // If called as type(pred, iterable), posArgs[0] is the type itself.
    // If called as filterfalse(pred, iterable), posArgs size is 2.
    size_t startIdx = 0;
    if (posArgs && posArgs->getSize(ctx) >= 3 && posArgs->getAt(ctx, 0) == self) {
        startIdx = 1;
    }
    
    if (!posArgs || posArgs->getSize(ctx) < startIdx + 2) return PROTO_NONE;
    const proto::ProtoObject* pred = posArgs->getAt(ctx, (int)startIdx);
    const proto::ProtoObject* iterable = posArgs->getAt(ctx, (int)startIdx + 1);

    const proto::ProtoObject* iterM = iterable->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__iter__"));
    if (!iterM || !iterM->asMethod(ctx)) return PROTO_NONE;
    const proto::ProtoObject* it = iterM->asMethod(ctx)(ctx, iterable, nullptr, ctx->newList(), nullptr);
    if (!it) return PROTO_NONE;

    const proto::ProtoObject* ff = self->newChild(ctx, true);
    ff = ff->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__filterfalse_pred__"), pred);
    ff = ff->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__filterfalse_it__"), it);
    return ff;
}

static const proto::ProtoObject* py_takewhile_next(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* pred = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__takewhile_pred__"));
    const proto::ProtoObject* it = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__takewhile_it__"));
    if (!pred || !pred->asMethod(ctx) || !it) return nullptr;
    const proto::ProtoObject* nextM = it->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__next__"));
    if (!nextM || !nextM->asMethod(ctx)) return nullptr;
    const proto::ProtoObject* val = nextM->asMethod(ctx)(ctx, it, nullptr, ctx->newList(), nullptr);
    if (!val || val == PROTO_NONE) return nullptr;
    const proto::ProtoList* predArgs = ctx->newList()->appendLast(ctx, val);
    const proto::ProtoObject* predResult = pred->asMethod(ctx)(ctx, pred, nullptr, predArgs, nullptr);
    bool ok = (predResult && predResult != PROTO_NONE && predResult != PROTO_FALSE);
    if (predResult && predResult->isInteger(ctx) && predResult->asLong(ctx) != 0) ok = true;
    if (!ok) return nullptr;
    return val;
}

static const proto::ProtoObject* py_takewhile(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* pred = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* iterable = posArgs->getAt(ctx, 1);
    const proto::ProtoObject* iterM = iterable->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__iter__"));
    if (!iterM || !iterM->asMethod(ctx)) return PROTO_NONE;
    const proto::ProtoObject* it = iterM->asMethod(ctx)(ctx, iterable, nullptr, ctx->newList(), nullptr);
    if (!it) return PROTO_NONE;
    const proto::ProtoObject* proto = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__takewhile_proto__"));
    if (!proto) return PROTO_NONE;
    const proto::ProtoObject* tw = proto->newChild(ctx, true);
    tw = tw->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__takewhile_pred__"), pred);
    tw = tw->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__takewhile_it__"), it);
    return tw;
}

static const proto::ProtoObject* py_dropwhile_next(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* pred = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__dropwhile_pred__"));
    const proto::ProtoObject* it = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__dropwhile_it__"));
    if (!pred || !pred->asMethod(ctx) || !it) return PROTO_NONE;
    const proto::ProtoObject* nextM = it->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__next__"));
    if (!nextM || !nextM->asMethod(ctx)) return PROTO_NONE;
    for (;;) {
        const proto::ProtoObject* val = nextM->asMethod(ctx)(ctx, it, nullptr, ctx->newList(), nullptr);
        if (!val || val == PROTO_NONE) return PROTO_NONE;
        const proto::ProtoObject* dropObj = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__dropwhile_dropping__"));
        if (dropObj == PROTO_NONE || !dropObj->isInteger(ctx) || dropObj->asLong(ctx) == 0)
            return val;
        const proto::ProtoList* predArgs = ctx->newList()->appendLast(ctx, val);
        const proto::ProtoObject* predResult = pred->asMethod(ctx)(ctx, pred, nullptr, predArgs, nullptr);
        bool predTrue = (predResult && predResult != PROTO_NONE && predResult != PROTO_FALSE);
        if (predResult && predResult->isInteger(ctx) && predResult->asLong(ctx) != 0) predTrue = true;
        if (!predTrue) {
            self->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__dropwhile_dropping__"), ctx->fromInteger(0));
            return val;
        }
    }
}

static const proto::ProtoObject* py_dropwhile(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* pred = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* iterable = posArgs->getAt(ctx, 1);
    const proto::ProtoObject* iterM = iterable->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__iter__"));
    if (!iterM || !iterM->asMethod(ctx)) return PROTO_NONE;
    const proto::ProtoObject* it = iterM->asMethod(ctx)(ctx, iterable, nullptr, ctx->newList(), nullptr);
    if (!it) return PROTO_NONE;
    const proto::ProtoObject* proto = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__dropwhile_proto__"));
    if (!proto) return PROTO_NONE;
    const proto::ProtoObject* dw = proto->newChild(ctx, true);
    dw = dw->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__dropwhile_pred__"), pred);
    dw = dw->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__dropwhile_it__"), it);
    dw = dw->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__dropwhile_dropping__"), ctx->fromInteger(1));
    return dw;
}

static const proto::ProtoObject* py_tee(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* iterable = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* iterM = iterable->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__iter__"));
    if (!iterM || !iterM->asMethod(ctx)) return PROTO_NONE;
    const proto::ProtoObject* it1 = iterM->asMethod(ctx)(ctx, iterable, nullptr, ctx->newList(), nullptr);
    if (!it1) return PROTO_NONE;
    const proto::ProtoObject* it2 = iterM->asMethod(ctx)(ctx, iterable, nullptr, ctx->newList(), nullptr);
    if (!it2) return PROTO_NONE;
    const proto::ProtoList* pair = ctx->newList()->appendLast(ctx, it1)->appendLast(ctx, it2);
    const proto::ProtoTuple* tup = ctx->newTupleFromList(pair);
    return tup ? tup->asObject(ctx) : PROTO_NONE;
}

/** Returns an empty iterator (chain of no iterables). */
static const proto::ProtoObject* empty_iterator(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    const proto::ProtoObject* chainM = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "chain"));
    if (!chainM || !chainM->asMethod(ctx)) return PROTO_NONE;
    return chainM->asMethod(ctx)(ctx, self, nullptr, ctx->newList(), nullptr);
}

/** Binary add for two ProtoObjects (int, float, str); else calls left.__add__(right). */
static const proto::ProtoObject* accumulate_add(
    proto::ProtoContext* ctx, const proto::ProtoObject* left, const proto::ProtoObject* right) {
    if (left->isInteger(ctx) && right->isInteger(ctx))
        return ctx->fromInteger(left->asLong(ctx) + right->asLong(ctx));
    if (left->isDouble(ctx) || right->isDouble(ctx)) {
        double a = left->isDouble(ctx) ? left->asDouble(ctx) : (left->isInteger(ctx) ? static_cast<double>(left->asLong(ctx)) : 0.0);
        double b = right->isDouble(ctx) ? right->asDouble(ctx) : (right->isInteger(ctx) ? static_cast<double>(right->asLong(ctx)) : 0.0);
        return ctx->fromDouble(a + b);
    }
    if (left->isString(ctx) && right->isString(ctx)) {
        std::string sa, sb;
        left->asString(ctx)->toUTF8String(ctx, sa);
        right->asString(ctx)->toUTF8String(ctx, sb);
        return PythonEnvironment::getInternedString(ctx, (sa + sb).c_str())->asObject(ctx);
    }
    const proto::ProtoObject* addM = left->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__add__"));
    if (addM && addM->asMethod(ctx)) {
        const proto::ProtoList* args = ctx->newList()->appendLast(ctx, right);
        return addM->asMethod(ctx)(ctx, const_cast<proto::ProtoObject*>(left), nullptr, args, nullptr);
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_accumulate_next(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* it = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__accum_it__"));
    const proto::ProtoObject* totalObj = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__accum_total__"));
    const proto::ProtoObject* func = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__accum_func__"));
    if (!it) return nullptr;
    const proto::ProtoObject* nextM = it->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__next__"));
    if (!nextM || !nextM->asMethod(ctx)) return nullptr;

    if (!totalObj || totalObj == PROTO_NONE) {
        const proto::ProtoObject* first = nextM->asMethod(ctx)(ctx, it, nullptr, ctx->newList(), nullptr);
        if (!first || first == PROTO_NONE) return nullptr;
        self->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__accum_total__"), first);
        return first;
    }
    const proto::ProtoObject* nextVal = nextM->asMethod(ctx)(ctx, it, nullptr, ctx->newList(), nullptr);
    if (!nextVal || nextVal == PROTO_NONE) return nullptr;
    const proto::ProtoObject* newTotal;
    if (func && func != PROTO_NONE) {
        const proto::ProtoList* args = ctx->newList()->appendLast(ctx, totalObj)->appendLast(ctx, nextVal);
        const proto::ProtoObject* callResult = func->call(ctx, nullptr, PythonEnvironment::getInternedString(ctx, "__call__"), func, args, nullptr);
        if (!callResult || callResult == PROTO_NONE) return nullptr;
        newTotal = callResult;
    } else {
        newTotal = accumulate_add(ctx, totalObj, nextVal);
        if (!newTotal || newTotal == PROTO_NONE) return nullptr;
    }
    self->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__accum_total__"), newTotal);
    return newTotal;
}

static const proto::ProtoObject* py_accumulate(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* iterable = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* func = (posArgs->getSize(ctx) >= 2) ? posArgs->getAt(ctx, 1) : PROTO_NONE;
    const proto::ProtoObject* iterM = iterable->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__iter__"));
    if (!iterM || !iterM->asMethod(ctx)) return PROTO_NONE;
    const proto::ProtoObject* it = iterM->asMethod(ctx)(ctx, iterable, nullptr, ctx->newList(), nullptr);
    if (!it) return PROTO_NONE;
    const proto::ProtoObject* proto = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__accumulate_proto__"));
    if (!proto) return PROTO_NONE;
    const proto::ProtoObject* acc = proto->newChild(ctx, true);
    acc = acc->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__accum_it__"), it);
    acc = acc->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__accum_total__"), PROTO_NONE);
    acc = acc->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__accum_func__"), func ? func : PROTO_NONE);
    return acc;
}

static const proto::ProtoObject* py_groupby_stub(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    (void)posArgs;
    return empty_iterator(ctx, self);
}

static const proto::ProtoObject* py_product_stub(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    (void)posArgs;
    return empty_iterator(ctx, self);
}

static const proto::ProtoObject* py_combinations_stub(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    (void)posArgs;
    return empty_iterator(ctx, self);
}

static const proto::ProtoObject* py_combinations_with_replacement_stub(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    (void)posArgs;
    return empty_iterator(ctx, self);
}

static const proto::ProtoObject* py_starmap_next(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* func = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__starmap_func__"));
    const proto::ProtoObject* it = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__starmap_it__"));
    if (!func || !it) return nullptr;
    const proto::ProtoObject* nextM = it->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__next__"));
    if (!nextM || !nextM->asMethod(ctx)) return nullptr;
    const proto::ProtoObject* argsObj = nextM->asMethod(ctx)(ctx, it, nullptr, ctx->newList(), nullptr);
    if (!argsObj || argsObj == PROTO_NONE) return nullptr;

    const proto::ProtoList* args = argsObj->asList(ctx);
    if (!args) {
        // If it's not a list, try converting it to one
        const proto::ProtoObject* iterM = argsObj->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__iter__"));
        if (iterM && iterM->asMethod(ctx)) {
            const proto::ProtoObject* tempIt = iterM->asMethod(ctx)(ctx, argsObj, nullptr, ctx->newList(), nullptr);
            if (tempIt) {
                const proto::ProtoList* L = ctx->newList();
                const proto::ProtoObject* nAttr = tempIt->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__next__"));
                if (nAttr && nAttr->asMethod(ctx)) {
                    while (const proto::ProtoObject* val = nAttr->asMethod(ctx)(ctx, tempIt, nullptr, ctx->newList(), nullptr)) {
                        L = L->appendLast(ctx, val);
                    }
                }
                args = L;
            }
        }
    }
    if (!args) return nullptr;

    const proto::ProtoObject* callM = func->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__call__"));
    if (!callM || !callM->asMethod(ctx)) return nullptr;
    return callM->asMethod(ctx)(ctx, func, nullptr, args, nullptr);
}

static const proto::ProtoObject* py_starmap(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* func = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* iterable = posArgs->getAt(ctx, 1);
    const proto::ProtoObject* iterM = iterable->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__iter__"));
    if (!iterM || !iterM->asMethod(ctx)) return PROTO_NONE;
    const proto::ProtoObject* it = iterM->asMethod(ctx)(ctx, iterable, nullptr, ctx->newList(), nullptr);
    if (!it) return PROTO_NONE;
    const proto::ProtoObject* proto = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__starmap_proto__"));
    if (!proto) return PROTO_NONE;
    const proto::ProtoObject* sm = proto->newChild(ctx, true);
    sm = sm->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__starmap_func__"), func);
    sm = sm->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__starmap_it__"), it);
    return sm;
}

static const proto::ProtoObject* py_permutations_stub(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    (void)posArgs;
    return empty_iterator(ctx, self);
}

static const proto::ProtoObject* py_chain_from_iterable(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* iterables = posArgs->getAt(ctx, 0);
    
    // We can implement this by creating a chain object that lazily consumes iterables.
    // For now, let's just collect all iterables if possible, or better, implement a proper from_iterable logic.
    // Actually, py_chain takes *args. from_iterable takes one argument (iterable of iterables).
    
    const proto::ProtoObject* iterM = iterables->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__iter__"));
    if (!iterM || !iterM->asMethod(ctx)) return PROTO_NONE;
    const proto::ProtoObject* it = iterM->asMethod(ctx)(ctx, iterables, nullptr, ctx->newList(), nullptr);
    if (!it) return PROTO_NONE;

    // Collect into a list for now (simple implementation)
    const proto::ProtoList* iters = ctx->newList();
    const proto::ProtoObject* nextM = it->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__next__"));
    if (nextM && nextM->asMethod(ctx)) {
        while (const proto::ProtoObject* item = nextM->asMethod(ctx)(ctx, it, nullptr, ctx->newList(), nullptr)) {
            if (item == PROTO_NONE) break;
            
            // Each item should be an iterable. Convert it to an iterator!
            const proto::ProtoObject* innerIterM = item->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__iter__"));
            if (innerIterM && innerIterM->asMethod(ctx)) {
                const proto::ProtoObject* innerIt = innerIterM->asMethod(ctx)(ctx, item, nullptr, ctx->newList(), nullptr);
                if (innerIt) iters = iters->appendLast(ctx, innerIt);
            }
        }
    }

    const proto::ProtoObject* chainProto = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__chain_proto__"));
    if (!chainProto) {
        // Try to find it on the module if self is the chain method
        // Actually, let's just use a hardcoded lookup if needed, but better pass it.
        return PROTO_NONE;
    }
    const proto::ProtoObject* ch = chainProto->newChild(ctx, true);
    ch = ch->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__chain_iters__"), iters->asObject(ctx));
    ch = ch->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__chain_idx__"), ctx->fromInteger(0));
    return ch;
}

static const proto::ProtoObject* py_chain(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    const proto::ProtoList* iters = ctx->newList();
    unsigned long n = posArgs->getSize(ctx);
    for (unsigned long i = 0; i < n; ++i) {
        const proto::ProtoObject* iterable = posArgs->getAt(ctx, static_cast<int>(i));
        const proto::ProtoObject* itAttr = iterable->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__iter__"));
        if (!itAttr || !itAttr->asMethod(ctx)) continue;
        const proto::ProtoObject* it = itAttr->asMethod(ctx)(ctx, iterable, nullptr, ctx->newList(), nullptr);
        if (it) iters = iters->appendLast(ctx, it);
    }
    const proto::ProtoObject* proto = self->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__chain_proto__"));
    if (!proto) return PROTO_NONE;
    const proto::ProtoObject* ch = proto->newChild(ctx, true);
    ch = ch->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__chain_iters__"), iters->asObject(ctx));
    ch = ch->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__chain_idx__"), ctx->fromInteger(0));
    return ch;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(false);

    const proto::ProtoObject* countProto = ctx->newObject(false);
    countProto = countProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__iter__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(countProto), py_iter_self));
    countProto = countProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__next__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(countProto), py_count_next));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__count_proto__"), countProto);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "count"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_count));

    const proto::ProtoObject* isliceProto = ctx->newObject(false);
    isliceProto = isliceProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__iter__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(isliceProto), py_iter_self));
    isliceProto = isliceProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__next__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(isliceProto), py_islice_next));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__islice_proto__"), isliceProto);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "islice"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_islice));

    const proto::ProtoObject* chainProto = ctx->newObject(false);
    chainProto = chainProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__iter__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(chainProto), py_iter_self));
    chainProto = chainProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__next__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(chainProto), py_chain_next));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__chain_proto__"), chainProto);
    const proto::ProtoObject* chainObj = ctx->newObject(false);
    chainObj = chainObj->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__call__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(chainObj), py_chain));
    chainObj = chainObj->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "from_iterable"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(chainObj), py_chain_from_iterable));
    chainObj = chainObj->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__chain_proto__"), chainProto);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "chain"), chainObj);

    const proto::ProtoObject* repeatProto = ctx->newObject(false);
    repeatProto = repeatProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__iter__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(repeatProto), py_iter_self));
    repeatProto = repeatProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__next__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(repeatProto), py_repeat_next));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__repeat_proto__"), repeatProto);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "repeat"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_repeat));

    const proto::ProtoObject* cycleProto = ctx->newObject(false);
    cycleProto = cycleProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__iter__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(cycleProto), py_iter_self));
    cycleProto = cycleProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__next__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(cycleProto), py_cycle_next));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__cycle_proto__"), cycleProto);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "cycle"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_cycle));

    const proto::ProtoObject* takewhileProto = ctx->newObject(false);
    takewhileProto = takewhileProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__iter__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(takewhileProto), py_iter_self));
    takewhileProto = takewhileProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__next__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(takewhileProto), py_takewhile_next));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__takewhile_proto__"), takewhileProto);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "takewhile"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_takewhile));

    const proto::ProtoObject* dropwhileProto = ctx->newObject(false);
    dropwhileProto = dropwhileProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__iter__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(dropwhileProto), py_iter_self));
    dropwhileProto = dropwhileProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__next__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(dropwhileProto), py_dropwhile_next));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__dropwhile_proto__"), dropwhileProto);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "dropwhile"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_dropwhile));

    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "tee"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_tee));
    const proto::ProtoObject* accumulateProto = ctx->newObject(false);
    accumulateProto = accumulateProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__iter__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(accumulateProto), py_iter_self));
    accumulateProto = accumulateProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__next__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(accumulateProto), py_accumulate_next));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__accumulate_proto__"), accumulateProto);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "accumulate"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_accumulate));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "groupby"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_groupby_stub));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "product"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_product_stub));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "combinations"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_combinations_stub));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "combinations_with_replacement"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_combinations_with_replacement_stub));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "permutations"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_permutations_stub));

    const proto::ProtoObject* starmapProto = ctx->newObject(false);
    starmapProto = starmapProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__iter__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(starmapProto), py_iter_self));
    starmapProto = starmapProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__next__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(starmapProto), py_starmap_next));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__starmap_proto__"), starmapProto);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "starmap"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_starmap));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "batched"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_batched_stub));

    const proto::ProtoObject* filterfalseProto = ctx->newObject(false);
    filterfalseProto = filterfalseProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__iter__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(filterfalseProto), py_iter_self));
    filterfalseProto = filterfalseProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__next__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(filterfalseProto), py_filterfalse_next));
    filterfalseProto = filterfalseProto->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "__new__"),
        ctx->fromMethod(nullptr, py_filterfalse));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "filterfalse"), filterfalseProto);

    return mod;
}

} // namespace itertools
} // namespace protoPython
