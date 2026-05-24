#include <protoPython/ThreadModule.h>
#include <protoPython/ExecutionEngine.h>
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <cstdlib>
#include <cstdint>

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif
#if defined(__linux__)
#include <sys/syscall.h>
#endif

namespace protoPython {
namespace thread_module {

// CPython _thread.lock has no ownership: thread A may acquire and
// thread B may release, mirroring a binary semaphore.  This is what
// threading.Condition.wait/notify relies on — the waiter lock is
// acquired by the waiting thread and released by the notifying
// thread.  std::mutex / std::timed_mutex are owner-bound: a release
// from a different thread is undefined behaviour.  Implement the
// non-owner-bound semantics manually via a condition variable.
struct LockData {
    std::mutex internal_m;
    std::condition_variable cv;
    bool taken = false;
    std::atomic<bool> held{false};
};

struct RLockData {
    std::recursive_timed_mutex m;
    std::atomic<int> count{0};
    // Owner thread id at the moment of acquisition. CPython's RLock
    // raises RuntimeError on release-from-non-owner; protoPython
    // previously had no ownership tracking so a misused RLock was
    // silent UB. 0 means "not currently held by anyone".
    std::atomic<long long> owner{0};
};

/** Return current OS thread id. Forward-declared so RLock owner
 *  tracking in acquire/release can call it before the definition. */
static long long current_thread_id();

static void mutex_finalizer(void* ptr) {
    delete static_cast<LockData*>(ptr);
}

static void rmutex_finalizer(void* ptr) {
    delete static_cast<RLockData*>(ptr);
}

static const proto::ProtoObject* py_lock_acquire(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    const proto::ProtoObject* handle = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_handle"));
    if (!handle || handle == PROTO_NONE) {
        if (posArgs && posArgs->getSize(ctx) >= 1) handle = posArgs->getAt(ctx, 0);
        else return PROTO_FALSE;
    }
    const proto::ProtoExternalPointer* ext = handle->asExternalPointer(ctx);
    if (!ext) return PROTO_FALSE;
    LockData* ld = static_cast<LockData*>(ext->getPointer(ctx));
    if (!ld) return PROTO_FALSE;
    bool blocking = true;
    double timeout = -1.0;
    if (posArgs && posArgs->getSize(ctx) >= 1)
        blocking = posArgs->getAt(ctx, 0) != PROTO_FALSE;
    if (posArgs && posArgs->getSize(ctx) >= 2) {
        const proto::ProtoObject* tArg = posArgs->getAt(ctx, 1);
        if (tArg) {
            if (tArg->isDouble(ctx)) timeout = tArg->asDouble(ctx);
            else if (tArg->isInteger(ctx)) timeout = (double)tArg->asLong(ctx);
            else if (tArg == PROTO_TRUE) timeout = 1.0;
            else if (tArg == PROTO_FALSE) timeout = 0.0;
        }
    }

    std::unique_lock<std::mutex> lk(ld->internal_m);
    if (!blocking) {
        if (ld->taken) return PROTO_FALSE;
        ld->taken = true;
        ld->held = true;
        return PROTO_TRUE;
    }
    if (timeout < 0) {
        ld->cv.wait(lk, [&]{ return !ld->taken; });
    } else {
        auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(
            std::chrono::duration<double>(timeout));
        if (!ld->cv.wait_for(lk, dur, [&]{ return !ld->taken; })) {
            return PROTO_FALSE;
        }
    }
    ld->taken = true;
    ld->held = true;
    return PROTO_TRUE;
}

static const proto::ProtoObject* py_lock_release(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    const proto::ProtoObject* handle = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_handle"));
    if (!handle || handle == PROTO_NONE) {
        if (posArgs && posArgs->getSize(ctx) >= 1) handle = posArgs->getAt(ctx, 0);
        else return PROTO_NONE;
    }
    const proto::ProtoExternalPointer* ext = handle->asExternalPointer(ctx);
    if (!ext) return PROTO_NONE;
    LockData* ld = static_cast<LockData*>(ext->getPointer(ctx));
    if (ld) {
        std::unique_lock<std::mutex> lk(ld->internal_m);
        ld->taken = false;
        ld->held = false;
        lk.unlock();
        ld->cv.notify_one();
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_rlock_acquire(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    const proto::ProtoObject* handle = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_handle"));
    if (!handle || handle == PROTO_NONE) {
        if (posArgs && posArgs->getSize(ctx) >= 1) handle = posArgs->getAt(ctx, 0);
        else return PROTO_FALSE;
    }
    const proto::ProtoExternalPointer* ext = handle->asExternalPointer(ctx);
    if (!ext) return PROTO_FALSE;
    RLockData* ld = static_cast<RLockData*>(ext->getPointer(ctx));
    if (!ld) return PROTO_FALSE;
    bool blocking = true;
    double timeout = -1.0;
    if (posArgs && posArgs->getSize(ctx) >= 1)
        blocking = posArgs->getAt(ctx, 0) != PROTO_FALSE;
    if (posArgs && posArgs->getSize(ctx) >= 2) {
        const proto::ProtoObject* tArg = posArgs->getAt(ctx, 1);
        if (tArg) {
            if (tArg->isDouble(ctx)) timeout = tArg->asDouble(ctx);
            else if (tArg->isInteger(ctx)) timeout = (double)tArg->asLong(ctx);
            else if (tArg == PROTO_TRUE) timeout = 1.0;
            else if (tArg == PROTO_FALSE) timeout = 0.0;
        }
    }

    auto recordAcquired = [&]() {
        ld->count++;
        ld->owner.store(current_thread_id(), std::memory_order_release);
    };

    if (!blocking) {
        if (ld->m.try_lock()) { recordAcquired(); return PROTO_TRUE; }
        return PROTO_FALSE;
    }
    if (timeout < 0) {
        ld->m.lock();
        recordAcquired();
        return PROTO_TRUE;
    }
    auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::duration<double>(timeout));
    if (ld->m.try_lock_for(dur)) {
        recordAcquired();
        return PROTO_TRUE;
    }
    return PROTO_FALSE;
}

static const proto::ProtoObject* py_rlock_release(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    const proto::ProtoObject* handle = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_handle"));
    if (!handle || handle == PROTO_NONE) {
        if (posArgs && posArgs->getSize(ctx) >= 1) handle = posArgs->getAt(ctx, 0);
        else return PROTO_NONE;
    }
    const proto::ProtoExternalPointer* ext = handle->asExternalPointer(ctx);
    if (!ext) return PROTO_NONE;
    RLockData* ld = static_cast<RLockData*>(ext->getPointer(ctx));
    if (!ld) return PROTO_NONE;

    // CPython: releasing an RLock from a thread other than the
    // current owner raises RuntimeError("cannot release un-acquired
    // lock"). The previous code blindly decrement-and-unlocked, so a
    // cross-thread release was silent UB (the recursive_mutex would
    // throw or assert under -O0, but produced nothing under -O3).
    long long me = current_thread_id();
    long long current_owner = ld->owner.load(std::memory_order_acquire);
    if (current_owner == 0 || current_owner != me) {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        if (env) env->raiseRuntimeError(ctx, "cannot release un-acquired lock");
        return nullptr;
    }
    int new_count = --ld->count;
    if (new_count == 0) {
        ld->owner.store(0, std::memory_order_release);
    }
    ld->m.unlock();
    return PROTO_NONE;
}

/** Return current OS thread id (TID on Linux, hash of thread::id elsewhere). */
static long long current_thread_id() {
#if defined(__linux__)
    return static_cast<long long>(syscall(SYS_gettid));
#else
    return static_cast<long long>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}

// Captured once at module initialization — the OS-level thread ID of the main thread.
static long long g_main_thread_id = current_thread_id();

/** Return current process id (PID). */
static long long current_process_id() {
#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
    return static_cast<long long>(getpid());
#else
    return static_cast<long long>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}

static const proto::ProtoObject* py_rlock_locked(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*posArgs*/,
    const proto::ProtoSparseList* /*kwargs*/) {
    const proto::ProtoObject* handle = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_handle"));
    if (!handle || handle == PROTO_NONE) return PROTO_FALSE;
    const proto::ProtoExternalPointer* ext = handle->asExternalPointer(ctx);
    if (!ext) return PROTO_FALSE;
    RLockData* ld = static_cast<RLockData*>(ext->getPointer(ctx));
    if (!ld) return PROTO_FALSE;
    return ld->count > 0 ? PROTO_TRUE : PROTO_FALSE;
}
static const proto::ProtoObject* py_lock_enter(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwargs) {
    return py_lock_acquire(ctx, self, parentLink, nullptr, nullptr);
}

static const proto::ProtoObject* py_lock_exit(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwargs) {
    py_lock_release(ctx, self, parentLink, nullptr, nullptr);
    return PROTO_NONE;
}

static const proto::ProtoObject* py_rlock_enter(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwargs) {
    return py_rlock_acquire(ctx, self, parentLink, nullptr, nullptr);
}

static const proto::ProtoObject* py_rlock_exit(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwargs) {
    py_rlock_release(ctx, self, parentLink, nullptr, nullptr);
    return PROTO_NONE;
}

/** Diagnostic: count distinct OS threads that enter thread_bootstrap (PROTO_THREAD_DIAG=1). Lock-free. */
static std::atomic<int> s_bootstrapTidCount{0};
static std::atomic<bool> s_bootstrapFirstLogged{false};
static std::once_flag s_bootstrapDiagOnce;
static void diagBootstrapTid() {
}

static const proto::ProtoObject* thread_bootstrap(
    proto::ProtoContext* context,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* /*kwargs*/) {
    diagBootstrapTid();
    if (!args || args->getSize(context) < 1) return PROTO_NONE;
    unsigned long callableIdx = 0;
    protoPython::PythonEnvironment* env = nullptr;
    if (args->getSize(context) >= 2) {
        const proto::ProtoObject* first = args->getAt(context, 0);
        const proto::ProtoExternalPointer* ep = first ? first->asExternalPointer(context) : nullptr;
        if (ep) {
            env = static_cast<protoPython::PythonEnvironment*>(ep->getPointer(context));
            if (env) {
                callableIdx = 1;
            }
        }
    }

    const proto::ProtoObject* result = nullptr;
    {
        protoPython::PythonEnvironment::ContextScope scope(env, context);
        const proto::ProtoObject* callable = args->getAt(context, static_cast<int>(callableIdx));
        const proto::ProtoList* argList = context->newList();
        for (unsigned long i = callableIdx + 1; i < args->getSize(context); ++i)
            argList = argList->appendLast(context, args->getAt(context, static_cast<int>(i)));
        result = protoPython::invokePythonCallable(context, callable, argList, nullptr);
    }
    return result;
}

static const proto::ProtoObject* py_start_new_thread(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* callable = posArgs->getAt(ctx, 0);
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(ctx);
    const proto::ProtoList* argsForThread = ctx->newList();
    if (env)
        argsForThread = argsForThread->appendLast(ctx, ctx->fromExternalPointer(env, nullptr));
    argsForThread = argsForThread->appendLast(ctx, callable);
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
    const proto::ProtoString* name = proto::ProtoString::createSymbol(ctx, "thread");
    const proto::ProtoThread* thread = ctx->space->newThread(ctx, name, thread_bootstrap, argsForThread, nullptr);
    return ctx->fromInteger(reinterpret_cast<uintptr_t>(thread));
}

// _thread._log_thread_ident — diagnostic hook used by threading.py to
// log the spawning thread's identity. Previous body was a literal
// `return PROTO_NONE; return PROTO_NONE;` (dead second statement).
// We now log to stderr only when PROTO_THREAD_DIAG is set so tests
// stay quiet by default but operators have a hook to trace
// spawn-time identity when debugging.
static const proto::ProtoObject* py_log_thread_ident(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*posArgs*/,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (protoPython::diagThreadEnabled()) {
        fprintf(stderr, "[thread] ident=%lld\n",
            static_cast<long long>(current_thread_id()));
        fflush(stderr);
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_lock_locked(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*posArgs*/,
    const proto::ProtoSparseList* /*kwargs*/) {
    const proto::ProtoObject* handle = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_handle"));
    if (!handle || handle == PROTO_NONE) return PROTO_FALSE;
    const proto::ProtoExternalPointer* ext = handle->asExternalPointer(ctx);
    if (!ext) return PROTO_FALSE;
    LockData* ld = static_cast<LockData*>(ext->getPointer(ctx));
    if (!ld) return PROTO_FALSE;
    return ld->held ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_allocate_lock(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*posArgs*/,
    const proto::ProtoSparseList* /*kwargs*/) {
    const proto::ProtoObject* lockProt = self ? self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_lockProt")) : nullptr;
    proto::ProtoObject* obj = lockProt ? const_cast<proto::ProtoObject*>(lockProt->newChild(ctx, true)) : const_cast<proto::ProtoObject*>(ctx->newObject(false));
    obj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_handle"), 
        ctx->fromExternalPointer(new LockData, mutex_finalizer));
    return obj;
}

static const proto::ProtoObject* py_allocate_rlock(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* /*posArgs*/,
    const proto::ProtoSparseList* /*kwargs*/) {
    const proto::ProtoObject* rlockProt = self ? self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_rlockProt")) : nullptr;
    proto::ProtoObject* obj = rlockProt ? const_cast<proto::ProtoObject*>(rlockProt->newChild(ctx, true)) : const_cast<proto::ProtoObject*>(ctx->newObject(false));
    obj->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_handle"), 
        ctx->fromExternalPointer(new RLockData, rmutex_finalizer));
    return obj;
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

static const proto::ProtoObject* py_is_main_interpreter(
    proto::ProtoContext* ctx,
    const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return PROTO_TRUE; // For now
}

static const proto::ProtoObject* py_get_main_thread_ident(
    proto::ProtoContext* ctx,
    const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return ctx->fromInteger(g_main_thread_id);
}

// _ThreadHandle methods.  The handle is a mutable Python object with two
// internal slots:
//   _proto_thread  — ExternalPointer wrapping a proto::ProtoThread*
//   ident          — integer (the OS-level identity of the underlying thread)
// A handle is "done" when the underlying ProtoThread is no longer registered
// in space->threads (which is how protoCore tracks running threads).

static const proto::ProtoObject* py_handle_is_done(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    if (!self) return PROTO_TRUE;
    const proto::ProtoString* doneAttr =
        proto::ProtoString::createSymbol(ctx, "_done");
    const proto::ProtoObject* doneFlag = self->getAttribute(ctx, doneAttr);
    if (doneFlag == PROTO_TRUE) return PROTO_TRUE;
    const proto::ProtoString* ptrAttr =
        proto::ProtoString::createSymbol(ctx, "_proto_thread");
    const proto::ProtoObject* handlePtr = self->getAttribute(ctx, ptrAttr);
    if (!handlePtr || handlePtr == PROTO_NONE) return PROTO_TRUE;
    const proto::ProtoExternalPointer* ext = handlePtr->asExternalPointer(ctx);
    if (!ext) return PROTO_TRUE;
    proto::ProtoThread* thread =
        static_cast<proto::ProtoThread*>(ext->getPointer(ctx));
    if (!thread) return PROTO_TRUE;
    unsigned long threadId = reinterpret_cast<uintptr_t>(thread);
    const proto::ProtoSparseList* threads = ctx->space->threads;
    if (!threads) return PROTO_TRUE;
    return (threads->getAt(ctx, threadId) == PROTO_NONE) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_handle_join(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    if (!self) return PROTO_NONE;
    const proto::ProtoString* ptrAttr =
        proto::ProtoString::createSymbol(ctx, "_proto_thread");
    const proto::ProtoObject* handlePtr = self->getAttribute(ctx, ptrAttr);
    if (!handlePtr || handlePtr == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoExternalPointer* ext = handlePtr->asExternalPointer(ctx);
    if (!ext) return PROTO_NONE;
    proto::ProtoThread* thread =
        static_cast<proto::ProtoThread*>(ext->getPointer(ctx));
    if (thread) thread->join(ctx);
    return PROTO_NONE;
}

static const proto::ProtoObject* py_handle_set_done(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    if (!self) return PROTO_NONE;
    const proto::ProtoString* doneAttr =
        proto::ProtoString::createSymbol(ctx, "_done");
    self->setAttribute(ctx, doneAttr, PROTO_TRUE);
    return PROTO_NONE;
}

// Build a fresh _ThreadHandle Python-visible object.  Optional positional
// arg interpreted as the initial ident (0 = unstarted, set by start_joinable_thread).
static const proto::ProtoObject* py_make_thread_handle(
    proto::ProtoContext* ctx,
    const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    proto::ProtoObject* handle =
        const_cast<proto::ProtoObject*>(ctx->newObject(true));
    long initialIdent = 0;
    if (posArgs && posArgs->getSize(ctx) >= 1) {
        const proto::ProtoObject* a0 = posArgs->getAt(ctx, 0);
        if (a0 && a0->isInteger(ctx)) initialIdent = a0->asLong(ctx);
    }
    handle = const_cast<proto::ProtoObject*>(handle->setAttribute(
        ctx, proto::ProtoString::createSymbol(ctx, "ident"),
        ctx->fromInteger(initialIdent)));
    handle = const_cast<proto::ProtoObject*>(handle->setAttribute(
        ctx, proto::ProtoString::createSymbol(ctx, "_proto_thread"), PROTO_NONE));
    handle = const_cast<proto::ProtoObject*>(handle->setAttribute(
        ctx, proto::ProtoString::createSymbol(ctx, "_done"), PROTO_FALSE));
    handle = const_cast<proto::ProtoObject*>(handle->setAttribute(
        ctx, proto::ProtoString::createSymbol(ctx, "is_done"),
        ctx->fromMethod(handle, py_handle_is_done)));
    handle = const_cast<proto::ProtoObject*>(handle->setAttribute(
        ctx, proto::ProtoString::createSymbol(ctx, "join"),
        ctx->fromMethod(handle, py_handle_join)));
    handle = const_cast<proto::ProtoObject*>(handle->setAttribute(
        ctx, proto::ProtoString::createSymbol(ctx, "_set_done"),
        ctx->fromMethod(handle, py_handle_set_done)));
    return handle;
}

// _thread.start_joinable_thread(target, handle=<existing handle>, daemon=<bool>)
//
// Spawns a real OS thread (via protoCore's ProtoSpace::newThread) running
// thread_bootstrap → target.  The provided handle (or a freshly created one
// when the kwarg is missing) gets its `ident` and `_proto_thread` slots
// populated so subsequent .is_done() / .join() calls observe the running
// thread.  Returns the handle.
static const proto::ProtoObject* py_start_joinable_thread(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* kwargs) {
    if (!posArgs || posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* callable = posArgs->getAt(ctx, 0);

    // Resolve handle (kwarg "handle"; otherwise build a fresh one).
    const proto::ProtoObject* handle = nullptr;
    if (kwargs) {
        const proto::ProtoString* handleKey =
            proto::ProtoString::createSymbol(ctx, "handle");
        unsigned long handleHash = reinterpret_cast<uintptr_t>(handleKey);
        if (kwargs->has(ctx, handleHash)) {
            handle = kwargs->getAt(ctx, handleHash);
        }
    }
    if (!handle || handle == PROTO_NONE) {
        handle = py_make_thread_handle(ctx, self, nullptr, ctx->newList(), nullptr);
    }

    // Build the args list passed to thread_bootstrap: [env_ptr, callable].
    protoPython::PythonEnvironment* env =
        protoPython::PythonEnvironment::fromContext(ctx);
    const proto::ProtoList* argsForThread = ctx->newList();
    if (env)
        argsForThread = argsForThread->appendLast(
            ctx, ctx->fromExternalPointer(env, nullptr));
    argsForThread = argsForThread->appendLast(ctx, callable);

    // Spawn.
    const proto::ProtoString* name = proto::ProtoString::createSymbol(ctx, "thread");
    const proto::ProtoThread* thread = ctx->space->newThread(
        ctx, name, thread_bootstrap, argsForThread, nullptr);

    // Populate handle with the live thread reference and its ident.
    proto::ProtoObject* mhandle = const_cast<proto::ProtoObject*>(handle);
    mhandle->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_proto_thread"),
                          ctx->fromExternalPointer(const_cast<proto::ProtoThread*>(thread), nullptr));
    mhandle->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "ident"),
                          ctx->fromInteger(reinterpret_cast<uintptr_t>(thread)));
    return handle;
}

static const proto::ProtoObject* py_daemon_threads_allowed(
    proto::ProtoContext* ctx,
    const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return PROTO_TRUE;
}

// _thread._shutdown — invoked by threading.py at interpreter exit.
// CPython joins every non-daemon thread so they finish their work
// (e.g. flushing buffered output) before main returns. Previously
// this was a no-op, so background workers spawned via
// `threading.Thread(target=…)` without explicit `.join()` would
// be killed mid-write at process exit and silently lose data.
//
// We don't track daemon-vs-non-daemon yet (CPython exposes it via
// the .daemon Python attribute, but protoPython's
// _thread.start_new_thread predates that gate); for now we join
// every live thread we can see in `space->threads`. The known cost
// is that a daemon-style worker that wants to be killed on exit
// will block shutdown — but that mirrors the conservative choice
// CPython makes when daemon flags aren't set (default is
// non-daemon).
static const proto::ProtoObject* py_shutdown(
    proto::ProtoContext* ctx,
    const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    if (!ctx || !ctx->space || !ctx->space->threads) return PROTO_NONE;
    const proto::ProtoSparseListIterator* it = ctx->space->threads->getIterator(ctx);
    while (it && it->hasNext(ctx)) {
        const proto::ProtoObject* val = it->nextValue(ctx);
        it = const_cast<proto::ProtoSparseListIterator*>(it)->advance(ctx);
        if (!val || val == PROTO_NONE) continue;
        const proto::ProtoExternalPointer* ext = val->asExternalPointer(ctx);
        proto::ProtoThread* t = nullptr;
        if (ext) {
            t = static_cast<proto::ProtoThread*>(ext->getPointer(ctx));
        }
        if (t) t->join(ctx);
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_is_alive(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 1) return PROTO_FALSE;
    const proto::ProtoObject* handle = posArgs->getAt(ctx, 0);
    const proto::ProtoExternalPointer* ext = handle->asExternalPointer(ctx);
    if (!ext) return PROTO_FALSE;
    proto::ProtoThread* thread = static_cast<proto::ProtoThread*>(ext->getPointer(ctx));
    if (!thread) return PROTO_FALSE;
    
    unsigned long threadId = reinterpret_cast<uintptr_t>(thread);
    const proto::ProtoSparseList* threads = ctx->space->threads;
    if (threads && threads->getAt(ctx, threadId) != PROTO_NONE)
        return PROTO_TRUE;
    
    return PROTO_FALSE;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(false);
    
    const proto::ProtoObject* lockProt = ctx->newObject(false);
    lockProt = lockProt->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "acquire"), 
        ctx->fromMethod(nullptr, py_lock_acquire));
    lockProt = lockProt->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "release"), 
        ctx->fromMethod(nullptr, py_lock_release));
    lockProt = lockProt->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "locked"), 
        ctx->fromMethod(nullptr, py_lock_locked));
    lockProt = lockProt->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__enter__"), 
        ctx->fromMethod(nullptr, py_lock_enter));
    lockProt = lockProt->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__exit__"), 
        ctx->fromMethod(nullptr, py_lock_exit));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_lockProt"), lockProt);

    const proto::ProtoObject* rlockProt = ctx->newObject(false);
    rlockProt = rlockProt->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "acquire"), 
        ctx->fromMethod(nullptr, py_rlock_acquire));
    rlockProt = rlockProt->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "release"), 
        ctx->fromMethod(nullptr, py_rlock_release));
    rlockProt = rlockProt->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "locked"), 
        ctx->fromMethod(nullptr, py_rlock_locked));
    rlockProt = rlockProt->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__enter__"), 
        ctx->fromMethod(nullptr, py_rlock_enter));
    rlockProt = rlockProt->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "__exit__"), 
        ctx->fromMethod(nullptr, py_rlock_exit));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_rlockProt"), rlockProt);

    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "start_new_thread"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_start_new_thread));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "join_thread"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_join_thread));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "is_alive"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_is_alive));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "get_ident"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_get_ident));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "getpid"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_getpid));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "log_thread_ident"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_log_thread_ident));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "allocate_lock"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_allocate_lock));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_lock_acquire"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_lock_acquire));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_lock_release"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_lock_release));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "allocate_rlock"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_allocate_rlock));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "RLock"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_allocate_rlock));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_rlock_acquire"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_rlock_acquire));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_rlock_release"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_rlock_release));

    // threading.py dependencies
    const proto::ProtoObject* pyType = protoPython::PythonEnvironment::fromContext(ctx) ? protoPython::PythonEnvironment::fromContext(ctx)->lookupName("type") : nullptr;
    const proto::ProtoObject* pyException = protoPython::PythonEnvironment::fromContext(ctx) ? protoPython::PythonEnvironment::fromContext(ctx)->lookupName("Exception") : nullptr;

    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "LockType"), ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_allocate_lock));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "error"), pyException ? pyException : PROTO_NONE);
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "TIMEOUT_MAX"), ctx->fromDouble(9223372036.0));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_is_main_interpreter"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_is_main_interpreter));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_get_main_thread_ident"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_get_main_thread_ident));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "start_joinable_thread"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_start_joinable_thread));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "daemon_threads_allowed"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_daemon_threads_allowed));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_shutdown"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_shutdown));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_make_thread_handle"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_make_thread_handle));
    // Provide a callable for _ThreadHandle
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_ThreadHandle"), ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_make_thread_handle));

    auto py_thread_count = [](proto::ProtoContext* c, const proto::ProtoObject*, const proto::ParentLink*, const proto::ProtoList*, const proto::ProtoSparseList*) -> const proto::ProtoObject* {
        return c->fromInteger(c->space->runningThreads.load());
    };
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_count"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_thread_count));

    auto py_get_handle = [](proto::ProtoContext* c, const proto::ProtoObject*, const proto::ParentLink*, const proto::ProtoList* args, const proto::ProtoSparseList*) -> const proto::ProtoObject* {
        if (!args || args->getSize(c) < 1) return PROTO_NONE;
        unsigned long tid = (unsigned long)args->getAt(c, 0)->asLong(c);
        const proto::ProtoSparseList* threads = c->space->threads;
        if (!threads) return PROTO_NONE;
        return threads->getAt(c, tid);
    };
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_get_thread_handle"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_get_handle));

    return mod;
}

} // namespace thread_module
} // namespace protoPython
