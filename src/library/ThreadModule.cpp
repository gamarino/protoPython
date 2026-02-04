#include <protoPython/ThreadModule.h>
#include <protoPython/ExecutionEngine.h>
#include <protoCore.h>
#include <thread>
#include <mutex>
#include <cstdint>

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif
#if defined(__linux__)
#include <sys/syscall.h>
#endif

namespace protoPython {
namespace thread_module {

/** Return current OS thread id (TID on Linux, hash of thread::id elsewhere). */
static long long current_thread_id() {
#if defined(__linux__)
    return static_cast<long long>(syscall(SYS_gettid));
#else
    return static_cast<long long>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}

/** Return current process id (PID). */
static long long current_process_id() {
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    return static_cast<long long>(getpid());
#else
    return static_cast<long long>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}

static void mutex_finalizer(void* ptr) {
    delete static_cast<std::mutex*>(ptr);
}

/** Bootstrap run in the new ProtoThread: args = [callable, arg0, arg1, ...]. */
static const proto::ProtoObject* thread_bootstrap(
    proto::ProtoContext* context,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (!args || args->getSize(context) < 1) return PROTO_NONE;
    const proto::ProtoObject* callable = args->getAt(context, 0);
    const proto::ProtoList* argList = context->newList();
    for (unsigned long i = 1; i < args->getSize(context); ++i)
        argList = argList->appendLast(context, args->getAt(context, static_cast<int>(i)));
    return protoPython::invokePythonCallable(context, callable, argList);
}

static const proto::ProtoObject* py_start_new_thread(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* callable = posArgs->getAt(ctx, 0);
    /* Build [callable, ...args]. If second arg is a tuple, unpack it; else use (posArgs[1], ...). */
    const proto::ProtoList* argsForThread = ctx->newList()->appendLast(ctx, callable);
    if (posArgs->getSize(ctx) >= 2) {
        const proto::ProtoObject* second = posArgs->getAt(ctx, 1);
        if (second->asTuple(ctx)) {
            const proto::ProtoTuple* tup = second->asTuple(ctx);
            for (unsigned long i = 0; i < tup->getSize(ctx); ++i)
                argsForThread = argsForThread->appendLast(ctx, tup->getAt(ctx, static_cast<int>(i)));
        } else {
            argsForThread = argsForThread->appendLast(ctx, second);
        }
    }
    const proto::ProtoString* name = proto::ProtoString::fromUTF8String(ctx, "thread");
    const proto::ProtoThread* thread = ctx->space->newThread(ctx, name, thread_bootstrap, argsForThread, nullptr);
    return ctx->fromExternalPointer(const_cast<proto::ProtoThread*>(thread), nullptr);
}

/** Write current pid/tid to C stderr for verification (does not depend on Python print). */
static const proto::ProtoObject* py_log_thread_ident(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    const char* label = "thread";
    if (posArgs && posArgs->getSize(ctx) >= 1 && posArgs->getAt(ctx, 0)->isString(ctx)) {
        std::string s;
        posArgs->getAt(ctx, 0)->asString(ctx)->toUTF8String(ctx, s);
        label = s.empty() ? "thread" : s.c_str();
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_allocate_lock(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*posArgs*/,
    const proto::ProtoSparseList* /*kwargs*/) {
    return ctx->fromExternalPointer(new std::mutex, mutex_finalizer);
}

static const proto::ProtoObject* py_lock_acquire(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 1) return PROTO_FALSE;
    const proto::ProtoObject* handle = posArgs->getAt(ctx, 0);
    const proto::ProtoExternalPointer* ext = handle->asExternalPointer(ctx);
    if (!ext) return PROTO_FALSE;
    std::mutex* m = static_cast<std::mutex*>(ext->getPointer(ctx));
    if (!m) return PROTO_FALSE;
    bool blocking = true;
    if (posArgs->getSize(ctx) >= 2)
        blocking = posArgs->getAt(ctx, 1) != PROTO_FALSE;
    if (blocking) {
        m->lock();
        return PROTO_TRUE;
    }
    return m->try_lock() ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_lock_release(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* handle = posArgs->getAt(ctx, 0);
    const proto::ProtoExternalPointer* ext = handle->asExternalPointer(ctx);
    if (!ext) return PROTO_NONE;
    std::mutex* m = static_cast<std::mutex*>(ext->getPointer(ctx));
    if (m) m->unlock();
    return PROTO_NONE;
}

static const proto::ProtoObject* py_get_ident(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*posArgs*/,
    const proto::ProtoSparseList* /*kwargs*/) {
    return ctx->fromInteger(current_thread_id());
}

static const proto::ProtoObject* py_getpid(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*posArgs*/,
    const proto::ProtoSparseList* /*kwargs*/) {
    return ctx->fromInteger(current_process_id());
}

static const proto::ProtoObject* py_join_thread(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* handle = posArgs->getAt(ctx, 0);
    const proto::ProtoExternalPointer* ext = handle->asExternalPointer(ctx);
    if (!ext) return PROTO_NONE;
    proto::ProtoThread* thread = static_cast<proto::ProtoThread*>(ext->getPointer(ctx));
    if (thread) thread->join(ctx);
    return PROTO_NONE;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(true);
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "start_new_thread"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_start_new_thread));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "join_thread"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_join_thread));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "get_ident"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_get_ident));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "getpid"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_getpid));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "log_thread_ident"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_log_thread_ident));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "allocate_lock"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_allocate_lock));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "_lock_acquire"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_lock_acquire));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "_lock_release"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_lock_release));
    return mod;
}

} // namespace thread_module
} // namespace protoPython
