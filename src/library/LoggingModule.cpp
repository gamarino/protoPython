#include <protoPython/PythonEnvironment.h>
#include <protoPython/LoggingModule.h>

namespace protoPython {
namespace logging {

static const proto::ProtoObject* py_basicConfig(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return PROTO_NONE;
}

static const proto::ProtoObject* py_logger_info(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return PROTO_NONE;
}

static const proto::ProtoObject* py_getLogger(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    const proto::ProtoObject* logger = ctx->newObject(false);
    logger = logger->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "name"),
        posArgs->getSize(ctx) > 0 ? posArgs->getAt(ctx, 0) : PythonEnvironment::getInternedString(ctx, "root")->asObject(ctx));
    logger = logger->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "info"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(logger), py_logger_info));
    return logger;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(false);
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "basicConfig"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_basicConfig));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "getLogger"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_getLogger));
    return mod;
}

} // namespace logging
} // namespace protoPython
