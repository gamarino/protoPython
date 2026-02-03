#include <protoPython/IOModule.h>
#include <cstdio>
#include <sstream>
#include <string>

namespace protoPython {
namespace io {

static void file_buffer_finalizer(void* ptr) {
    delete static_cast<std::string*>(ptr);
}

static const proto::ProtoObject* py_io_read(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    const proto::ProtoObject* bufObj = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__file_buffer__"));
    if (!bufObj || !bufObj->asExternalPointer(context)) return context->fromUTF8String("");
    std::string* buffer = static_cast<std::string*>(bufObj->asExternalPointer(context)->getPointer(context));
    if (!buffer) return context->fromUTF8String("");
    long long n = -1;
    if (posArgs->getSize(context) > 0 && posArgs->getAt(context, 0)->isInteger(context))
        n = posArgs->getAt(context, 0)->asLong(context);
    std::string result;
    if (n < 0) {
        result = *buffer;
        buffer->clear();
    } else {
        size_t take = static_cast<size_t>(n);
        if (take > buffer->size()) take = buffer->size();
        result = buffer->substr(0, take);
        buffer->erase(0, take);
    }
    return context->fromUTF8String(result.c_str());
}

static const proto::ProtoObject* py_io_write(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (posArgs->getSize(context) < 1) return context->fromInteger(0);
    const proto::ProtoObject* bufObj = self->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__file_buffer__"));
    if (!bufObj || !bufObj->asExternalPointer(context)) return context->fromInteger(0);
    std::string* buffer = static_cast<std::string*>(bufObj->asExternalPointer(context)->getPointer(context));
    if (!buffer) return context->fromInteger(0);
    const proto::ProtoObject* data = posArgs->getAt(context, 0);
    std::string s;
    if (data->isString(context)) data->asString(context)->toUTF8String(context, s);
    buffer->append(s);
    return context->fromInteger(static_cast<long long>(s.size()));
}

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
    std::string* buffer = new std::string();
    fileObj = fileObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__file_buffer__"),
        context->fromExternalPointer(buffer, file_buffer_finalizer));
    fileObj = fileObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "read"),
        context->fromMethod(const_cast<proto::ProtoObject*>(fileObj), py_io_read));
    fileObj = fileObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "write"),
        context->fromMethod(const_cast<proto::ProtoObject*>(fileObj), py_io_write));
    return fileObj;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* ioMod = ctx->newObject(true);
    
    ioMod = ioMod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "open"), ctx->fromMethod(const_cast<proto::ProtoObject*>(ioMod), py_io_open));
    
    return ioMod;
}

} // namespace io
} // namespace protoPython
