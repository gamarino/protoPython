#include <protoPython/PythonEnvironment.h>
#include <protoPython/IOModule.h>
#include <protoPython/DiagUtils.h>
#include <algorithm>
#include <cstdio>
#include <cerrno>
#include <cstring>
#include <sstream>
#include <string>
#include <iostream>
#include <fstream>
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/stat.h>
#include <unistd.h>
#endif
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#endif
#include "PosixCompat.h"

namespace protoPython {
namespace io {

static void file_buffer_finalizer(void* ptr) {
    delete static_cast<std::string*>(ptr);
}

// Build a real `bytes` instance whose __data__ is a ProtoByteBuffer.
// Forward-declared so the fd-based read path can return real bytes
// instead of strings (subprocess feeds the result back into byte ops).
static const proto::ProtoObject* bio_make_bytes(proto::ProtoContext* ctx,
                                                const std::string& data);

// Pull __file_fd__ attribute if present; -1 means "not an fd-backed
// file" and the buffer-based code paths apply.
static int io_get_fd(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    if (!self) return -1;
    const proto::ProtoObject* fdObj = self->getAttribute(ctx,
        proto::ProtoString::createSymbol(ctx, "__file_fd__"));
    if (!fdObj || fdObj == PROTO_NONE) return -1;
    if (!fdObj->isInteger(ctx)) return -1;
    return static_cast<int>(fdObj->asLong(ctx));
}

// Extract raw octets from a bytes/bytearray/str payload — same helper
// as OsModule's extractRawBytes, kept local so this file stays
// self-contained.
static bool io_extract_bytes(proto::ProtoContext* ctx,
                             const proto::ProtoObject* obj,
                             std::string& out) {
    if (!obj) return false;
    if (obj->isString(ctx)) {
        obj->asString(ctx)->toUTF8String(ctx, out);
        return true;
    }
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoString* dataKey = env ? env->getDataString()
        : PythonEnvironment::getInternedString(ctx, "__data__");
    const proto::ProtoObject* d = obj->getAttribute(ctx, dataKey);
    if (!d) return false;
    if (d->isByteBuffer(ctx)) {
        const proto::ProtoByteBuffer* bb = d->asByteBuffer(ctx);
        if (bb) { out.assign(bb->getBuffer(ctx), bb->getSize(ctx)); return true; }
    } else if (d->isString(ctx)) {
        d->asString(ctx)->toUTF8String(ctx, out);
        return true;
    }
    return false;
}

// A buffer-backed file opened in binary mode ("rb", "r+b", ...) reads bytes,
// as in CPython; a text-mode one reads str.
static bool io_is_binary(proto::ProtoContext* context, const proto::ProtoObject* self) {
    const proto::ProtoObject* m = self->getAttribute(context, proto::ProtoString::createSymbol(context, "mode"));
    if (!m || m == PROTO_NONE || !m->isString(context)) return false;
    std::string mode;
    m->asString(context)->toUTF8String(context, mode);
    return mode.find('b') != std::string::npos;
}

static const proto::ProtoObject* io_read_result(proto::ProtoContext* context, const proto::ProtoObject* self,
                                                const std::string& data) {
    if (io_is_binary(context, self)) return bio_make_bytes(context, data);
    return PythonEnvironment::getInternedString(context, data.c_str())->asObject(context);
}

// ----- Descriptor-backed files -----------------------------------------------
//
// open() in a writing or read-write mode, open() with an opener, and
// io.open(fd) return a file object over an OS descriptor. The descriptor
// belongs to the object (unless closefd=False): close() and `with` release
// it at once, and an object that is never closed releases it when the
// collector reclaims it (the FdOwner finalizer below). CPython closes such a
// file as soon as its last reference goes; protoPython has no reference
// counts, so an unclosed file stays open until it is collected -- and
// protoCore collects only under a configured heap limit
// (PROTOCORE_HEAP_LIMIT_CELLS) -- or until the process exits.
//
// Reads go through a read-ahead buffer owned with the descriptor (readline
// reads a block and keeps the rest); writes are unbuffered. Text mode is
// UTF-8: reads translate "\r\n" and "\r" to "\n" when newline is None, and
// writes translate "\n" to os.linesep ("\r\n" on Windows) when newline is
// None, or to the newline given, as CPython's TextIOWrapper.
struct FdOwner {
    int fd;
    bool closefd;
    std::string readAhead;  // bytes read from fd but not yet consumed
    bool eof = false;       // the last read returned 0 bytes
};

// Runs on protoCore's collector thread.
static void fd_owner_finalizer(void* p) {
    FdOwner* owner = static_cast<FdOwner*>(p);
    if (owner->fd >= 0 && owner->closefd) {
#if defined(_WIN32)
        // A descriptor the program closed itself (os.close(f.fileno())) is
        // an error to report, not a reason to end the process.
        ProtopyInvalidParameterScope reportInvalidParameters;
#endif
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__) || defined(_WIN32)
        ::close(owner->fd);
#endif
    }
    delete owner;
}

static FdOwner* io_fd_owner(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    const proto::ProtoObject* o = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__fd_owner__"));
    if (!o || o == PROTO_NONE) return nullptr;
    const proto::ProtoExternalPointer* ext = o->asExternalPointer(ctx);
    return ext ? static_cast<FdOwner*>(ext->getPointer(ctx)) : nullptr;
}

static bool io_flag(proto::ProtoContext* ctx, const proto::ProtoObject* self, const char* name) {
    const proto::ProtoObject* v = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, name));
    return v == PROTO_TRUE;
}

static long long io_lseek(int fd, long long offset, int whence) {
#if defined(_WIN32)
    return _lseeki64(fd, offset, whence);
#else
    return static_cast<long long>(::lseek(fd, static_cast<off_t>(offset), whence));
#endif
}

static void io_raise_errno(proto::ProtoContext* ctx, int err) {
    if (PythonEnvironment* env = PythonEnvironment::fromContext(ctx)) env->raiseOSError(ctx, err, std::strerror(err));
}

static void io_raise_closed(proto::ProtoContext* ctx) {
    if (PythonEnvironment* env = PythonEnvironment::fromContext(ctx)) {
        env->raiseValueError(ctx, PythonEnvironment::newStr(ctx, "I/O operation on closed file."));
    }
}

// One read(2) of up to `n` bytes, outside the managed region (it may block).
// Returns the count, 0 at end of file; -1 with OSError pending.
static long long io_read_once(proto::ProtoContext* ctx, int fd, char* buf, size_t n) {
    for (;;) {
        long long got;
        int err = 0;
        {
            proto::ProtoContext::UnmanagedScope u(ctx);
#if defined(_WIN32)
            got = ::read(fd, buf, static_cast<unsigned>(n > 0x7fffffff ? 0x7fffffff : n));
#else
            got = static_cast<long long>(::read(fd, buf, n));
#endif
            err = (got < 0) ? errno : 0;
        }
        if (got >= 0) return got;
        if (err == EINTR) continue;
        io_raise_errno(ctx, err);
        return -1;
    }
}

// Appends one more block to the read-ahead buffer. Returns false with an
// exception pending; sets eof at end of file.
static bool io_fill(proto::ProtoContext* ctx, FdOwner* owner) {
    char chunk[8192];
    const long long got = io_read_once(ctx, owner->fd, chunk, sizeof(chunk));
    if (got < 0) return false;
    if (got == 0) owner->eof = true;
    else owner->readAhead.append(chunk, static_cast<size_t>(got));
    return true;
}

// Before writing or seeking, give back what was read ahead, so the file
// position is where the caller believes it is.
static void io_drop_read_ahead(FdOwner* owner) {
    if (!owner->readAhead.empty()) {
        io_lseek(owner->fd, -static_cast<long long>(owner->readAhead.size()), SEEK_CUR);
        owner->readAhead.clear();
    }
    owner->eof = false;
}

// "\r\n" and "\r" -> "\n".
static void io_translate_universal(std::string& s) {
    if (s.find('\r') == std::string::npos) return;
    std::string out;
    out.reserve(s.size());
    for (size_t i = 0; i < s.size(); ++i) {
        if (s[i] == '\r') {
            out.push_back('\n');
            if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
        } else {
            out.push_back(s[i]);
        }
    }
    s.swap(out);
}

// Makes sure a "\r" at the end of the read-ahead buffer is not the first half
// of a "\r\n" pair split across reads.
static bool io_complete_cr(proto::ProtoContext* ctx, FdOwner* owner) {
    while (!owner->eof && !owner->readAhead.empty() && owner->readAhead.back() == '\r') {
        const size_t before = owner->readAhead.size();
        if (!io_fill(ctx, owner)) return false;
        if (owner->readAhead.size() > before) break;
    }
    return true;
}

// The bytes of a read(n) on a descriptor file. Binary: as a raw read, at most
// one read(2) (what was read ahead first). Text: n characters, reading until
// there are enough or the file ends.
static bool io_fd_read(proto::ProtoContext* ctx, FdOwner* owner, bool text, bool universal,
                       long long n, std::string& out) {
    out.clear();
    if (n < 0) {
        while (!owner->eof) {
            if (!io_fill(ctx, owner)) return false;
        }
        out.swap(owner->readAhead);
        owner->eof = false;
        if (text && universal) io_translate_universal(out);
        return true;
    }
    if (n == 0) return true;
    if (!text) {
        if (owner->readAhead.empty()) {
            out.resize(static_cast<size_t>(n));
            const long long got = io_read_once(ctx, owner->fd, &out[0], static_cast<size_t>(n));
            if (got < 0) return false;
            out.resize(static_cast<size_t>(got));
            return true;
        }
        const size_t take = std::min(owner->readAhead.size(), static_cast<size_t>(n));
        out.assign(owner->readAhead, 0, take);
        owner->readAhead.erase(0, take);
        return true;
    }
    // Text: walk code points (a "\r\n" pair is one character when
    // translating), reading more until n of them are available.
    size_t i = 0;
    long long chars = 0;
    for (;;) {
        std::string& buf = owner->readAhead;
        while (chars < n && i < buf.size()) {
            const unsigned char c = static_cast<unsigned char>(buf[i]);
            size_t len = c < 0x80 ? 1 : (c >> 5) == 0x6 ? 2 : (c >> 4) == 0xe ? 3 : (c >> 3) == 0x1e ? 4 : 1;
            if (universal && c == '\r') {
                if (i + 1 >= buf.size() && !owner->eof) break;  // need the next byte
                if (i + 1 < buf.size() && buf[i + 1] == '\n') len = 2;
            }
            if (i + len > buf.size() && !owner->eof) break;  // an incomplete character
            i += std::min(len, buf.size() - i);
            ++chars;
        }
        if (chars >= n || owner->eof) break;
        if (!io_fill(ctx, owner)) return false;
    }
    out.assign(owner->readAhead, 0, i);
    owner->readAhead.erase(0, i);
    if (universal) io_translate_universal(out);
    return true;
}

// The bytes of the next line (with its terminator), "" at end of file.
static bool io_fd_readline(proto::ProtoContext* ctx, FdOwner* owner, bool universal, std::string& out) {
    size_t scanned = 0;
    for (;;) {
        std::string& buf = owner->readAhead;
        for (size_t i = scanned; i < buf.size(); ++i) {
            if (buf[i] == '\n' || (universal && buf[i] == '\r')) {
                size_t end = i + 1;
                if (buf[i] == '\r') {
                    if (end >= buf.size() && !owner->eof) {
                        // Is it "\r\n"? Read on to find out.
                        if (!io_fill(ctx, owner)) return false;
                        return io_fd_readline(ctx, owner, universal, out);
                    }
                    if (end < buf.size() && buf[end] == '\n') ++end;
                }
                out.assign(buf, 0, end);
                buf.erase(0, end);
                if (universal) io_translate_universal(out);
                return true;
            }
        }
        scanned = buf.size();
        if (owner->eof) {
            out.swap(buf);
            buf.clear();
            if (universal) io_translate_universal(out);
            return true;
        }
        if (!io_fill(ctx, owner)) return false;
    }
}

// The Python value of bytes read from a descriptor file: str in text mode,
// bytes in binary mode.
static const proto::ProtoObject* io_fd_result(proto::ProtoContext* ctx, bool text, const std::string& data) {
    return text ? PythonEnvironment::newStr(ctx, data) : bio_make_bytes(ctx, data);
}

// An open descriptor file whose descriptor was released by close(); raises
// ValueError and returns true for an operation on it.
static bool io_closed_fd_file(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    if (io_fd_owner(ctx, self) && io_get_fd(ctx, self) < 0) {
        io_raise_closed(ctx);
        return true;
    }
    return false;
}

// Writes all of `data` to the descriptor. Returns false with OSError pending.
static bool io_write_all(proto::ProtoContext* ctx, int fd, const std::string& data) {
    size_t done = 0;
    while (done < data.size()) {
        long long written;
        int err = 0;
        {
            // ::write can block on full pipes / sockets / slow disks.
            proto::ProtoContext::UnmanagedScope u(ctx);
            const size_t chunk = std::min<size_t>(data.size() - done, 0x40000000);
#if defined(_WIN32)
            written = ::write(fd, data.data() + done, static_cast<unsigned>(chunk));
#else
            written = static_cast<long long>(::write(fd, data.data() + done, chunk));
#endif
            err = (written < 0) ? errno : 0;
        }
        if (written < 0) {
            if (err == EINTR) continue;
            io_raise_errno(ctx, err);
            return false;
        }
        done += static_cast<size_t>(written);
    }
    return true;
}

// file.write(data) on a descriptor file: str in text mode (encoded as UTF-8,
// "\n" translated as the newline argument asks), a bytes-like object in
// binary mode. Returns the number of characters or bytes written.
static const proto::ProtoObject* io_fd_write(proto::ProtoContext* ctx, const proto::ProtoObject* self,
                                             FdOwner* owner, const proto::ProtoObject* data) {
    const bool text = io_flag(ctx, self, "__text__");
    std::string s;
    if (text) {
        if (!data->isString(ctx)) {
            if (PythonEnvironment* env = PythonEnvironment::fromContext(ctx)) {
                env->raiseTypeError(ctx, "write() argument must be str");
            }
            return nullptr;
        }
        data->asString(ctx)->toUTF8String(ctx, s);
        const proto::ProtoObject* nl = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__write_nl__"));
        if (nl && nl != PROTO_NONE && nl->isString(ctx) && s.find('\n') != std::string::npos) {
            std::string newline;
            nl->asString(ctx)->toUTF8String(ctx, newline);
            std::string out;
            out.reserve(s.size() + s.size() / 16);
            for (char c : s) {
                if (c == '\n') out += newline;
                else out.push_back(c);
            }
            s.swap(out);
        }
    } else if (!io_extract_bytes(ctx, data, s)) {
        if (PythonEnvironment* env = PythonEnvironment::fromContext(ctx)) {
            env->raiseTypeError(ctx, "a bytes-like object is required");
        }
        return nullptr;
    }
    io_drop_read_ahead(owner);
    if (!io_write_all(ctx, owner->fd, s)) return nullptr;
    if (text) return ctx->fromInteger(static_cast<long long>(data->asString(ctx)->getSize(ctx)));
    return ctx->fromInteger(static_cast<long long>(s.size()));
}

static const proto::ProtoObject* py_io_read(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    long long n = -1;
    if (posArgs->getSize(context) > 0 && posArgs->getAt(context, 0)->isInteger(context))
        n = posArgs->getAt(context, 0)->asLong(context);

    // A descriptor file reads through its read-ahead buffer (io_fd_read).
    int fd = io_get_fd(context, self);
    if (fd >= 0) {
        FdOwner* owner = io_fd_owner(context, self);
        if (owner) {
            const bool text = io_flag(context, self, "__text__");
            std::string out;
            if (!io_fd_read(context, owner, text, io_flag(context, self, "__universal__"), n, out)) return nullptr;
            return io_fd_result(context, text, out);
        }
    }
    if (io_closed_fd_file(context, self)) return nullptr;

    const proto::ProtoObject* bufObj = self->getAttribute(context, proto::ProtoString::createSymbol(context, "__file_buffer__"));
    if (!bufObj || !bufObj->asExternalPointer(context)) return PythonEnvironment::getInternedString(context, "")->asObject(context);
    std::string* buffer = static_cast<std::string*>(bufObj->asExternalPointer(context)->getPointer(context));
    if (!buffer) return PythonEnvironment::getInternedString(context, "")->asObject(context);
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
    return io_read_result(context, self, result);
}

static const proto::ProtoObject* py_io_close(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    // Descriptor file: release the descriptor now (unless closefd=False)
    // and mark the object closed; its owner no longer closes anything when
    // collected. Closing twice does nothing, as in CPython.
    int fd = io_get_fd(context, self);
    if (fd >= 0) {
        FdOwner* owner = io_fd_owner(context, self);
        const bool closefd = !owner || owner->closefd;
        if (owner) {
            owner->fd = -1;
            owner->readAhead.clear();
        }
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__) || defined(_WIN32)
        if (closefd) ::close(fd);
#endif
        const_cast<proto::ProtoObject*>(self)->setAttribute(context,
            proto::ProtoString::createSymbol(context, "__file_fd__"),
            context->fromInteger(-1));
        const_cast<proto::ProtoObject*>(self)->setAttribute(context,
            proto::ProtoString::createSymbol(context, "closed"),
            PROTO_TRUE);
        return PROTO_NONE;
    }
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

// The next line of a descriptor file, as str or bytes; nullptr with an
// exception pending.
static const proto::ProtoObject* io_fd_next_line(proto::ProtoContext* context, const proto::ProtoObject* self,
                                                 FdOwner* owner, bool& atEnd) {
    std::string line;
    if (!io_fd_readline(context, owner, io_flag(context, self, "__universal__"), line)) return nullptr;
    atEnd = line.empty();
    return io_fd_result(context, io_flag(context, self, "__text__"), line);
}

static const proto::ProtoObject* py_io_readline(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    if (io_get_fd(context, self) >= 0) {
        if (FdOwner* owner = io_fd_owner(context, self)) {
            bool atEnd = false;
            return io_fd_next_line(context, self, owner, atEnd);
        }
    }
    if (io_closed_fd_file(context, self)) return nullptr;
    std::string line = io_consume_line(self, context);
    return io_read_result(context, self, line);
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
    if (io_get_fd(context, self) >= 0) {
        if (FdOwner* owner = io_fd_owner(context, self)) {
            bool atEnd = false;
            const proto::ProtoObject* line = io_fd_next_line(context, self, owner, atEnd);
            if (line && atEnd) {
                if (PythonEnvironment* env = PythonEnvironment::fromContext(context)) env->raiseStopIteration(context);
                return nullptr;
            }
            return line;
        }
    }
    if (io_closed_fd_file(context, self)) return nullptr;
    std::string line = io_consume_line(self, context);
    if (line.empty()) {
        // EOF — raise StopIteration so the caller terminates the loop.
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (env) env->raiseStopIteration(context);
        return nullptr;
    }
    return io_read_result(context, self, line);
}

static const proto::ProtoObject* py_io_flush(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    // Writes are unbuffered (descriptor files) or go to the backing buffer
    // immediately, so there is nothing to flush; a closed file raises.
    if (io_closed_fd_file(ctx, self)) return nullptr;
    return PROTO_NONE;
}

// fileno() returns the wrapped fd for fd-backed files; -1 for
// closed files; raises for buffer-backed string files where the
// concept doesn't apply.
static const proto::ProtoObject* py_io_fileno(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    int fd = io_get_fd(ctx, self);
    if (fd < 0) {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        if (env) env->raiseOSError(ctx, 9, "Bad file descriptor", "");
        return nullptr;
    }
    return ctx->fromInteger(fd);
}

// readable/writable/seekable predicates — subprocess and asyncio
// poke these to decide whether to read/write or use TextIOWrapper. A
// descriptor file answers from its mode, as CPython's FileIO does.
static bool io_mode_has(proto::ProtoContext* ctx, const proto::ProtoObject* self, const char* chars) {
    const proto::ProtoObject* m = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "mode"));
    if (!m || !m->isString(ctx)) return false;
    std::string mode;
    m->asString(ctx)->toUTF8String(ctx, mode);
    return mode.find_first_of(chars) != std::string::npos;
}

static const proto::ProtoObject* py_io_readable(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    if (io_closed_fd_file(ctx, self)) return nullptr;
    int fd = io_get_fd(ctx, self);
    if (fd < 0) return PROTO_FALSE;
    return io_mode_has(ctx, self, "r+") ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_io_writable(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    if (io_closed_fd_file(ctx, self)) return nullptr;
    int fd = io_get_fd(ctx, self);
    if (fd < 0) return PROTO_FALSE;
    return io_mode_has(ctx, self, "wax+") ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_io_seekable(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    if (io_closed_fd_file(ctx, self)) return nullptr;
    // Files are seekable; pipes, sockets and terminals are not.
    int fd = io_get_fd(ctx, self);
    if (fd < 0) return PROTO_FALSE;
#if defined(_WIN32)
    const HANDLE h = reinterpret_cast<HANDLE>(_get_osfhandle(fd));
    if (h == INVALID_HANDLE_VALUE || GetFileType(h) != FILE_TYPE_DISK) return PROTO_FALSE;
#endif
    return io_lseek(fd, 0, SEEK_CUR) >= 0 ? PROTO_TRUE : PROTO_FALSE;
}

// file.seek(offset, whence=0) on a descriptor file: the new position.
static const proto::ProtoObject* py_io_seek(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (io_closed_fd_file(ctx, self)) return nullptr;
    FdOwner* owner = io_fd_owner(ctx, self);
    if (!owner || owner->fd < 0) return ctx->fromInteger(0);
    long long offset = 0;
    int whence = SEEK_SET;
    if (posArgs && posArgs->getSize(ctx) > 0 && posArgs->getAt(ctx, 0)->isInteger(ctx))
        offset = posArgs->getAt(ctx, 0)->asLong(ctx);
    if (posArgs && posArgs->getSize(ctx) > 1 && posArgs->getAt(ctx, 1)->isInteger(ctx))
        whence = static_cast<int>(posArgs->getAt(ctx, 1)->asLong(ctx));
    // A relative seek starts from where the caller is, not from the end of
    // what was read ahead.
    io_drop_read_ahead(owner);
    const long long pos = io_lseek(owner->fd, offset, whence);
    if (pos < 0) {
        io_raise_errno(ctx, errno);
        return nullptr;
    }
    return ctx->fromInteger(pos);
}

// file.tell() on a descriptor file.
static const proto::ProtoObject* py_io_tell(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    if (io_closed_fd_file(ctx, self)) return nullptr;
    FdOwner* owner = io_fd_owner(ctx, self);
    if (!owner || owner->fd < 0) return ctx->fromInteger(0);
    const long long pos = io_lseek(owner->fd, 0, SEEK_CUR);
    if (pos < 0) {
        io_raise_errno(ctx, errno);
        return nullptr;
    }
    return ctx->fromInteger(pos - static_cast<long long>(owner->readAhead.size()));
}

// file.truncate(size=None) on a descriptor file: the new size.
static const proto::ProtoObject* py_io_truncate(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (io_closed_fd_file(ctx, self)) return nullptr;
    FdOwner* owner = io_fd_owner(ctx, self);
    if (!owner || owner->fd < 0) return ctx->fromInteger(0);
    io_drop_read_ahead(owner);
    long long size = io_lseek(owner->fd, 0, SEEK_CUR);
    if (posArgs && posArgs->getSize(ctx) > 0 && posArgs->getAt(ctx, 0)->isInteger(ctx))
        size = posArgs->getAt(ctx, 0)->asLong(ctx);
#if defined(_WIN32)
    const int err = _chsize_s(owner->fd, size);
#else
    const int err = ::ftruncate(owner->fd, static_cast<off_t>(size)) == 0 ? 0 : errno;
#endif
    if (err != 0) {
        io_raise_errno(ctx, err);
        return nullptr;
    }
    return ctx->fromInteger(size);
}

// file.writelines(lines): write() of each item.
static const proto::ProtoObject* py_io_writelines(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*);

static const proto::ProtoObject* py_io_isatty(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    int fd = io_get_fd(ctx, self);
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__) || defined(_WIN32)
    if (fd >= 0) return ::isatty(fd) ? PROTO_TRUE : PROTO_FALSE;
#endif
    return PROTO_FALSE;
}

static const proto::ProtoObject* py_io_readlines(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*,
    const proto::ProtoSparseList*) {
    if (io_get_fd(context, self) >= 0) {
        if (FdOwner* owner = io_fd_owner(context, self)) {
            const proto::ProtoList* lines = context->newList();
            for (;;) {
                bool atEnd = false;
                const proto::ProtoObject* line = io_fd_next_line(context, self, owner, atEnd);
                if (!line) return nullptr;
                if (atEnd) break;
                lines = lines->appendLast(context, line);
            }
            return PythonEnvironment::wrapList(context, lines);
        }
    }
    if (io_closed_fd_file(context, self)) return nullptr;
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
        lines = lines->appendLast(context, io_read_result(context, self, line));
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
    const proto::ProtoObject* data = posArgs->getAt(context, 0);

    // Descriptor file: through to the descriptor (io_fd_write).
    if (io_get_fd(context, self) >= 0) {
        if (FdOwner* owner = io_fd_owner(context, self)) return io_fd_write(context, self, owner, data);
    }
    if (io_closed_fd_file(context, self)) return nullptr;

    const proto::ProtoObject* bufObj = self->getAttribute(context, proto::ProtoString::createSymbol(context, "__file_buffer__"));
    if (!bufObj || !bufObj->asExternalPointer(context)) return context->fromInteger(0);
    std::string* buffer = static_cast<std::string*>(bufObj->asExternalPointer(context)->getPointer(context));
    if (!buffer) return context->fromInteger(0);
    std::string s;
    if (data->isString(context)) data->asString(context)->toUTF8String(context, s);
    buffer->append(s);
    return context->fromInteger(static_cast<long long>(s.size()));
}

// The prototype of file objects over an OS descriptor. Their methods live
// here, not on each instance, so that special-method lookups that ignore
// instance attributes (the `with` statement) find __enter__ and __exit__:
// `with open(path, "w") as f:` used to leave the descriptor open.
static const proto::ProtoObject* io_make_fd_file_prototype(proto::ProtoContext* context) {
    const proto::ProtoObject* proto = context->newObject(true);
    auto method = [&](const char* name, proto::ProtoMethod fn) {
        proto->setAttribute(context, proto::ProtoString::createSymbol(context, name),
            context->fromMethod(nullptr, fn));
    };
    method("read", py_io_read);
    method("readline", py_io_readline);
    method("readlines", py_io_readlines);
    method("write", py_io_write);
    method("close", py_io_close);
    method("__enter__", py_io_enter);
    method("__exit__", py_io_exit);
    method("__iter__", py_io_iter);
    method("__next__", py_io_next);
    method("flush", py_io_flush);
    method("fileno", py_io_fileno);
    method("readable", py_io_readable);
    method("writable", py_io_writable);
    method("seekable", py_io_seekable);
    method("isatty", py_io_isatty);
    method("seek", py_io_seek);
    method("tell", py_io_tell);
    method("truncate", py_io_truncate);
    method("writelines", py_io_writelines);
    return proto;
}

static const proto::ProtoObject* py_io_writelines(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (!posArgs || posArgs->getSize(ctx) < 1) return PROTO_NONE;
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* lines = posArgs->getAt(ctx, 0);
    // Any iterable: list() it through the builtin.
    const proto::ProtoObject* listType = env && env->getBuiltins()
        ? env->getBuiltins()->getAttribute(ctx, PythonEnvironment::getInternedString(ctx, "list")) : nullptr;
    const proto::ProtoObject* asList = (listType && listType != PROTO_NONE) ? env->callObject(listType, {lines}) : nullptr;
    if (!asList) return env && env->hasPendingException() ? nullptr : PROTO_NONE;
    const proto::ProtoObject* data = asList->getAttribute(ctx, env->getDataString());
    const proto::ProtoList* items = data ? data->asList(ctx) : asList->asList(ctx);
    if (!items) return PROTO_NONE;
    for (proto::proto_ulong i = 0; i < items->getSize(ctx); ++i) {
        const proto::ProtoList* one = ctx->newList()->appendLast(ctx, items->getAt(ctx, static_cast<int>(i)));
        if (!py_io_write(ctx, self, nullptr, one, nullptr)) return nullptr;
    }
    return PROTO_NONE;
}

// open()'s argument at `index` or keyword `name`; nullptr when absent.
static const proto::ProtoObject* io_open_arg(proto::ProtoContext* context, const proto::ProtoList* pos,
                                             const proto::ProtoSparseList* kw, proto::proto_ulong index,
                                             const char* name) {
    if (pos && pos->getSize(context) > index) return pos->getAt(context, static_cast<int>(index));
    if (kw) {
        const proto::proto_ulong h = PythonEnvironment::getInternedString(context, name)->getHash(context);
        if (kw->has(context, h)) return kw->getAt(context, h);
    }
    return nullptr;
}

// open()'s `newline` argument (6th positional or keyword) is None or absent.
static bool io_newline_is_none(proto::ProtoContext* context, const proto::ProtoList* pos,
                               const proto::ProtoSparseList* kw) {
    const proto::ProtoObject* nl = io_open_arg(context, pos, kw, 5, "newline");
    return !nl || nl == PROTO_NONE;
}

// A file object over an open OS descriptor (see "Descriptor-backed files").
// It is mutable so that close() records `closed` and the released descriptor
// on it. `newline` is open()'s argument (nullptr: absent).
static const proto::ProtoObject* io_make_fd_file(proto::ProtoContext* context,
                                                 const proto::ProtoObject* ioModule, int fd,
                                                 const std::string& mode,
                                                 const proto::ProtoObject* nameObj,
                                                 bool closefd,
                                                 const proto::ProtoObject* newline) {
    const proto::ProtoObject* proto = ioModule ? ioModule->getAttribute(context,
        proto::ProtoString::createSymbol(context, "__fd_file_prototype__")) : nullptr;
    const proto::ProtoObject* fileObj = (proto && proto != PROTO_NONE)
        ? proto->newChild(context, true)
        : context->newObject(true);
    auto set = [&](const char* name, const proto::ProtoObject* value) {
        fileObj->setAttribute(context, proto::ProtoString::createSymbol(context, name), value);
    };
    set("__file_fd__", context->fromInteger(fd));
    set("__fd_owner__", context->fromExternalPointer(new FdOwner{fd, closefd}, fd_owner_finalizer));
    set("mode", PythonEnvironment::getInternedString(context, mode.c_str())->asObject(context));
    set("name", nameObj);
    set("buffering", context->fromInteger(-1));  // the default
    set("closed", PROTO_FALSE);
    const bool text = mode.find('b') == std::string::npos;
    std::string nl;
    const bool nlNone = !newline || newline == PROTO_NONE || !newline->isString(context);
    if (!nlNone) newline->asString(context)->toUTF8String(context, nl);
    set("__text__", text ? PROTO_TRUE : PROTO_FALSE);
    // Reading: universal newlines when newline is None.
    set("__universal__", (text && nlNone) ? PROTO_TRUE : PROTO_FALSE);
    // Writing: "\n" becomes os.linesep when newline is None, or the newline
    // given when it is "\r" or "\r\n"; "" and "\n" write "\n" as is.
#if defined(_WIN32)
    const char* linesep = "\r\n";
#else
    const char* linesep = nullptr;
#endif
    const char* writeNl = nullptr;
    if (text) {
        if (nlNone) writeNl = linesep;
        else if (nl == "\r" || nl == "\r\n") writeNl = nl == "\r" ? "\r" : "\r\n";
    }
    if (writeNl) set("__write_nl__", PythonEnvironment::getInternedString(context, writeNl)->asObject(context));
    return fileObj;
}

// The os.open() flags of an open() mode, as CPython's FileIO computes them.
static int io_mode_open_flags(const std::string& mode) {
    const bool plus = mode.find('+') != std::string::npos;
    int flags = 0;
    if (mode.find('w') != std::string::npos) flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_TRUNC;
    else if (mode.find('a') != std::string::npos) flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_APPEND;
    else if (mode.find('x') != std::string::npos) flags = (plus ? O_RDWR : O_WRONLY) | O_CREAT | O_EXCL;
    else flags = plus ? O_RDWR : O_RDONLY;
    flags |= O_CLOEXEC;
#ifdef O_BINARY
    flags |= O_BINARY;
#endif
    return flags;
}

static const proto::ProtoObject* py_io_open(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) < 1) return PROTO_NONE;

    const proto::ProtoObject* fileArg = positionalParameters->getAt(context, 0);
    std::string mode = "r";
    const proto::ProtoObject* modeArg = io_open_arg(context, positionalParameters, keywordParameters, 1, "mode");
    if (modeArg && modeArg->isString(context)) modeArg->asString(context)->toUTF8String(context, mode);
    const proto::ProtoObject* newlineArg = io_open_arg(context, positionalParameters, keywordParameters, 5, "newline");
    const proto::ProtoObject* closefdArg = io_open_arg(context, positionalParameters, keywordParameters, 6, "closefd");
    const proto::ProtoObject* openerArg = io_open_arg(context, positionalParameters, keywordParameters, 7, "opener");
    const bool closefd = !(closefdArg == PROTO_FALSE
                           || (closefdArg && closefdArg->isInteger(context) && closefdArg->asLong(context) == 0));

    // PEP 446 / CPython compat: io.open(fd: int, mode, ...) wraps an
    // existing OS fd into a file object.  subprocess.Popen routes
    // captured stdout/stderr through this path.
    if (fileArg && fileArg->isInteger(context)) {
        return io_make_fd_file(context, self, static_cast<int>(fileArg->asLong(context)), mode, fileArg,
                               closefd, newlineArg);
    }

    // open(file, mode, ..., opener=f): the descriptor is f(file, flags), as
    // in CPython. tempfile opens its files this way.
    if (openerArg && openerArg != PROTO_NONE) {
        PythonEnvironment* env = PythonEnvironment::fromContext(context);
        if (!env) return PROTO_NONE;
        const proto::ProtoObject* fdObj = env->callObject(openerArg,
            {fileArg, context->fromInteger(io_mode_open_flags(mode))});
        if (!fdObj) return nullptr;
        if (!fdObj->isInteger(context) || fdObj->asLong(context) < 0) {
            env->raiseValueError(context, PythonEnvironment::newStr(context,
                "opener returned " + std::string(fdObj->isInteger(context) ? std::to_string(fdObj->asLong(context)) : "a non-integer")));
            return nullptr;
        }
        return io_make_fd_file(context, self, static_cast<int>(fdObj->asLong(context)), mode, fileArg,
                               true, newlineArg);
    }

    // A str or an os.PathLike (such as pathlib.Path), as CPython accepts.
    std::string filename;
    if (!PythonEnvironment::fsPathArgument(context, fileArg, filename)) {
        PythonEnvironment* pathEnv = PythonEnvironment::fromContext(context);
        return (pathEnv && pathEnv->hasPendingException()) ? nullptr : PROTO_NONE;
    }

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__) || defined(_WIN32)
    // Writing and read-write modes ("w", "a", "x", "r+", ...) open the file
    // and work through the descriptor. Plain reads ("r", "rb") read the
    // whole file now and close it at once (the buffer-backed object below),
    // so they never keep a descriptor.
    if (mode.find('+') != std::string::npos || mode.find('w') != std::string::npos
        || mode.find('a') != std::string::npos || mode.find('x') != std::string::npos) {
        int fd = ::open(filename.c_str(), io_mode_open_flags(mode), 0666);
        if (fd < 0) {
            int err = errno;
            if (PythonEnvironment* env = PythonEnvironment::fromContext(context)) {
                env->raiseOSError(context, err, std::strerror(err), filename);
                return nullptr;
            }
            return PROTO_NONE;
        }
        return io_make_fd_file(context, self, fd, mode, fileArg, true, newlineArg);
    }
#endif

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
        // Report the error open(2) gives, and EISDIR for a directory, which
        // Linux opens read-only without error (CPython checks with fstat).
        int err = 0;
        int probeFd = ::open(filename.c_str(), O_RDONLY | O_CLOEXEC);
        if (probeFd < 0) {
            err = errno;
        } else {
            struct stat st;
            if (::fstat(probeFd, &st) == 0 && S_ISDIR(st.st_mode)) err = EISDIR;
            ::close(probeFd);
        }
        if (err != 0) {
            PythonEnvironment* env = PythonEnvironment::fromContext(context);
            if (env) {
                env->raiseOSError(context, err, std::strerror(err), filename);
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
        std::ifstream f(filename, std::ios::binary);
        if (f) {
            std::stringstream ss;
            ss << f.rdbuf();
            *buffer = ss.str();
        }
        // Universal newlines, as CPython's text mode on every platform: with
        // newline=None (the default) "\r\n" and "\r" read as "\n". A file
        // written on Windows reads the same everywhere.
        if (mode.find('b') == std::string::npos && io_newline_is_none(context, positionalParameters,
                                                                      keywordParameters)) {
            std::string& s = *buffer;
            if (s.find('\r') != std::string::npos) {
                std::string out;
                out.reserve(s.size());
                for (std::size_t i = 0; i < s.size(); ++i) {
                    if (s[i] == '\r') {
                        out.push_back('\n');
                        if (i + 1 < s.size() && s[i + 1] == '\n') ++i;
                    } else {
                        out.push_back(s[i]);
                    }
                }
                s.swap(out);
            }
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

// io.open_code(path): open(path, "rb"), as in CPython. site.addpackage reads
// each .pth file through it and decodes the bytes itself.
static const proto::ProtoObject* py_io_open_code(
    proto::ProtoContext* context,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* positionalParameters,
    const proto::ProtoSparseList* keywordParameters) {
    if (positionalParameters->getSize(context) != 1) return PROTO_NONE;
    const proto::ProtoList* args = context->newList()
        ->appendLast(context, positionalParameters->getAt(context, 0))
        ->appendLast(context, context->fromUTF8String("rb"));
    return py_io_open(context, self, parentLink, args, nullptr);
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
        data.data(), static_cast<proto::proto_ulong>(data.size()));
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
        proto::proto_ulong n = bb->getSize(ctx);
        std::string out(n, '\0');
        for (proto::proto_ulong i = 0; i < n; ++i) {
            out[i] = static_cast<char>(static_cast<unsigned char>(bb->getAt(ctx, static_cast<int>(i))));
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
                proto::proto_ulong n = bb->getSize(ctx);
                std::string out(n, '\0');
                for (proto::proto_ulong i = 0; i < n; ++i) {
                    out[i] = static_cast<char>(static_cast<unsigned char>(bb->getAt(ctx, static_cast<int>(i))));
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

static proto::proto_long bio_get_pos(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    const proto::ProtoObject* p = self->getAttribute(ctx, k_bio_pos(ctx));
    return (p && p->isInteger(ctx)) ? static_cast<proto::proto_long>(p->asLong(ctx)) : 0;
}

static const proto::ProtoObject* bio_set_state(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* self,
                                               const std::string& buf,
                                               proto::proto_long pos) {
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
    proto::proto_long pos = bio_get_pos(ctx, self);
    if (pos < 0) pos = 0;
    if (static_cast<size_t>(pos) > buf.size()) buf.append(static_cast<size_t>(pos) - buf.size(), '\0');
    size_t end = static_cast<size_t>(pos) + text.size();
    if (end > buf.size()) buf.resize(end, '\0');
    for (size_t i = 0; i < text.size(); ++i) buf[pos + i] = text[i];
    bio_set_state(ctx, self, buf, static_cast<proto::proto_long>(pos + text.size()));
    return ctx->fromInteger(static_cast<proto::proto_long>(text.size()));
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
    proto::proto_long pos = bio_get_pos(ctx, self);
    if (pos < 0) pos = 0;
    size_t size = buf.size();
    proto::proto_long want = -1;
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a = args->getAt(ctx, 0);
        if (a && a->isInteger(ctx)) want = static_cast<proto::proto_long>(a->asLong(ctx));
    }
    size_t take;
    if (want < 0) take = (static_cast<size_t>(pos) < size) ? size - pos : 0;
    else take = std::min<size_t>(size - std::min<size_t>(pos, size), static_cast<size_t>(want));
    std::string out = (pos < static_cast<proto::proto_long>(size)) ? buf.substr(pos, take) : std::string();
    bio_set_state(ctx, self, buf, pos + static_cast<proto::proto_long>(out.size()));
    return bio_make_bytes(ctx, out);
}

static const proto::ProtoObject* py_bio_readline(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    std::string buf = bio_get_buf(ctx, self);
    proto::proto_long pos = bio_get_pos(ctx, self);
    if (pos < 0) pos = 0;
    proto::proto_long limit = -1;
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a = args->getAt(ctx, 0);
        if (a && a->isInteger(ctx)) limit = static_cast<proto::proto_long>(a->asLong(ctx));
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
    bio_set_state(ctx, self, buf, pos + static_cast<proto::proto_long>(take));
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
    proto::proto_long off = 0;
    proto::proto_long whence = 0;
    if (args && args->getSize(ctx) > 0 && args->getAt(ctx, 0)->isInteger(ctx))
        off = static_cast<proto::proto_long>(args->getAt(ctx, 0)->asLong(ctx));
    if (args && args->getSize(ctx) > 1 && args->getAt(ctx, 1)->isInteger(ctx))
        whence = static_cast<proto::proto_long>(args->getAt(ctx, 1)->asLong(ctx));
    std::string buf = bio_get_buf(ctx, self);
    proto::proto_long pos = bio_get_pos(ctx, self);
    proto::proto_long newPos = pos;
    if (whence == 0) newPos = off;
    else if (whence == 1) newPos = pos + off;
    else if (whence == 2) newPos = static_cast<proto::proto_long>(buf.size()) + off;
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
    proto::proto_long pos = bio_get_pos(ctx, self);
    proto::proto_long size = pos;
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a = args->getAt(ctx, 0);
        if (a && a->isInteger(ctx)) size = static_cast<proto::proto_long>(a->asLong(ctx));
    }
    if (size < 0) size = 0;
    if (static_cast<size_t>(size) < buf.size()) buf.resize(size);
    bio_set_state(ctx, self, buf, pos);
    return ctx->fromInteger(static_cast<proto::proto_long>(buf.size()));
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

static proto::proto_long sio_get_pos(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    const proto::ProtoObject* p = self->getAttribute(ctx, k_sio_pos(ctx));
    return (p && p->isInteger(ctx)) ? static_cast<proto::proto_long>(p->asLong(ctx)) : 0;
}

static const proto::ProtoObject* sio_set_state(proto::ProtoContext* ctx,
                                               const proto::ProtoObject* self,
                                               const std::string& buf,
                                               proto::proto_long pos) {
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
    proto::proto_long pos = sio_get_pos(ctx, self);
    if (pos < 0) pos = 0;
    if (static_cast<size_t>(pos) > buf.size()) buf.append(static_cast<size_t>(pos) - buf.size(), '\0');
    // Overwrite starting at pos, extending buffer if necessary.
    size_t end = static_cast<size_t>(pos) + text.size();
    if (end > buf.size()) buf.resize(end, '\0');
    for (size_t i = 0; i < text.size(); ++i) buf[pos + i] = text[i];
    sio_set_state(ctx, self, buf, static_cast<proto::proto_long>(pos + text.size()));
    return ctx->fromInteger(static_cast<proto::proto_long>(text.size()));
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
    proto::proto_long pos = sio_get_pos(ctx, self);
    if (pos < 0) pos = 0;
    size_t size = buf.size();
    proto::proto_long want = -1;
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a = args->getAt(ctx, 0);
        if (a && a->isInteger(ctx)) want = static_cast<proto::proto_long>(a->asLong(ctx));
    }
    size_t take;
    if (want < 0) take = (static_cast<size_t>(pos) < size) ? size - pos : 0;
    else take = std::min<size_t>(size - std::min<size_t>(pos, size), static_cast<size_t>(want));
    std::string out = (pos < static_cast<proto::proto_long>(size)) ? buf.substr(pos, take) : std::string();
    sio_set_state(ctx, self, buf, pos + static_cast<proto::proto_long>(out.size()));
    return PythonEnvironment::getInternedString(ctx, out.c_str())->asObject(ctx);
}

static const proto::ProtoObject* py_sio_seek(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoList* args, const proto::ProtoSparseList*) {
    proto::proto_long off = 0;
    proto::proto_long whence = 0;
    if (args && args->getSize(ctx) > 0 && args->getAt(ctx, 0)->isInteger(ctx))
        off = static_cast<proto::proto_long>(args->getAt(ctx, 0)->asLong(ctx));
    if (args && args->getSize(ctx) > 1 && args->getAt(ctx, 1)->isInteger(ctx))
        whence = static_cast<proto::proto_long>(args->getAt(ctx, 1)->asLong(ctx));
    std::string buf = sio_get_buf(ctx, self);
    proto::proto_long pos = sio_get_pos(ctx, self);
    proto::proto_long newPos = pos;
    if (whence == 0) newPos = off;
    else if (whence == 1) newPos = pos + off;
    else if (whence == 2) newPos = static_cast<proto::proto_long>(buf.size()) + off;
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
    proto::proto_long pos = sio_get_pos(ctx, self);
    proto::proto_long size = pos;
    if (args && args->getSize(ctx) > 0) {
        const proto::ProtoObject* a = args->getAt(ctx, 0);
        if (a && a->isInteger(ctx)) size = static_cast<proto::proto_long>(a->asLong(ctx));
    }
    if (size < 0) size = 0;
    if (static_cast<size_t>(size) < buf.size()) buf.resize(size);
    sio_set_state(ctx, self, buf, pos);
    return ctx->fromInteger(static_cast<proto::proto_long>(buf.size()));
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

// io.text_encoding(encoding, stacklevel=2, /): the encoding argument a text
// stream should use. An explicit encoding is returned unchanged; None
// becomes "utf-8" in UTF-8 mode (sys.flags.utf8_mode) and "locale"
// otherwise, as in CPython's _io.text_encoding. EncodingWarning (emitted
// by CPython under -X warn_default_encoding) is not implemented.
static const proto::ProtoObject* py_io_text_encoding(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::proto_ulong argc = posArgs ? posArgs->getSize(ctx) : 0;
    if (argc < 1 || argc > 2) {
        if (env) env->raiseTypeError(ctx, argc < 1
            ? "text_encoding() missing required argument 'encoding' (pos 1)"
            : "text_encoding() takes at most 2 arguments (" + std::to_string(argc) + " given)");
        return nullptr;
    }
    const proto::ProtoObject* encoding = posArgs->getAt(ctx, 0);
    if (encoding && encoding != PROTO_NONE && !(env && encoding == env->getNonePrototype())) {
        return encoding;
    }
    bool utf8Mode = false;
    const proto::ProtoObject* sys = env ? env->getSysModule() : nullptr;
    if (sys) {
        const proto::ProtoObject* flags = env->getAttribute(ctx, sys,
            PythonEnvironment::getInternedString(ctx, "flags"), false);
        const proto::ProtoObject* mode = (flags && flags != PROTO_NONE)
            ? env->getAttribute(ctx, flags, PythonEnvironment::getInternedString(ctx, "utf8_mode"), false)
            : nullptr;
        if (env->hasPendingException()) env->clearPendingException();
        utf8Mode = mode && mode->isInteger(ctx) && mode->asLong(ctx) != 0;
    }
    return PythonEnvironment::getInternedString(ctx, utf8Mode ? "utf-8" : "locale")->asObject(ctx);
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* ioMod = ctx->newObject(false);
    
    ioMod = ioMod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__fd_file_prototype__"),
        io_make_fd_file_prototype(ctx));
    ioMod = ioMod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "open"), ctx->fromMethod(const_cast<proto::ProtoObject*>(ioMod), py_io_open));
    ioMod = ioMod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "open_code"), ctx->fromMethod(const_cast<proto::ProtoObject*>(ioMod), py_io_open_code));
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
    ioMod = ioMod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "text_encoding"),
        ctx->fromMethod(nullptr, py_io_text_encoding));
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
