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

// Reads up to and including the next '\n' from `__file_buffer__`,
// erases the consumed prefix in place, and returns the line. Returns
// the empty string when the buffer is exhausted (caller distinguishes
// EOF from a true empty line by emptiness AND prior consumption).
static std::string io_consume_line(const proto::ProtoObject* self,
                                   proto::ProtoContext* context) {
    const proto::ProtoObject* bufObj = self->getAttribute(context, proto::ProtoString::createSymbol(context, "__file_buffer__"));
    if (!bufObj || !bufObj->asExternalPointer(context)) return std::string();
    std::string* buffer = static_cast<std::string*>(bufObj->asExternalPointer(context)->getPointer(context));
    if (!buffer || buffer->empty()) return std::string();
    size_t nl = buffer->find('\n');
    std::string line;
    if (nl == std::string::npos) {
        line = *buffer;
        buffer->clear();
    } else {
        line = buffer->substr(0, nl + 1);
        buffer->erase(0, nl + 1);
    }
    return line;
}

static const proto::ProtoObject* py_io_readline(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    std::string line = io_consume_line(self, context);
    return PythonEnvironment::getInternedString(context, line.c_str())->asObject(context);
}

static const proto::ProtoObject* py_io_iter(
    proto::ProtoContext*,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    // File objects are their own iterator: `for line in f` reuses the
    // same object across __next__ calls. Returning self preserves the
    // CPython contract.
    return self;
}

static const proto::ProtoObject* py_io_next(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    std::string line = io_consume_line(self, context);
    if (line.empty()) {
        // EOF — raise StopIteration so the caller terminates the loop.
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) env->raiseStopIteration(context);
        return nullptr;
    }
    return PythonEnvironment::getInternedString(context, line.c_str())->asObject(context);
}

static const proto::ProtoObject* py_io_flush(
    proto::ProtoContext*,
    const proto::ProtoObject*,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    // No-op for our buffer-backed file: writes already go to the
    // backing buffer immediately. Return None.
    return PROTO_NONE;
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

    // Pre-flight: when opening for read, fail with FileNotFoundError if
    // the file is missing. Previously a missing file silently produced
    // an empty fake handle, so callers like pdb's `with open(rcfile)`
    // never reached their `except OSError` branch and instead crashed
    // later when they tried to iterate the (un-iterable) handle.
    bool isRead = (mode.find('r') != std::string::npos)
                  && (mode.find('w') == std::string::npos)
                  && (mode.find('a') == std::string::npos)
                  && (mode.find('x') == std::string::npos);
    if (isRead) {
        std::ifstream probe(filename);
        if (!probe.is_open()) {
            PythonEnvironment* env = PythonEnvironment::fromContext(context);
            if (env) {
                env->raiseOSError(context, 2, "No such file or directory", filename);
                return nullptr;
            }
            return PROTO_NONE;
        }
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
    fileObj = fileObj->setAttribute(context, proto::ProtoString::createSymbol(context, "readline"),
        context->fromMethod(const_cast<proto::ProtoObject*>(fileObj), py_io_readline));
    fileObj = fileObj->setAttribute(context, proto::ProtoString::createSymbol(context, "__iter__"),
        context->fromMethod(const_cast<proto::ProtoObject*>(fileObj), py_io_iter));
    fileObj = fileObj->setAttribute(context, proto::ProtoString::createSymbol(context, "__next__"),
        context->fromMethod(const_cast<proto::ProtoObject*>(fileObj), py_io_next));
    fileObj = fileObj->setAttribute(context, proto::ProtoString::createSymbol(context, "flush"),
        context->fromMethod(const_cast<proto::ProtoObject*>(fileObj), py_io_flush));
    return fileObj;
}

static const proto::ProtoObject* py_io_register(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    // Dummy register method for ABCs
    return self;
}

// ----- BytesIO --------------------------------------------------------------
//
// In-memory byte stream. Storage layout mirrors StringIO: a ProtoString
// holding the raw octets in `__bio_buffer__` (we use ProtoString rather
// than ProtoByteBuffer because protoPython's interned-string path
// preserves embedded nulls when constructed with explicit length, and
// every read/write helper treats the string as raw octets via
// `getSize`/`toUTF8String`). Position lives in `__bio_pos__`.
//
// Inputs (write, ctor) are coerced to a std::string via bio_obj_to_bytes,
// which accepts:
//   - `bytes` wrappers (whose `__data__` is ProtoString or ByteBuffer);
//   - raw ByteBuffer cells;
//   - `memoryview` (recurses into __mv_data__);
//   - ProtoString (interpreted as raw octets);
//   - bytearray (stored as a wrapper with mutable __data__).
//
// Outputs (read, getvalue) return real `bytes` instances built via
// bio_make_bytes — never ProtoString — so callers see `type(b) is bytes`
// and avoid the same UTF-8 truncation hazards binascii's make_bytes
// already documents.

static const proto::ProtoString* k_bio_buf(proto::ProtoContext* c) {
    return proto::ProtoString::createSymbol(c, "__bio_buffer__");
}
static const proto::ProtoString* k_bio_pos(proto::ProtoContext* c) {
    return proto::ProtoString::createSymbol(c, "__bio_pos__");
}

// Build a `bytes` instance whose __data__ is a ProtoByteBuffer holding
// the given raw octets — same pattern as binascii::make_bytes so
// downstream code sees a real bytes value.
static const proto::ProtoObject* bio_make_bytes(proto::ProtoContext* ctx,
                                                const std::string& data) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    proto::ProtoObject* obj = const_cast<proto::ProtoObject*>(ctx->newObject(false));
    if (env && env->getBytesPrototype()) {
        obj = const_cast<proto::ProtoObject*>(obj->addParent(ctx, env->getBytesPrototype()));
        obj = const_cast<proto::ProtoObject*>(obj->setAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "__class__"),
            env->getBytesPrototype()));
    }
    const proto::ProtoByteBuffer* bb = ctx->newByteBuffer(
        data.data(), static_cast<unsigned long>(data.size()));
    obj = const_cast<proto::ProtoObject*>(obj->setAttribute(ctx,
        env ? env->getDataString() : PythonEnvironment::getInternalString(ctx, "__data__"),
        bb->asObject(ctx)));
    return obj;
}

// Coerce a Python bytes-like value to a std::string of raw octets.
// Returns "" when the input cannot be interpreted as bytes — matches
// binascii::obj_to_bytes for consistency across modules.
static std::string bio_obj_to_bytes(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (!obj || obj == PROTO_NONE) return std::string();
    if (obj->isString(ctx)) {
        std::string s;
        obj->asString(ctx)->toUTF8String(ctx, s);
        return s;
    }
    if (const proto::ProtoByteBuffer* bb = obj->asByteBuffer(ctx)) {
        unsigned long n = bb->getSize(ctx);
        std::string out(n, '\0');
        for (unsigned long i = 0; i < n; ++i) {
            out[i] = static_cast<char>(static_cast<unsigned char>(bb->getAt(ctx, i)));
        }
        return out;
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (env) {
        // memoryview wrapper.
        const proto::ProtoObject* mv = obj->getAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "__mv_data__"));
        if (mv && mv != PROTO_NONE && mv != obj) return bio_obj_to_bytes(ctx, mv);
        // bytes / bytearray wrapper: __data__ is ProtoString or ByteBuffer.
        const proto::ProtoObject* data = obj->getAttribute(ctx, env->getDataString());
        if (data && data != PROTO_NONE && data != obj) {
            if (const proto::ProtoByteBuffer* bb = data->asByteBuffer(ctx)) {
                unsigned long n = bb->getSize(ctx);
                std::string out(n, '\0');
                for (unsigned long i = 0; i < n; ++i) {
                    out[i] = static_cast<char>(static_cast<unsigned char>(bb->getAt(ctx, i)));
                }
                return out;
            }
            if (data->isString(ctx)) {
                std::string s;
                data->asString(ctx)->toUTF8String(ctx, s);
                return s;
            }
        }
    }
    return std::string();
}

static std::string bio_get_buf(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    const proto::ProtoObject* bufObj = self->getAttribute(ctx, k_bio_buf(ctx));
    if (!bufObj) return std::string();
    return bio_obj_to_bytes(ctx, bufObj);
}

static long bio_get_pos(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    const proto::ProtoObject* p = self->getAttribute(ctx, k_bio_pos(ctx));
    return (p && p->isInteger(ctx)) ? static_cast<long>(p->asLong(ctx)) : 0;
}

static const proto::ProtoObject* bio_set_state(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* self,
                                               const std::string& buf,
                                               long pos) {
    self = self->setAttribute(ctx, k_bio_buf(ctx), bio_make_bytes(ctx, buf));
    self = self->setAttribute(ctx, k_bio_pos(ctx), ctx->fromInteger(pos));
    return self;
}

static const proto::ProtoObject* py_bio_write(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1) return ctx->fromInteger(0);
    const proto::ProtoObject* data = args->getAt(ctx, 0);
    // Symmetric to StringIO.write: BytesIO.write requires bytes-like
    // and rejects str (CPython: TypeError("a bytes-like object is
    // required, not 'str'")). Base64.encode(StringIO, BytesIO) tests
    // this contract.
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (env && env->getStrPrototype() && data) {
        const proto::ProtoObject* cls = env->getType(ctx, data);
        if (cls == env->getStrPrototype()) {
            env->raiseTypeError(ctx,
                "a bytes-like object is required, not 'str'");
            return nullptr;
        }
    }
    std::string text = bio_obj_to_bytes(ctx, data);
    std::string buf = bio_get_buf(ctx, self);
    long pos = bio_get_pos(ctx, self);
    if (pos < 0) pos = 0;
    if (static_cast<size_t>(pos) > buf.size()) buf.append(static_cast<size_t>(pos) - buf.size(), '\0');
    size_t end = static_cast<size_t>(pos) + text.size();
    if (end > buf.size()) buf.resize(end, '\0');
    for (size_t i = 0; i < text.size(); ++i) buf[pos + i] = text[i];
    bio_set_state(ctx, self, buf, static_cast<long>(pos + text.size()));
    return ctx->fromInteger(static_cast<long>(text.size()));
}

static const proto::ProtoObject* py_bio_getvalue(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return bio_make_bytes(ctx, bio_get_buf(ctx, self));
}

// CPython's BytesIO.getbuffer() returns a writable memoryview over the
// internal buffer.  protoPython does not yet implement memoryview, but
// every observed consumer (pickle framer at lib/python3.14/pickle.py:214)
// only uses `len(data)` and writes its content out — bytes satisfies that
// contract via __len__ and the buffer protocol.  Returning bytes here
// unblocks pickle round-trip without growing a memoryview dependency.
static const proto::ProtoObject* py_bio_getbuffer(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return bio_make_bytes(ctx, bio_get_buf(ctx, self));
}

static const proto::ProtoObject* py_bio_read(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    std::string buf = bio_get_buf(ctx, self);
    long pos = bio_get_pos(ctx, self);
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
    bio_set_state(ctx, self, buf, pos + static_cast<long>(out.size()));
    return bio_make_bytes(ctx, out);
}

static const proto::ProtoObject* py_bio_readline(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    std::string buf = bio_get_buf(ctx, self);
    long pos = bio_get_pos(ctx, self);
    if (pos < 0) pos = 0;
    long limit = -1;
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a = args->getAt(ctx, 0);
        if (a && a->isInteger(ctx)) limit = static_cast<long>(a->asLong(ctx));
    }
    if (static_cast<size_t>(pos) >= buf.size()) {
        return bio_make_bytes(ctx, std::string());
    }
    size_t end = buf.find('\n', pos);
    if (end == std::string::npos) end = buf.size();
    else end += 1;  // include the '\n'
    size_t take = end - pos;
    if (limit >= 0 && take > static_cast<size_t>(limit)) take = static_cast<size_t>(limit);
    std::string out = buf.substr(pos, take);
    bio_set_state(ctx, self, buf, pos + static_cast<long>(take));
    return bio_make_bytes(ctx, out);
}

static const proto::ProtoObject* py_bio_readlines(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoList* lines = ctx->newList();
    for (;;) {
        const proto::ProtoObject* line = py_bio_readline(ctx, self, nullptr, ctx->newList(), nullptr);
        // EOF: bio_make_bytes("") produces a bytes instance with empty
        // __data__ — peek into it to detect end of stream.
        std::string raw = bio_obj_to_bytes(ctx, line);
        if (raw.empty()) break;
        lines = lines->appendLast(ctx, line);
    }
    return lines->asObject(ctx);
}

static const proto::ProtoObject* py_bio_iter(
    proto::ProtoContext*, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return self;
}

static const proto::ProtoObject* py_bio_next(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    const proto::ProtoObject* line = py_bio_readline(ctx, self, nullptr, ctx->newList(), nullptr);
    std::string raw = bio_obj_to_bytes(ctx, line);
    if (raw.empty()) {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        if (env) env->raiseStopIteration(ctx);
        return nullptr;
    }
    return line;
}

static const proto::ProtoObject* py_bio_seek(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    long off = 0;
    long whence = 0;
    if (args && args->getSize(ctx) > 0 && args->getAt(ctx, 0)->isInteger(ctx))
        off = static_cast<long>(args->getAt(ctx, 0)->asLong(ctx));
    if (args && args->getSize(ctx) > 1 && args->getAt(ctx, 1)->isInteger(ctx))
        whence = static_cast<long>(args->getAt(ctx, 1)->asLong(ctx));
    std::string buf = bio_get_buf(ctx, self);
    long pos = bio_get_pos(ctx, self);
    long newPos = pos;
    if (whence == 0) newPos = off;
    else if (whence == 1) newPos = pos + off;
    else if (whence == 2) newPos = static_cast<long>(buf.size()) + off;
    if (newPos < 0) newPos = 0;
    bio_set_state(ctx, self, buf, newPos);
    return ctx->fromInteger(newPos);
}

static const proto::ProtoObject* py_bio_tell(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return ctx->fromInteger(bio_get_pos(ctx, self));
}

static const proto::ProtoObject* py_bio_truncate(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    std::string buf = bio_get_buf(ctx, self);
    long pos = bio_get_pos(ctx, self);
    long size = pos;
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a = args->getAt(ctx, 0);
        if (a && a->isInteger(ctx)) size = static_cast<long>(a->asLong(ctx));
    }
    if (size < 0) size = 0;
    if (static_cast<size_t>(size) < buf.size()) buf.resize(size);
    bio_set_state(ctx, self, buf, pos);
    return ctx->fromInteger(static_cast<long>(buf.size()));
}

static const proto::ProtoObject* py_bio_close(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    bio_set_state(ctx, self, std::string(), 0);
    return PROTO_NONE;
}

static const proto::ProtoObject* py_bio_flush(
    proto::ProtoContext*, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return PROTO_NONE;
}

static const proto::ProtoObject* py_bio_writable(
    proto::ProtoContext*, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return PROTO_TRUE;
}

static const proto::ProtoObject* py_bio_readable(
    proto::ProtoContext*, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return PROTO_TRUE;
}

static const proto::ProtoObject* py_bio_seekable(
    proto::ProtoContext*, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return PROTO_TRUE;
}

static const proto::ProtoObject* py_bio_enter(
    proto::ProtoContext*, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return self;
}

static const proto::ProtoObject* py_bio_exit(
    proto::ProtoContext*, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return PROTO_FALSE;
}

// BytesIO.__new__(cls, initial_bytes=b'') — fresh instance.
// CPython's class instantiation routes `BytesIO(b)` through
// type.__call__ which calls cls.__new__(cls, *args) and then
// __init__(instance, *args). protoPython invokes __new__ with
// positionalParameters[0] = cls, [1..] = the user's args (mirroring
// py_uniontype_new). Setting __call__ on the class did NOT work in
// practice — class instantiation bypassed it for stdlib-style
// classes — so we register __new__ on both StringIO and BytesIO
// and absorb the construction work there.
static const proto::ProtoObject* py_bio_new(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* cls = args->getAt(ctx, 0);
    proto::ProtoObject* inst = const_cast<proto::ProtoObject*>(cls->newChild(ctx, true));
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    inst = const_cast<proto::ProtoObject*>(inst->setAttribute(ctx,
        env ? env->getClassString() : PythonEnvironment::getInternalString(ctx, "__class__"), cls));
    std::string initial;
    if (args->getSize(ctx) >= 2) {
        const proto::ProtoObject* a = args->getAt(ctx, 1);
        if (a && a != PROTO_NONE) initial = bio_obj_to_bytes(ctx, a);
    }
    inst = const_cast<proto::ProtoObject*>(inst->setAttribute(ctx, k_bio_buf(ctx), bio_make_bytes(ctx, initial)));
    inst = const_cast<proto::ProtoObject*>(inst->setAttribute(ctx, k_bio_pos(ctx), ctx->fromInteger(0)));
    (void)env;
    return inst;
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
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    // CPython's StringIO.write requires str — passing bytes raises
    // TypeError("string argument expected, got 'bytes'"). Previously
    // we silently accepted bytes (the wrapper's __data__ is a
    // ProtoString, so the isString fallback below extracted text
    // and wrote it as UTF-8). Reject explicit bytes / bytearray
    // instances at the entry to surface the same error CPython
    // does — base64.encode(BytesIO, StringIO) depends on it.
    if (env && env->getBytesPrototype() && data) {
        const proto::ProtoObject* cls = env->getType(ctx, data);
        if (cls == env->getBytesPrototype()) {
            env->raiseTypeError(ctx,
                "string argument expected, got 'bytes'");
            return nullptr;
        }
    }
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

// StringIO.__new__(cls, initial_value='', newline='\n') — fresh instance
// with `initial_value` preloaded. Routed through __new__ rather than
// __call__ because protoPython's class-instantiation path bypasses
// __call__ for stdlib-style classes (it goes type.__call__ →
// cls.__new__ → cls.__init__, none of which previously did anything
// for StringIO, so `StringIO('hello').getvalue()` returned ''
// regardless of input).
static const proto::ProtoObject* py_sio_new(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    if (!args || args->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* cls = args->getAt(ctx, 0);
    proto::ProtoObject* inst = const_cast<proto::ProtoObject*>(cls->newChild(ctx, true));
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    inst = const_cast<proto::ProtoObject*>(inst->setAttribute(ctx,
        env ? env->getClassString() : PythonEnvironment::getInternalString(ctx, "__class__"), cls));
    std::string initial;
    if (args->getSize(ctx) >= 2) {
        const proto::ProtoObject* a = args->getAt(ctx, 1);
        if (a && a != PROTO_NONE) {
            if (a->isString(ctx)) {
                a->asString(ctx)->toUTF8String(ctx, initial);
            } else {
                const proto::ProtoObject* d = a->getAttribute(ctx,
                    PythonEnvironment::getInternedString(ctx, "__data__"));
                if (d && d->isString(ctx)) d->asString(ctx)->toUTF8String(ctx, initial);
            }
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
    // STRUCT-183: FileIO.closed needs to be a descriptor-shaped object
    // whose __doc__ matches CPython's getset descriptor.  test_descrdoc
    // checks `FileIO.closed.__doc__ == "True if the file is closed"`.
    // The stub created by add_stub doesn't carry that introspection
    // string; install it post-hoc on a child object stored as `closed`.
    {
        const proto::ProtoObject* fileIO = ioMod->getAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "FileIO"));
        if (fileIO && fileIO != PROTO_NONE) {
            const proto::ProtoObject* closedDescr = ctx->newObject(false);
            closedDescr = closedDescr->setAttribute(ctx, py_doc_s,
                PythonEnvironment::getInternedString(ctx,
                    "True if the file is closed")->asObject(ctx));
            const proto::ProtoObject* updated = fileIO->setAttribute(ctx,
                PythonEnvironment::getInternedString(ctx, "closed"), closedDescr);
            ioMod = ioMod->setAttribute(ctx,
                PythonEnvironment::getInternedString(ctx, "FileIO"), updated);
        }
    }
    // BytesIO: real implementation (mirrors StringIO). Tests in
    // test_base64 / test_struct / test_pickle rely on read, readline,
    // write, seek, tell, getvalue, iteration, and the context-manager
    // protocol; everything below maps to the corresponding py_bio_*.
    {
        const proto::ProtoString* nameS = proto::ProtoString::createSymbol(ctx, "BytesIO");
        const proto::ProtoObject* bio = ctx->newObject(false);
        bio = bio->setAttribute(ctx, py_name_s,
            PythonEnvironment::getInternedString(ctx, "BytesIO")->asObject(ctx));
        bio = bio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__qualname__"),
            PythonEnvironment::getInternedString(ctx, "BytesIO")->asObject(ctx));
        bio = bio->setAttribute(ctx, py_module_s, py_io_s);
        bio = bio->setAttribute(ctx, py_doc_s, py_empty_doc);
        bio = bio->setAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "__new__"),
            ctx->fromMethod(nullptr, py_bio_new));
        bio = bio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "write"),
            ctx->fromMethod(nullptr, py_bio_write));
        bio = bio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "getvalue"),
            ctx->fromMethod(nullptr, py_bio_getvalue));
        bio = bio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "getbuffer"),
            ctx->fromMethod(nullptr, py_bio_getbuffer));
        bio = bio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "read"),
            ctx->fromMethod(nullptr, py_bio_read));
        bio = bio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "readline"),
            ctx->fromMethod(nullptr, py_bio_readline));
        bio = bio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "readlines"),
            ctx->fromMethod(nullptr, py_bio_readlines));
        bio = bio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__iter__"),
            ctx->fromMethod(nullptr, py_bio_iter));
        bio = bio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__next__"),
            ctx->fromMethod(nullptr, py_bio_next));
        bio = bio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "seek"),
            ctx->fromMethod(nullptr, py_bio_seek));
        bio = bio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "tell"),
            ctx->fromMethod(nullptr, py_bio_tell));
        bio = bio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "truncate"),
            ctx->fromMethod(nullptr, py_bio_truncate));
        bio = bio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "close"),
            ctx->fromMethod(nullptr, py_bio_close));
        bio = bio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "flush"),
            ctx->fromMethod(nullptr, py_bio_flush));
        bio = bio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "writable"),
            ctx->fromMethod(nullptr, py_bio_writable));
        bio = bio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "readable"),
            ctx->fromMethod(nullptr, py_bio_readable));
        bio = bio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "seekable"),
            ctx->fromMethod(nullptr, py_bio_seekable));
        bio = bio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__enter__"),
            ctx->fromMethod(nullptr, py_bio_enter));
        bio = bio->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__exit__"),
            ctx->fromMethod(nullptr, py_bio_exit));
        // Empty initial state so `hasattr` reports True before the
        // first write — matches StringIO's startup contract.
        bio = bio->setAttribute(ctx, k_bio_buf(ctx), bio_make_bytes(ctx, std::string()));
        bio = bio->setAttribute(ctx, k_bio_pos(ctx), ctx->fromInteger(0));
        ioMod = ioMod->setAttribute(ctx, nameS, bio);
    }
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
        // Class instantiation: __new__ creates a fresh instance with the
        // user-provided initial state. (Setting __call__ does not work
        // for stdlib-shaped classes; see py_sio_new comment.)
        sio = sio->setAttribute(ctx,
            PythonEnvironment::getInternedString(ctx, "__new__"),
            ctx->fromMethod(nullptr, py_sio_new));
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
