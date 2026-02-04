#include <protoPython/FunctoolsModule.h>

namespace protoPython {
namespace functools {

static const proto::ProtoObject* py_partial_call(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    const proto::ProtoObject* func = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__partial_func__"));
    const proto::ProtoObject* frozenObj = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__partial_args__"));
    if (!func) return PROTO_NONE;

    const proto::ProtoList* args = ctx->newList();
    if (frozenObj && frozenObj->asList(ctx)) {
        const proto::ProtoList* frozen = frozenObj->asList(ctx);
        for (unsigned long i = 0; i < frozen->getSize(ctx); ++i)
            args = args->appendLast(ctx, frozen->getAt(ctx, static_cast<int>(i)));
    }
    for (unsigned long i = 0; i < posArgs->getSize(ctx); ++i)
        args = args->appendLast(ctx, posArgs->getAt(ctx, static_cast<int>(i)));

    const proto::ProtoObject* callAttr = func->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__call__"));
    if (!callAttr || !callAttr->asMethod(ctx)) return PROTO_NONE;
    return callAttr->asMethod(ctx)(ctx, func, nullptr, args, nullptr);
}

static const proto::ProtoObject* py_partial(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* func = posArgs->getAt(ctx, 0);
    const proto::ProtoList* frozen = ctx->newList();
    for (unsigned long i = 1; i < posArgs->getSize(ctx); ++i)
        frozen = frozen->appendLast(ctx, posArgs->getAt(ctx, static_cast<int>(i)));

    const proto::ProtoObject* partialProto = self->getAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__partial_proto__"));
    if (!partialProto) return PROTO_NONE;
    const proto::ProtoObject* p = partialProto->newChild(ctx, true);
    p = p->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__partial_func__"), func);
    p = p->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__partial_args__"), frozen->asObject(ctx));
    return p;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(true);
    const proto::ProtoObject* partialProto = ctx->newObject(true);
    partialProto = partialProto->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__call__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(partialProto), py_partial_call));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__partial_proto__"), partialProto);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "partial"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_partial));
    return mod;
}

} // namespace functools
} // namespace protoPython
