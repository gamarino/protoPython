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

    return mod;
}

} // namespace itertools
} // namespace protoPython
