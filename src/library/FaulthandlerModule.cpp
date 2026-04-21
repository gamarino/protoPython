#include <protoPython/FaulthandlerModule.h>
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>

namespace protoPython {
namespace faulthandler {

static const proto::ProtoObject* py_enable(
    proto::ProtoContext* ctx, const proto::ProtoObject* /*self*/, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return PROTO_NONE;
}

static const proto::ProtoObject* py_disable(
    proto::ProtoContext* ctx, const proto::ProtoObject* /*self*/, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return PROTO_NONE;
}

static const proto::ProtoObject* py_is_enabled(
    proto::ProtoContext* ctx, const proto::ProtoObject* /*self*/, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return PROTO_FALSE;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(false);
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "enable"), ctx->fromMethod(nullptr, py_enable));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "disable"), ctx->fromMethod(nullptr, py_disable));
    mod = mod->setAttribute(ctx, PythonEnvironment::getInternedString(ctx, "is_enabled"), ctx->fromMethod(nullptr, py_is_enabled));
    return mod;
}

} // namespace faulthandler
} // namespace protoPython
