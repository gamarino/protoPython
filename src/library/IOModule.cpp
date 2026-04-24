#include <protoPython/PythonEnvironment.h>
#include <protoPython/IOModule.h>
#include <protoPython/DiagUtils.h>
#include <cstdio>
#include <sstream>
#include <string>
#include <iostream>
#include <fstream>

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
    const proto::ProtoObject* bufObj = self->getAttribute(context, proto::ProtoString::createSymbol(context, "__file_buffer__"));
    if (!bufObj || !bufObj->asExternalPointer(context)) return PythonEnvironment::getInternedString(context, "")->asObject(context);
    std::string* buffer = static_cast<std::string*>(bufObj->asExternalPointer(context)->getPointer(context));
    if (!buffer) return PythonEnvironment::getInternedString(context, "")->asObject(context);
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
    return PythonEnvironment::getInternedString(context, result.c_str())->asObject(context);
}

static const proto::ProtoObject* py_io_close(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    // Clear the buffer to simulate close
    const proto::ProtoObject* bufObj = self->getAttribute(context, proto::ProtoString::createSymbol(context, "__file_buffer__"));
    if (bufObj && bufObj->asExternalPointer(context)) {
        std::string* buffer = static_cast<std::string*>(bufObj->asExternalPointer(context)->getPointer(context));
        if (buffer) buffer->clear();
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_io_enter(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    return self;
}

static const proto::ProtoObject* py_io_exit(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    // Close the file and suppress no exceptions
    py_io_close(context, self, nullptr, posArgs, nullptr);
    return PROTO_FALSE;
}

static const proto::ProtoObject* py_io_readlines(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    const proto::ProtoObject* bufObj = self->getAttribute(context, proto::ProtoString::createSymbol(context, "__file_buffer__"));
    if (!bufObj || !bufObj->asExternalPointer(context)) return context->newList()->asObject(context);
    std::string* buffer = static_cast<std::string*>(bufObj->asExternalPointer(context)->getPointer(context));
    if (!buffer) return context->newList()->asObject(context);
    const proto::ProtoList* lines = context->newList();
    std::string content = *buffer;
    size_t pos = 0;
    while (pos < content.size()) {
        size_t nl = content.find('\n', pos);
        std::string line;
        if (nl == std::string::npos) {
            line = content.substr(pos);
            pos = content.size();
        } else {
            line = content.substr(pos, nl - pos + 1);
            pos = nl + 1;
        }
        lines = lines->appendLast(context, PythonEnvironment::getInternedString(context, line.c_str())->asObject(context));
    }
    buffer->clear();
    return lines->asObject(context);
}

static const proto::ProtoObject* py_io_write(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (posArgs->getSize(context) < 1) return context->fromInteger(0);
    const proto::ProtoObject* bufObj = self->getAttribute(context, proto::ProtoString::createSymbol(context, "__file_buffer__"));
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
    fileObj = fileObj->setAttribute(context, proto::ProtoString::createSymbol(context, "name"), fileArg);
    fileObj = fileObj->setAttribute(context, proto::ProtoString::createSymbol(context, "mode"), PythonEnvironment::getInternedString(context, mode.c_str())->asObject(context));
    fileObj = fileObj->setAttribute(context, proto::ProtoString::createSymbol(context, "buffering"), context->fromInteger(-1));
    std::string* buffer = new std::string();
    if (mode.find('r') != std::string::npos) {
        std::ifstream f(filename);
        if (f) {
            std::stringstream ss;
            ss << f.rdbuf();
            *buffer = ss.str();
        }
    }
    fileObj = fileObj->setAttribute(context, proto::ProtoString::createSymbol(context, "__file_buffer__"),
        context->fromExternalPointer(buffer, file_buffer_finalizer));
    fileObj = fileObj->setAttribute(context, proto::ProtoString::createSymbol(context, "read"),
        context->fromMethod(const_cast<proto::ProtoObject*>(fileObj), py_io_read));
    fileObj = fileObj->setAttribute(context, proto::ProtoString::createSymbol(context, "readlines"),
        context->fromMethod(const_cast<proto::ProtoObject*>(fileObj), py_io_readlines));
    fileObj = fileObj->setAttribute(context, proto::ProtoString::createSymbol(context, "write"),
        context->fromMethod(const_cast<proto::ProtoObject*>(fileObj), py_io_write));
    fileObj = fileObj->setAttribute(context, proto::ProtoString::createSymbol(context, "close"),
        context->fromMethod(const_cast<proto::ProtoObject*>(fileObj), py_io_close));
    fileObj = fileObj->setAttribute(context, proto::ProtoString::createSymbol(context, "__enter__"),
        context->fromMethod(const_cast<proto::ProtoObject*>(fileObj), py_io_enter));
    fileObj = fileObj->setAttribute(context, proto::ProtoString::createSymbol(context, "__exit__"),
        context->fromMethod(const_cast<proto::ProtoObject*>(fileObj), py_io_exit));
    return fileObj;
}

static const proto::ProtoObject* py_io_register(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    // Dummy register method for ABCs
    return self;
}

// ----- StringIO -------------------------------------------------------------

static const proto::ProtoString* k_sio_buf(proto::ProtoContext* c) {
    return proto::ProtoString::createSymbol(c, "__sio_buffer__");
}
static const proto::ProtoString* k_sio_pos(proto::ProtoContext* c) {
    return proto::ProtoString::createSymbol(c, "__sio_pos__");
}

static std::string sio_get_buf(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    const proto::ProtoObject* bufObj = self->getAttribute(ctx, k_sio_buf(ctx));
    if (!bufObj || !bufObj->isString(ctx)) return std::string();
    std::string s;
    bufObj->asString(ctx)->toUTF8String(ctx, s);
    return s;
}

static long sio_get_pos(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    const proto::ProtoObject* p = self->getAttribute(ctx, k_sio_pos(ctx));
    return (p && p->isInteger(ctx)) ? static_cast<long>(p->asLong(ctx)) : 0;
}

static const proto::ProtoObject* sio_set_state(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* self,
                                               const std::string& buf,
                                               long pos) {
    self = self->setAttribute(ctx, k_sio_buf(ctx),
                              PythonEnvironment::getInternedString(ctx, buf.c_str())->asObject(ctx));
    self = self->setAttribute(ctx, k_sio_pos(ctx), ctx->fromInteger(pos));
    return self;
}

static const proto::ProtoObject* py_sio_write(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1) return ctx->fromInteger(0);
    const proto::ProtoObject* data = args->getAt(ctx, 0);
    std::string text;
    if (data && data->isString(ctx)) data->asString(ctx)->toUTF8String(ctx, text);
    else if (data) {
        const proto::ProtoObject* d = data->getAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "__data__"));
        if (d && d->isString(ctx)) d->asString(ctx)->toUTF8String(ctx, text);
    }
    std::string buf = sio_get_buf(ctx, self);
    long pos = sio_get_pos(ctx, self);
    if (pos < 0) pos = 0;
    if (static_cast<size_t>(pos) > buf.size()) buf.append(static_cast<size_t>(pos) - buf.size(), '\0');
    // Overwrite starting at pos, extending buffer if necessary.
    size_t end = static_cast<size_t>(pos) + text.size();
    if (end > buf.size()) buf.resize(end, '\0');
    for (size_t i = 0; i < text.size(); ++i) buf[pos + i] = text[i];
    sio_set_state(ctx, self, buf, static_cast<long>(pos + text.size()));
    return ctx->fromInteger(static_cast<long>(text.size()));
}

static const proto::ProtoObject* py_sio_getvalue(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    std::string buf = sio_get_buf(ctx, self);
    return PythonEnvironment::getInternedString(ctx, buf.c_str())->asObject(ctx);
}

static const proto::ProtoObject* py_sio_read(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    std::string buf = sio_get_buf(ctx, self);
    long pos = sio_get_pos(ctx, self);
    if (pos < 0) pos = 0;
    size_t size = buf.size();
    long want = -1;
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a = args->getAt(ctx, 0);
        if (a && a->isInteger(ctx)) want = static_cast<long>(a->asLong(ctx));
    }
    size_t take;
    if (want < 0) take = (static_cast<size_t>(pos) < size) ? size - pos : 0;
    else take = std::min<size_t>(size - std::min<size_t>(pos, size), static_cast<size_t>(want));
    std::string out = (pos < static_cast<long>(size)) ? buf.substr(pos, take) : std::string();
    sio_set_state(ctx, self, buf, pos + static_cast<long>(out.size()));
    return PythonEnvironment::getInternedString(ctx, out.c_str())->asObject(ctx);
}

static const proto::ProtoObject* py_sio_seek(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    long off = 0;
    long whence = 0;
    if (args && args->getSize(ctx) > 0 && args->getAt(ctx, 0)->isInteger(ctx))
        off = static_cast<long>(args->getAt(ctx, 0)->asLong(ctx));
    if (args && args->getSize(ctx) > 1 && args->getAt(ctx, 1)->isInteger(ctx))
        whence = static_cast<long>(args->getAt(ctx, 1)->asLong(ctx));
    std::string buf = sio_get_buf(ctx, self);
    long pos = sio_get_pos(ctx, self);
    long newPos = pos;
    if (whence == 0) newPos = off;
    else if (whence == 1) newPos = pos + off;
    else if (whence == 2) newPos = static_cast<long>(buf.size()) + off;
    if (newPos < 0) newPos = 0;
    sio_set_state(ctx, self, buf, newPos);
    return ctx->fromInteger(newPos);
}

static const proto::ProtoObject* py_sio_tell(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return ctx->fromInteger(sio_get_pos(ctx, self));
}

static const proto::ProtoObject* py_sio_truncate(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    std::string buf = sio_get_buf(ctx, self);
    long pos = sio_get_pos(ctx, self);
    long size = pos;
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a = args->getAt(ctx, 0);
        if (a && a->isInteger(ctx)) size = static_cast<long>(a->asLong(ctx));
    }
    if (size < 0) size = 0;
    if (static_cast<size_t>(size) < buf.size()) buf.resize(size);
    sio_set_state(ctx, self, buf, pos);
    return ctx->fromInteger(static_cast<long>(buf.size()));
}

static const proto::ProtoObject* py_sio_close(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    sio_set_state(ctx, self, std::string(), 0);
    return PROTO_NONE;
}

static const proto::ProtoObject* py_sio_writable(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return PROTO_TRUE;
}

static const proto::ProtoObject* py_sio_readable(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return PROTO_TRUE;
}

static const proto::ProtoObject* py_sio_enter(
    proto::ProtoContext*, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return self;
}

static const proto::ProtoObject* py_sio_exit(
    proto::ProtoContext*, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return PROTO_FALSE;
}

// StringIO(initial_value='', newline='\n') — returns a fresh instance with
// `initial_value` preloaded, position at 0.  Called when the type itself is
// invoked as a constructor (e.g. `StringIO()`).
static const proto::ProtoObject* py_sio_call(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    // `self` here is the class (the stub).  Create an instance that
    // inherits from it so `type(obj) is StringIO` remains True.
    proto::ProtoObject* inst = const_cast<proto::ProtoObject*>(self->newChild(ctx, true));
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    inst = const_cast<proto::ProtoObject*>(inst->setAttribute(ctx,
        env ? env->getClassString() : PythonEnvironment::getInternalString(ctx, "__class__"), self));
    std::string initial;
    unsigned long n = args ? args->getSize(ctx) : 0;
    for (unsigned long i = 0; i < n; ++i) {
        const proto::ProtoObject* a = args->getAt(ctx, static_cast<int>(i));
        if (!a || a == self) continue;
        if (a->isString(ctx)) {
            a->asString(ctx)->toUTF8String(ctx, initial);
            break;
        }
        const proto::ProtoObject* d = a->getAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "__data__"));
        if (d && d->isString(ctx)) {
            d->asString(ctx)->toUTF8String(ctx, initial);
            break;
        }
    }
    inst = const_cast<proto::ProtoObject*>(inst->setAttribute(ctx, k_sio_buf(ctx),
        PythonEnvironment::getInternedString(ctx, initial.c_str())->asObject(ctx)));
    inst = const_cast<proto::ProtoObject*>(inst->setAttribute(ctx, k_sio_pos(ctx), ctx->fromInteger(0)));
    return inst;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* ioMod = ctx->newObject(false);
    
    ioMod = ioMod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "open"), ctx->fromMethod(const_cast<proto::ProtoObject*>(ioMod), py_io_open));
    ioMod = ioMod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "open_code"), ctx->fromMethod(const_cast<proto::ProtoObject*>(ioMod), py_io_open));
    ioMod = ioMod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "DEFAULT_BUFFER_SIZE"), ctx->fromInteger(8192));
    
    // Stubs for io.py requirements
    const proto::ProtoString* py_name_s = proto::ProtoString::createSymbol(ctx, "__name__");
    const proto::ProtoString* py_doc_s = proto::ProtoString::createSymbol(ctx, "__doc__");
    const proto::ProtoString* py_module_s = proto::ProtoString::createSymbol(ctx, "__module__");
    const proto::ProtoObject* py_io_s = PythonEnvironment::getInternedString(ctx, "_io")->asObject(ctx);
    const proto::ProtoObject* py_empty_doc = PythonEnvironment::getInternedString(ctx, "")->asObject(ctx);

    auto add_stub = [&](const char* name) {
        const proto::ProtoString* nameS = proto::ProtoString::createSymbol(ctx, name);
        const proto::ProtoObject* stub = ctx->newObject(false);
        stub = stub->setAttribute(ctx, py_name_s, PythonEnvironment::getInternedString(ctx, name)->asObject(ctx));
        stub = stub->setAttribute(ctx, py_doc_s, py_empty_doc);
        stub = stub->setAttribute(ctx, py_module_s, py_io_s);
        stub = stub->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "register"),
            ctx->fromMethod(const_cast<proto::ProtoObject*>(stub), py_io_register));
        
        ioMod = ioMod->setAttribute(ctx, nameS, stub);
    };

    add_stub("BlockingIOError");
    add_stub("UnsupportedOperation");
    add_stub("FileIO");
    add_stub("BytesIO");
    // StringIO: real implementation (not a stub) so tests can use it.
    {
        const proto::ProtoString* nameS = proto::ProtoString::createSymbol(ctx, "StringIO");
        const proto::ProtoObject* sio = ctx->newObject(false);
        sio = sio->setAttribute(ctx, py_name_s,
            PythonEnvironment::getInternedString(ctx, "StringIO")->asObject(ctx));
        sio = sio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__qualname__"),
            PythonEnvironment::getInternedString(ctx, "StringIO")->asObject(ctx));
        sio = sio->setAttribute(ctx, py_module_s, py_io_s);
        sio = sio->setAttribute(ctx, py_doc_s, py_empty_doc);
        // Make the class callable so StringIO(...) instantiates.
        sio = sio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__call__"),
            ctx->fromMethod(const_cast<proto::ProtoObject*>(sio), py_sio_call));
        // Instance methods are installed on the prototype so instances inherit them.
        sio = sio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "write"),
            ctx->fromMethod(nullptr, py_sio_write));
        sio = sio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "getvalue"),
            ctx->fromMethod(nullptr, py_sio_getvalue));
        sio = sio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "read"),
            ctx->fromMethod(nullptr, py_sio_read));
        sio = sio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "seek"),
            ctx->fromMethod(nullptr, py_sio_seek));
        sio = sio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "tell"),
            ctx->fromMethod(nullptr, py_sio_tell));
        sio = sio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "truncate"),
            ctx->fromMethod(nullptr, py_sio_truncate));
        sio = sio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "close"),
            ctx->fromMethod(nullptr, py_sio_close));
        sio = sio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "writable"),
            ctx->fromMethod(nullptr, py_sio_writable));
        sio = sio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "readable"),
            ctx->fromMethod(nullptr, py_sio_readable));
        sio = sio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__enter__"),
            ctx->fromMethod(nullptr, py_sio_enter));
        sio = sio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__exit__"),
            ctx->fromMethod(nullptr, py_sio_exit));
        // Default buffer state so hasattr returns True before first write.
        sio = sio->setAttribute(ctx, k_sio_buf(ctx),
            PythonEnvironment::getInternedString(ctx, "")->asObject(ctx));
        sio = sio->setAttribute(ctx, k_sio_pos(ctx), ctx->fromInteger(0));
        ioMod = ioMod->setAttribute(ctx, nameS, sio);
    }
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
