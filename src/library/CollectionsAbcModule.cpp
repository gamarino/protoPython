#include <protoPython/CollectionsAbcModule.h>

namespace protoPython {
namespace collections_abc {

/** Minimal __call__ for ABC stub: return new child (for isinstance/callable use). */
static const proto::ProtoObject* py_abc_call(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    return self->newChild(ctx, true);
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* iterableType = ctx->newObject(true);
    iterableType = iterableType->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__call__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(iterableType), py_abc_call));

    const proto::ProtoObject* sequenceType = ctx->newObject(true);
    sequenceType = sequenceType->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "__call__"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(sequenceType), py_abc_call));

    const proto::ProtoObject* mod = ctx->newObject(true);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "Iterable"), iterableType);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "Sequence"), sequenceType);
    return mod;
}

} // namespace collections_abc
} // namespace protoPython
