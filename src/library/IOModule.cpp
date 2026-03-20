#include <protoPython/IOModule.h>
#include <cstdio>
#include <sstream>
#include <string>
#include <iostream>

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

    const proto::ProtoObject* fileObj = context->newObject(false);
    fileObj = fileObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "name"), fileArg);
    fileObj = fileObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "mode"), context->fromUTF8String(mode.c_str()));
    fileObj = fileObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "buffering"), context->fromInteger(-1));
    std::string* buffer = new std::string();
    if (mode.find('r') != std::string::npos) {
        FILE* f = fopen(filename.c_str(), "rb");
        if (f) {
            fseek(f, 0, SEEK_END);
            long size = ftell(f);
            fseek(f, 0, SEEK_SET);
            if (size > 0) {
                buffer->resize(size);
                fread(&(*buffer)[0], 1, size, f);
            }
            fclose(f);
        }
    }
    fileObj = fileObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__file_buffer__"),
        context->fromExternalPointer(buffer, file_buffer_finalizer));
    fileObj = fileObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "read"),
        context->fromMethod(const_cast<proto::ProtoObject*>(fileObj), py_io_read));
    fileObj = fileObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "write"),
        context->fromMethod(const_cast<proto::ProtoObject*>(fileObj), py_io_write));
    return fileObj;
}

static const proto::ProtoObject* py_io_register(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    // Dummy register method for ABCs
    return self;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* ioMod = ctx->newObject(false);
    
    ioMod = ioMod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "open"), ctx->fromMethod(const_cast<proto::ProtoObject*>(ioMod), py_io_open));
    ioMod = ioMod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "open_code"), ctx->fromMethod(const_cast<proto::ProtoObject*>(ioMod), py_io_open));
    ioMod = ioMod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "DEFAULT_BUFFER_SIZE"), ctx->fromInteger(8192));
    
    // Stubs for io.py requirements
    const proto::ProtoString* py_name_s = proto::ProtoString::fromUTF8String(ctx, "__name__");
    const proto::ProtoString* py_doc_s = proto::ProtoString::fromUTF8String(ctx, "__doc__");
    const proto::ProtoString* py_module_s = proto::ProtoString::fromUTF8String(ctx, "__module__");
    const proto::ProtoObject* py_io_s = ctx->fromUTF8String("_io");
    const proto::ProtoObject* py_empty_doc = ctx->fromUTF8String("");

    auto add_stub = [&](const char* name) {
        const proto::ProtoString* nameS = proto::ProtoString::fromUTF8String(ctx, name);
        const proto::ProtoObject* stub = ctx->newObject(false);
        stub = stub->setAttribute(ctx, py_name_s, ctx->fromUTF8String(name));
        stub = stub->setAttribute(ctx, py_doc_s, py_empty_doc);
        stub = stub->setAttribute(ctx, py_module_s, py_io_s);
        stub = stub->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "register"),
            ctx->fromMethod(const_cast<proto::ProtoObject*>(stub), py_io_register));
        
        ioMod = ioMod->setAttribute(ctx, nameS, stub);
    };

    add_stub("BlockingIOError");
    add_stub("UnsupportedOperation");
    add_stub("FileIO");
    add_stub("BytesIO");
    add_stub("StringIO");
    add_stub("BufferedReader");
    add_stub("BufferedWriter");
    add_stub("BufferedRWPair");
    add_stub("BufferedRandom");
    add_stub("IncrementalNewlineDecoder");
    add_stub("text_encoding");
    add_stub("TextIOWrapper");
    add_stub("_IOBase");
    add_stub("_RawIOBase");
    add_stub("_BufferedIOBase");
    add_stub("_TextIOBase");
    add_stub("_WindowsConsoleIO");

    return ioMod;
}

} // namespace io
} // namespace protoPython
