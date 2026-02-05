#include <protoPython/AtexitModule.h>

namespace protoPython {
namespace atexit_module {

static const proto::ProtoObject* py_register(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    (void)posArgs;
    return PROTO_NONE;
}

static const proto::ProtoObject* py_unregister(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    (void)posArgs;
    return PROTO_NONE;
}

static const proto::ProtoObject* py_run_exitfuncs(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    (void)posArgs;
    return PROTO_NONE;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(true);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "register"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_register));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "unregister"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_unregister));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "_run_exitfuncs"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_run_exitfuncs));
    return mod;
}

} // namespace atexit_module
} // namespace protoPython
