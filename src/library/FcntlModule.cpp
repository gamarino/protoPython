#include <protoPython/FcntlModule.h>
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>

#include <cerrno>
#include <cstring>
#include <string>

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <fcntl.h>
#include <sys/ioctl.h>
#include <sys/file.h>   // flock
#endif

namespace protoPython {
namespace fcntl_module {

// fcntl.fcntl(fd, cmd, arg=0) -> int.  arg is either an integer
// (most common) or a bytes/str passed through as the third operand
// to ::fcntl.  We only handle the integer form here — that covers
// every use site in the standard library we ship.
static const proto::ProtoObject* py_fcntl_fcntl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    int fd  = static_cast<int>(posArgs->getAt(ctx, 0)->asLong(ctx));
    int cmd = static_cast<int>(posArgs->getAt(ctx, 1)->asLong(ctx));
    long arg = 0;
    if (posArgs->getSize(ctx) >= 3) {
        const proto::ProtoObject* a = posArgs->getAt(ctx, 2);
        if (a && a != PROTO_NONE && a->isInteger(ctx)) {
            arg = static_cast<long>(a->asLong(ctx));
        }
    }
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    int res = ::fcntl(fd, cmd, arg);
    if (res == -1) {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        if (env) env->raiseOSError(ctx, errno, std::strerror(errno), "");
        return nullptr;
    }
    return ctx->fromInteger(static_cast<long long>(res));
#else
    return PROTO_NONE;
#endif
}

// fcntl.ioctl(fd, request, arg=0) -> int.  Same integer-only
// limitation as fcntl().
static const proto::ProtoObject* py_fcntl_ioctl(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    int fd = static_cast<int>(posArgs->getAt(ctx, 0)->asLong(ctx));
    unsigned long request = static_cast<unsigned long>(posArgs->getAt(ctx, 1)->asLong(ctx));
    long arg = 0;
    if (posArgs->getSize(ctx) >= 3) {
        const proto::ProtoObject* a = posArgs->getAt(ctx, 2);
        if (a && a != PROTO_NONE && a->isInteger(ctx)) arg = static_cast<long>(a->asLong(ctx));
    }
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    int res = ::ioctl(fd, request, arg);
    if (res == -1) {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        if (env) env->raiseOSError(ctx, errno, std::strerror(errno), "");
        return nullptr;
    }
    return ctx->fromInteger(static_cast<long long>(res));
#else
    return PROTO_NONE;
#endif
}

// fcntl.flock(fd, op)
static const proto::ProtoObject* py_fcntl_flock(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    int fd = static_cast<int>(posArgs->getAt(ctx, 0)->asLong(ctx));
    int op = static_cast<int>(posArgs->getAt(ctx, 1)->asLong(ctx));
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    if (::flock(fd, op) == -1) {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        if (env) env->raiseOSError(ctx, errno, std::strerror(errno), "");
        return nullptr;
    }
#endif
    return PROTO_NONE;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx, PythonEnvironment* env) {
    const proto::ProtoObject* mod = env && env->getObjectPrototype()
        ? env->getObjectPrototype()->newChild(ctx, true)
        : ctx->newObject(false);

    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "fcntl"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_fcntl_fcntl));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "ioctl"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_fcntl_ioctl));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "flock"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_fcntl_flock));
    // lockf maps to fcntl(F_SETLK, ...) in CPython; we accept it as
    // an alias to flock — every consumer in the stdlib we ship only
    // uses LOCK_EX/LOCK_UN, which both implementations honour.
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "lockf"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_fcntl_flock));

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    // Command constants
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "F_DUPFD"),
        ctx->fromInteger(F_DUPFD));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "F_GETFD"),
        ctx->fromInteger(F_GETFD));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "F_SETFD"),
        ctx->fromInteger(F_SETFD));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "F_GETFL"),
        ctx->fromInteger(F_GETFL));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "F_SETFL"),
        ctx->fromInteger(F_SETFL));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "FD_CLOEXEC"),
        ctx->fromInteger(FD_CLOEXEC));
#ifdef F_DUPFD_CLOEXEC
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "F_DUPFD_CLOEXEC"),
        ctx->fromInteger(F_DUPFD_CLOEXEC));
#endif
#ifdef F_GETLK
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "F_GETLK"),
        ctx->fromInteger(F_GETLK));
#endif
#ifdef F_SETLK
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "F_SETLK"),
        ctx->fromInteger(F_SETLK));
#endif
#ifdef F_SETLKW
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "F_SETLKW"),
        ctx->fromInteger(F_SETLKW));
#endif
#ifdef F_GETOWN
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "F_GETOWN"),
        ctx->fromInteger(F_GETOWN));
#endif
#ifdef F_SETOWN
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "F_SETOWN"),
        ctx->fromInteger(F_SETOWN));
#endif
#ifdef F_GETSIG
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "F_GETSIG"),
        ctx->fromInteger(F_GETSIG));
#endif
#ifdef F_SETSIG
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "F_SETSIG"),
        ctx->fromInteger(F_SETSIG));
#endif
#ifdef F_NOTIFY
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "F_NOTIFY"),
        ctx->fromInteger(F_NOTIFY));
#endif
#ifdef FASYNC
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "FASYNC"),
        ctx->fromInteger(FASYNC));
#endif
#ifdef O_NONBLOCK
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "O_NONBLOCK"),
        ctx->fromInteger(O_NONBLOCK));
#endif
#ifdef O_NDELAY
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "O_NDELAY"),
        ctx->fromInteger(O_NDELAY));
#endif
#ifdef O_APPEND
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "O_APPEND"),
        ctx->fromInteger(O_APPEND));
#endif
#ifdef O_SYNC
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "O_SYNC"),
        ctx->fromInteger(O_SYNC));
#endif
#ifdef O_ASYNC
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "O_ASYNC"),
        ctx->fromInteger(O_ASYNC));
#endif
    // flock operations
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "LOCK_SH"),
        ctx->fromInteger(LOCK_SH));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "LOCK_EX"),
        ctx->fromInteger(LOCK_EX));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "LOCK_UN"),
        ctx->fromInteger(LOCK_UN));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "LOCK_NB"),
        ctx->fromInteger(LOCK_NB));
#endif

    // __all__/__keys__ for `from fcntl import *`.
    const proto::ProtoList* keys = ctx->newList();
    const char* names[] = {
        "fcntl", "ioctl", "flock", "lockf",
        "F_DUPFD", "F_GETFD", "F_SETFD", "F_GETFL", "F_SETFL", "FD_CLOEXEC",
        "LOCK_SH", "LOCK_EX", "LOCK_UN", "LOCK_NB",
        nullptr
    };
    for (size_t i = 0; names[i]; ++i) {
        keys = keys->appendLast(ctx,
            PythonEnvironment::getInternedString(ctx, names[i])->asObject(ctx));
    }
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__keys__"), keys->asObject(ctx));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__all__"), keys->asObject(ctx));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__name__"),
        PythonEnvironment::getInternedString(ctx, "fcntl")->asObject(ctx));

    return mod;
}

} // namespace fcntl_module
} // namespace protoPython
