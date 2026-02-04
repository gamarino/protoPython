#include <protoPython/ItertoolsModule.h>

namespace protoPython {
namespace itertools {

static const proto::ProtoObject* py_count_next(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* cur = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__count_cur__"));
    const proto::ProtoObject* step = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__count_step__"));
    if (!cur || !cur->isInteger(ctx) || !step) return PROTO_NONE;
    long long v = cur->asLong(ctx);
    long long s = step->isInteger(ctx) ? step->asLong(ctx) : 1;
    self->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__count_cur__"), ctx->fromInteger(v + s));
    return ctx->fromInteger(v);
}

static const proto::ProtoObject* py_iter_self(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    return self;
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

    const proto::ProtoObject* proto = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__count_proto__"));
    if (!proto) return PROTO_NONE;
    const proto::ProtoObject* c = proto->newChild(ctx, true);
    c = c->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__count_cur__"), ctx->fromInteger(start));
    c = c->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__count_step__"), ctx->fromInteger(step));
    return c;
}

static const proto::ProtoObject* py_islice_next(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* it = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__islice_it__"));
    const proto::ProtoObject* stopObj = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__islice_stop__"));
    const proto::ProtoObject* idxObj = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__islice_idx__"));
    if (!it || !stopObj || !idxObj) return PROTO_NONE;
    long long idx = idxObj->asLong(ctx);
    long long stop = stopObj->asLong(ctx);
    if (idx >= stop) return PROTO_NONE;

    const proto::ProtoObject* nextM = it->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__next__"));
    if (!nextM || !nextM->asMethod(ctx)) return PROTO_NONE;
    const proto::ProtoObject* val = nextM->asMethod(ctx)(ctx, it, nullptr, ctx->newList(), nullptr);
    if (!val || val == PROTO_NONE) return PROTO_NONE;
    self->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__islice_idx__"), ctx->fromInteger(idx + 1));
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

    const proto::ProtoObject* iterM = iterable->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter__"));
    if (!iterM || !iterM->asMethod(ctx)) return PROTO_NONE;
    const proto::ProtoObject* it = iterM->asMethod(ctx)(ctx, iterable, nullptr, ctx->newList(), nullptr);
    if (!it) return PROTO_NONE;

    for (long long i = 0; i < start; ++i) {
        const proto::ProtoObject* nextM = it->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__next__"));
        if (!nextM || !nextM->asMethod(ctx)) break;
        const proto::ProtoObject* v = nextM->asMethod(ctx)(ctx, it, nullptr, ctx->newList(), nullptr);
        if (!v || v == PROTO_NONE) break;
    }

    const proto::ProtoObject* proto = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__islice_proto__"));
    if (!proto) return PROTO_NONE;
    const proto::ProtoObject* sl = proto->newChild(ctx, true);
    sl = sl->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__islice_it__"), it);
    sl = sl->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__islice_stop__"), ctx->fromInteger(stop));
    sl = sl->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__islice_idx__"), ctx->fromInteger(start));
    return sl;
}

static const proto::ProtoObject* py_chain_next(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* itersObj = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__chain_iters__"));
    const proto::ProtoObject* idxObj = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__chain_idx__"));
    if (!itersObj || !itersObj->asList(ctx) || !idxObj || !idxObj->isInteger(ctx)) return PROTO_NONE;
    const proto::ProtoList* iters = itersObj->asList(ctx);
    long long idx = idxObj->asLong(ctx);
    unsigned long n = iters->getSize(ctx);
    while (static_cast<unsigned long>(idx) < n) {
        const proto::ProtoObject* it = iters->getAt(ctx, static_cast<int>(idx));
        const proto::ProtoObject* nextM = it ? it->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__next__")) : nullptr;
        if (!nextM || !nextM->asMethod(ctx)) {
            idx++;
            self->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__chain_idx__"), ctx->fromInteger(idx));
            continue;
        }
        const proto::ProtoObject* val = nextM->asMethod(ctx)(ctx, it, nullptr, ctx->newList(), nullptr);
        if (val && val != PROTO_NONE) return val;
        idx++;
        self->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__chain_idx__"), ctx->fromInteger(idx));
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_repeat_next(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* obj = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__repeat_obj__"));
    const proto::ProtoObject* timesObj = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__repeat_times__"));
    const proto::ProtoObject* countObj = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__repeat_count__"));
    if (!obj || !timesObj || !countObj) return PROTO_NONE;
    if (timesObj != PROTO_NONE && timesObj->isInteger(ctx)) {
        long long times = timesObj->asLong(ctx);
        long long count = countObj->isInteger(ctx) ? countObj->asLong(ctx) : 0;
        if (count >= times) return PROTO_NONE;
        self->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__repeat_count__"), ctx->fromInteger(count + 1));
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

    const proto::ProtoObject* proto = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__repeat_proto__"));
    if (!proto) return PROTO_NONE;
    const proto::ProtoObject* r = proto->newChild(ctx, true);
    r = r->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__repeat_obj__"), obj);
    r = r->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__repeat_times__"), times ? times : PROTO_NONE);
    r = r->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__repeat_count__"), ctx->fromInteger(0));
    return r;
}

static const proto::ProtoObject* py_cycle_next(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* it = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__cycle_it__"));
    const proto::ProtoObject* cacheObj = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__cycle_cache__"));
    const proto::ProtoObject* idxObj = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__cycle_idx__"));
    if (!it || !cacheObj || !cacheObj->asList(ctx) || !idxObj) return PROTO_NONE;
    const proto::ProtoList* cache = cacheObj->asList(ctx);
    long long idx = idxObj->asLong(ctx);
    if (idx < static_cast<long long>(cache->getSize(ctx))) {
        const proto::ProtoObject* val = cache->getAt(ctx, static_cast<int>(idx));
        self->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__cycle_idx__"), ctx->fromInteger(idx + 1));
        return val;
    }
    const proto::ProtoObject* nextM = it->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__next__"));
    if (!nextM || !nextM->asMethod(ctx)) return PROTO_NONE;
    const proto::ProtoObject* val = nextM->asMethod(ctx)(ctx, it, nullptr, ctx->newList(), nullptr);
    if (!val || val == PROTO_NONE) {
        if (cache->getSize(ctx) == 0) return PROTO_NONE;
        self->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__cycle_idx__"), ctx->fromInteger(1));
        return cache->getAt(ctx, 0);
    }
    const proto::ProtoList* newCache = cache->appendLast(ctx, val);
    self->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__cycle_cache__"), newCache->asObject(ctx));
    self->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__cycle_idx__"), ctx->fromInteger(newCache->getSize(ctx)));
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
    const proto::ProtoObject* iterM = iterable->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter__"));
    if (!iterM || !iterM->asMethod(ctx)) return PROTO_NONE;
    const proto::ProtoObject* it = iterM->asMethod(ctx)(ctx, iterable, nullptr, ctx->newList(), nullptr);
    if (!it) return PROTO_NONE;
    const proto::ProtoObject* proto = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__cycle_proto__"));
    if (!proto) return PROTO_NONE;
    const proto::ProtoObject* c = proto->newChild(ctx, true);
    c = c->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__cycle_it__"), it);
    c = c->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__cycle_cache__"), ctx->newList()->asObject(ctx));
    c = c->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__cycle_idx__"), ctx->fromInteger(0));
    return c;
}

static const proto::ProtoObject* py_takewhile_next(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* pred = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__takewhile_pred__"));
    const proto::ProtoObject* it = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__takewhile_it__"));
    if (!pred || !pred->asMethod(ctx) || !it) return PROTO_NONE;
    const proto::ProtoObject* nextM = it->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__next__"));
    if (!nextM || !nextM->asMethod(ctx)) return PROTO_NONE;
    const proto::ProtoObject* val = nextM->asMethod(ctx)(ctx, it, nullptr, ctx->newList(), nullptr);
    if (!val || val == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoList* predArgs = ctx->newList()->appendLast(ctx, val);
    const proto::ProtoObject* predResult = pred->asMethod(ctx)(ctx, pred, nullptr, predArgs, nullptr);
    bool ok = (predResult && predResult != PROTO_NONE && predResult != PROTO_FALSE);
    if (predResult && predResult->isInteger(ctx) && predResult->asLong(ctx) != 0) ok = true;
    if (!ok) return PROTO_NONE;
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
    const proto::ProtoObject* iterM = iterable->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter__"));
    if (!iterM || !iterM->asMethod(ctx)) return PROTO_NONE;
    const proto::ProtoObject* it = iterM->asMethod(ctx)(ctx, iterable, nullptr, ctx->newList(), nullptr);
    if (!it) return PROTO_NONE;
    const proto::ProtoObject* proto = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__takewhile_proto__"));
    if (!proto) return PROTO_NONE;
    const proto::ProtoObject* tw = proto->newChild(ctx, true);
    tw = tw->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__takewhile_pred__"), pred);
    tw = tw->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__takewhile_it__"), it);
    return tw;
}

static const proto::ProtoObject* py_dropwhile_next(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* pred = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__dropwhile_pred__"));
    const proto::ProtoObject* it = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__dropwhile_it__"));
    if (!pred || !pred->asMethod(ctx) || !it) return PROTO_NONE;
    const proto::ProtoObject* nextM = it->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__next__"));
    if (!nextM || !nextM->asMethod(ctx)) return PROTO_NONE;
    for (;;) {
        const proto::ProtoObject* val = nextM->asMethod(ctx)(ctx, it, nullptr, ctx->newList(), nullptr);
        if (!val || val == PROTO_NONE) return PROTO_NONE;
        const proto::ProtoObject* dropObj = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__dropwhile_dropping__"));
        if (dropObj == PROTO_NONE || !dropObj->isInteger(ctx) || dropObj->asLong(ctx) == 0)
            return val;
        const proto::ProtoList* predArgs = ctx->newList()->appendLast(ctx, val);
        const proto::ProtoObject* predResult = pred->asMethod(ctx)(ctx, pred, nullptr, predArgs, nullptr);
        bool predTrue = (predResult && predResult != PROTO_NONE && predResult != PROTO_FALSE);
        if (predResult && predResult->isInteger(ctx) && predResult->asLong(ctx) != 0) predTrue = true;
        if (!predTrue) {
            self->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__dropwhile_dropping__"), ctx->fromInteger(0));
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
    const proto::ProtoObject* iterM = iterable->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter__"));
    if (!iterM || !iterM->asMethod(ctx)) return PROTO_NONE;
    const proto::ProtoObject* it = iterM->asMethod(ctx)(ctx, iterable, nullptr, ctx->newList(), nullptr);
    if (!it) return PROTO_NONE;
    const proto::ProtoObject* proto = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__dropwhile_proto__"));
    if (!proto) return PROTO_NONE;
    const proto::ProtoObject* dw = proto->newChild(ctx, true);
    dw = dw->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__dropwhile_pred__"), pred);
    dw = dw->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__dropwhile_it__"), it);
    dw = dw->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__dropwhile_dropping__"), ctx->fromInteger(1));
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
    const proto::ProtoObject* iterM = iterable->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter__"));
    if (!iterM || !iterM->asMethod(ctx)) return PROTO_NONE;
    const proto::ProtoObject* it1 = iterM->asMethod(ctx)(ctx, iterable, nullptr, ctx->newList(), nullptr);
    if (!it1) return PROTO_NONE;
    const proto::ProtoObject* it2 = iterM->asMethod(ctx)(ctx, iterable, nullptr, ctx->newList(), nullptr);
    if (!it2) return PROTO_NONE;
    const proto::ProtoList* pair = ctx->newList()->appendLast(ctx, it1)->appendLast(ctx, it2);
    const proto::ProtoTuple* tup = ctx->newTupleFromList(pair);
    return tup ? tup->asObject(ctx) : PROTO_NONE;
}

static const proto::ProtoObject* py_accumulate_stub(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    (void)self;
    (void)posArgs;
    return PROTO_NONE;
}

static const proto::ProtoObject* py_groupby_stub(
    proto::ProtoContext* ctx, const proto::ProtoObject* self,
    const proto::ParentLink*, const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    (void)self;
    (void)posArgs;
    return PROTO_NONE;
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
        const proto::ProtoObject* itAttr = iterable->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter__"));
        if (!itAttr || !itAttr->asMethod(ctx)) continue;
        const proto::ProtoObject* it = itAttr->asMethod(ctx)(ctx, iterable, nullptr, ctx->newList(), nullptr);
        if (it) iters = iters->appendLast(ctx, it);
    }
    const proto::ProtoObject* proto = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__chain_proto__"));
    if (!proto) return PROTO_NONE;
    const proto::ProtoObject* ch = proto->newChild(ctx, true);
    ch = ch->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__chain_iters__"), iters->asObject(ctx));
    ch = ch->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__chain_idx__"), ctx->fromInteger(0));
    return ch;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(true);

    const proto::ProtoObject* countProto = ctx->newObject(true);
    countProto = countProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(countProto), py_iter_self));
    countProto = countProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__next__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(countProto), py_count_next));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__count_proto__"), countProto);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "count"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_count));

    const proto::ProtoObject* isliceProto = ctx->newObject(true);
    isliceProto = isliceProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(isliceProto), py_iter_self));
    isliceProto = isliceProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__next__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(isliceProto), py_islice_next));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__islice_proto__"), isliceProto);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "islice"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_islice));

    const proto::ProtoObject* chainProto = ctx->newObject(true);
    chainProto = chainProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(chainProto), py_iter_self));
    chainProto = chainProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__next__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(chainProto), py_chain_next));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__chain_proto__"), chainProto);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "chain"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_chain));

    const proto::ProtoObject* repeatProto = ctx->newObject(true);
    repeatProto = repeatProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(repeatProto), py_iter_self));
    repeatProto = repeatProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__next__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(repeatProto), py_repeat_next));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__repeat_proto__"), repeatProto);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "repeat"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_repeat));

    const proto::ProtoObject* cycleProto = ctx->newObject(true);
    cycleProto = cycleProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(cycleProto), py_iter_self));
    cycleProto = cycleProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__next__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(cycleProto), py_cycle_next));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__cycle_proto__"), cycleProto);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "cycle"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_cycle));

    const proto::ProtoObject* takewhileProto = ctx->newObject(true);
    takewhileProto = takewhileProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(takewhileProto), py_iter_self));
    takewhileProto = takewhileProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__next__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(takewhileProto), py_takewhile_next));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__takewhile_proto__"), takewhileProto);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "takewhile"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_takewhile));

    const proto::ProtoObject* dropwhileProto = ctx->newObject(true);
    dropwhileProto = dropwhileProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__iter__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(dropwhileProto), py_iter_self));
    dropwhileProto = dropwhileProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__next__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(dropwhileProto), py_dropwhile_next));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__dropwhile_proto__"), dropwhileProto);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "dropwhile"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_dropwhile));

    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "tee"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_tee));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "accumulate"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_accumulate_stub));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "groupby"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_groupby_stub));

    return mod;
}

} // namespace itertools
} // namespace protoPython
