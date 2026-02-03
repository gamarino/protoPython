#include <protoPython/IOModule.h>
#include <cstdio>
#include <string>

namespace protoPython {
namespace io {

static const proto::ProtoObject* py_io_open(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;
    
    const proto::ProtoObject* fileArg = positionalParameters->getAt(context, 0);
    if (!fileArg->isString(context)) return PROTO_NONE;
    
    std::string filename;
    fileArg->asString(context)->toUTF8String(context, filename);

    std::string mode = "r";
    if (positionalParameters->getSize(context) >= 2 && positionalParameters->getAt(context, 1)->isString(context)) {
        positionalParameters->getAt(context, 1)->asString(context)->toUTF8String(context, mode);
    }

    const proto::ProtoObject* fileObj = context->newObject(true);
    fileObj = fileObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "name"), fileArg);
    fileObj = fileObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "mode"), context->fromUTF8String(mode.c_str()));
    fileObj = fileObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "buffering"), context->fromInteger(-1));
    return fileObj;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* ioMod = ctx->newObject(true);
    
    ioMod = ioMod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "open"), ctx->fromMethod(const_cast<proto::ProtoObject*>(ioMod), py_io_open));
    
    return ioMod;
}

} // namespace io
} // namespace protoPython
