#include <protoPython/ContextvarsModule.h>
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>

namespace protoPython {
namespace contextvars {

static const proto::ProtoObject* cv_get(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*, const proto::ProtoList* pos, const proto::ProtoSparseList*) {
    // Basic getter: if a default is provided in pos, return it.
    if (pos && pos->getSize(ctx) > 0) {
        return pos->getAt(ctx, 0);
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* cv_set(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*, const proto::ProtoList* pos, const proto::ProtoSparseList*) {
    // Placeholder setter
    return PROTO_NONE;
}

static const proto::ProtoObject* cv_call(proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*, const proto::ProtoList* pos, const proto::ProtoSparseList*) {
    const proto::ProtoObject* inst = ctx->newObject(false);
    // self is the ContextVar prototype
    inst = inst->addParent(ctx, self);
    return inst;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(false);
    
    // ContextVar "class"
    const proto::ProtoObject* contextVarProto = ctx->newObject(false);
    contextVarProto = contextVarProto->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "get"), 
        ctx->fromMethod(const_cast<proto::ProtoObject*>(contextVarProto), cv_get));
    contextVarProto = contextVarProto->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "set"), 
        ctx->fromMethod(const_cast<proto::ProtoObject*>(contextVarProto), cv_set));
    
    // To make ContextVar('name') work, the object must be callable
    contextVarProto = contextVarProto->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__call__"), 
        ctx->fromMethod(const_cast<proto::ProtoObject*>(contextVarProto), cv_call));
    // Also satisfy potential checks for type
    contextVarProto = contextVarProto->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__name__"), PythonEnvironment::getInternedString(ctx, "ContextVar"));

    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "ContextVar"), contextVarProto);
    
    return mod;
}

} // namespace contextvars
} // namespace protoPython
