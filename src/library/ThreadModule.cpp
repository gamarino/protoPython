#include <protoPython/ThreadModule.h>
#include <protoPython/ExecutionEngine.h>
#include <protoPython/PythonEnvironment.h>
#include <protoPython/Debugger.h>
#include <protoCore.h>
#include <iostream>
#include <thread>
#include <mutex>
#include <atomic>
#include <vector>
#include <cstdlib>
#include <cstdint>

#if defined(__linux__) || defined(__unix__) || defined(__APPLE__)
#include <unistd.h>
#endif
#if defined(__linux__)
#include <sys/syscall.h>
#endif
#if defined(_WIN32)
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#include <windows.h>
#include <process.h>
#endif
#include "PosixCompat.h"

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
    const proto::ProtoArguments* posArgs,
    const proto::ProtoKeywordArguments* /*kwargs*/) {
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

    {
        std::lock_guard<std::mutex> lk(ld->internal_m);
        if (!ld->taken) {
            ld->taken = true;
            ld->held = true;
            return PROTO_TRUE;
        }
        if (!blocking) return PROTO_FALSE;
    }
    // Contended: a `threading.Lock.acquire()` blocks the calling thread
    // on a foreign condition variable that the protoCore GC cannot
    // inspect, so the wait runs inside an unmanaged region (otherwise a
    // GC cycle would stall behind every waiting thread). internal_m must
    // be released BEFORE the region ends: returnFromUnmanaged() parks
    // while a stop-the-world is in progress, and parking with
    // internal_m held blocks every managed thread that then calls
    // release() on this lock — the GC waits for those threads to park
    // and they wait for the mutex.
    bool acquired;
    {
        proto::ProtoContext::UnmanagedScope u(ctx);
        std::unique_lock<std::mutex> lk(ld->internal_m);
        if (timeout < 0) {
            ld->cv.wait(lk, [&]{ return !ld->taken; });
            acquired = true;
        } else {
            auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::duration<double>(timeout));
            acquired = ld->cv.wait_for(lk, dur, [&]{ return !ld->taken; });
        }
        if (acquired) {
            ld->taken = true;
            ld->held = true;
        }
    }
    return acquired ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_lock_release(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoArguments* posArgs,
    const proto::ProtoKeywordArguments* /*kwargs*/) {
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
    const proto::ProtoArguments* posArgs,
    const proto::ProtoKeywordArguments* /*kwargs*/) {
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

    if (ld->m.try_lock()) { recordAcquired(); return PROTO_TRUE; }
    if (!blocking) return PROTO_FALSE;

    // Contended: block inside an unmanaged region. The owner may be parked
    // at a GC safepoint while holding the RLock; a waiter blocked in lock()
    // as a managed thread would never park, so the stop-the-world quorum
    // could never be reached (threading's _active_limbo_lock is an RLock
    // taken by every Thread start and exit).
    bool acquired;
    {
        proto::ProtoContext::UnmanagedScope u(ctx);
        if (timeout < 0) {
            ld->m.lock();
            acquired = true;
        } else {
            auto dur = std::chrono::duration_cast<std::chrono::nanoseconds>(
                std::chrono::duration<double>(timeout));
            acquired = ld->m.try_lock_for(dur);
        }
    }
    if (!acquired) return PROTO_FALSE;
    recordAcquired();
    return PROTO_TRUE;
}

static const proto::ProtoObject* py_rlock_release(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoArguments* posArgs,
    const proto::ProtoKeywordArguments* /*kwargs*/) {
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
#elif defined(_WIN32)
    return static_cast<long long>(GetCurrentThreadId());
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
#elif defined(_WIN32)
    return static_cast<long long>(_getpid());
#else
    return static_cast<long long>(std::hash<std::thread::id>{}(std::this_thread::get_id()));
#endif
}

static const proto::ProtoObject* py_rlock_locked(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoArguments* /*posArgs*/,
    const proto::ProtoKeywordArguments* /*kwargs*/) {
    const proto::ProtoObject* handle = self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_handle"));
    if (!handle || handle == PROTO_NONE) return PROTO_FALSE;
    const proto::ProtoExternalPointer* ext = handle->asExternalPointer(ctx);
    if (!ext) return PROTO_FALSE;
    RLockData* ld = static_cast<RLockData*>(ext->getPointer(ctx));
    if (!ld) return PROTO_FALSE;
    return ld->count > 0 ? PROTO_TRUE : PROTO_FALSE;
}
// The RLock methods threading.Condition uses when its lock has them, as
// CPython's C RLock does.  Without _is_owned, Condition fell back to
// probing with acquire(False), which a reentrant lock grants to its owner,
// so notify() on a Condition over an RLock (the default) always raised
// "cannot notify on un-acquired lock".
static RLockData* rlockData(proto::ProtoContext* ctx, const proto::ProtoObject* self) {
    const proto::ProtoObject* handle = self ? self->getAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_handle")) : nullptr;
    if (!handle || handle == PROTO_NONE) return nullptr;
    const proto::ProtoExternalPointer* ext = handle->asExternalPointer(ctx);
    return ext ? static_cast<RLockData*>(ext->getPointer(ctx)) : nullptr;
}

static bool rlockOwnedByMe(const RLockData* ld) {
    return ld->count > 0 && ld->owner.load(std::memory_order_acquire) == current_thread_id();
}

static const proto::ProtoObject* py_rlock_is_owned(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoArguments*, const proto::ProtoKeywordArguments*) {
    RLockData* ld = rlockData(ctx, self);
    return ld && rlockOwnedByMe(ld) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_rlock_recursion_count(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoArguments*, const proto::ProtoKeywordArguments*) {
    RLockData* ld = rlockData(ctx, self);
    return ctx->fromInteger(ld && rlockOwnedByMe(ld) ? ld->count.load() : 0);
}

// _release_save(): release every level of the calling thread's hold and
// answer (count, owner) for _acquire_restore (Condition.wait).
static const proto::ProtoObject* py_rlock_release_save(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoArguments*, const proto::ProtoKeywordArguments*) {
    RLockData* ld = rlockData(ctx, self);
    if (!ld || !rlockOwnedByMe(ld)) {
        if (PythonEnvironment* env = PythonEnvironment::fromContext(ctx))
            env->raiseRuntimeError(ctx, "cannot release un-acquired lock");
        return nullptr;
    }
    const int count = ld->count.exchange(0);
    const long long owner = ld->owner.exchange(0, std::memory_order_acq_rel);
    for (int k = 0; k < count; ++k) ld->m.unlock();
    std::vector<const proto::ProtoObject*> state{ctx->fromInteger(count), ctx->fromInteger(owner)};
    return ctx->newTuple(state)->asObject(ctx);
}

// _acquire_restore((count, owner)): take the lock again, blocking, at the
// depth _release_save recorded.
static const proto::ProtoObject* py_rlock_acquire_restore(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    RLockData* ld = rlockData(ctx, self);
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoObject* stateObj = posArgs && posArgs->getSize(ctx) == 1 ? posArgs->getAt(ctx, 0) : nullptr;
    const proto::ProtoTuple* state = stateObj ? stateObj->asTuple(ctx) : nullptr;
    if (!ld || !state || state->getSize(ctx) != 2 || !state->getAt(ctx, 0)->isInteger(ctx)) {
        if (env) env->raiseTypeError(ctx, "_acquire_restore() argument must be a (count, owner) tuple");
        return nullptr;
    }
    const long long count = state->getAt(ctx, 0)->asLong(ctx);
    if (!ld->m.try_lock()) {
        // Blocked waiting for another holder: unmanaged, as in acquire().
        proto::ProtoContext::UnmanagedScope u(ctx);
        ld->m.lock();
    }
    for (long long k = 1; k < count; ++k) ld->m.lock();  // reentrant: never blocks
    ld->count = static_cast<int>(count < 1 ? 1 : count);
    ld->owner.store(current_thread_id(), std::memory_order_release);
    return PROTO_NONE;
}

// lock._at_fork_reinit() / RLock._at_fork_reinit(): give the lock a fresh,
// unlocked state, as CPython does in a child process after fork().  The
// state the lock had may belong to a thread that does not exist in the
// child, so it is replaced rather than released; the old state is freed by
// its finalizer.  concurrent.futures.thread and threading register these
// with os.register_at_fork at import time.
static const proto::ProtoObject* py_lock_at_fork_reinit(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoArguments*, const proto::ProtoKeywordArguments*) {
    if (!self || self == PROTO_NONE) return PROTO_NONE;
    const_cast<proto::ProtoObject*>(self)->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_handle"),
        ctx->fromExternalPointer(new LockData, mutex_finalizer));
    return PROTO_NONE;
}

static const proto::ProtoObject* py_rlock_at_fork_reinit(
    proto::ProtoContext* ctx, const proto::ProtoObject* self, const proto::ParentLink*,
    const proto::ProtoArguments*, const proto::ProtoKeywordArguments*) {
    if (!self || self == PROTO_NONE) return PROTO_NONE;
    const_cast<proto::ProtoObject*>(self)->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_handle"),
        ctx->fromExternalPointer(new RLockData, rmutex_finalizer));
    return PROTO_NONE;
}

static const proto::ProtoObject* py_lock_enter(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoArguments* posArgs,
    const proto::ProtoKeywordArguments* kwargs) {
    return py_lock_acquire(ctx, self, parentLink, nullptr, nullptr);
}

static const proto::ProtoObject* py_lock_exit(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoArguments* posArgs,
    const proto::ProtoKeywordArguments* kwargs) {
    py_lock_release(ctx, self, parentLink, nullptr, nullptr);
    return PROTO_NONE;
}

static const proto::ProtoObject* py_rlock_enter(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoArguments* posArgs,
    const proto::ProtoKeywordArguments* kwargs) {
    return py_rlock_acquire(ctx, self, parentLink, nullptr, nullptr);
}

static const proto::ProtoObject* py_rlock_exit(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink* parentLink,
    const proto::ProtoArguments* posArgs,
    const proto::ProtoKeywordArguments* kwargs) {
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
    const proto::ProtoArguments* args,
    const proto::ProtoKeywordArguments* /*kwargs*/) {
    diagBootstrapTid();
#if defined(_WIN32)
    // Invalid C runtime arguments are errors on Python threads (PosixCompat.h).
    protopy_ignore_invalid_parameters();
#endif
    if (!args || args->getSize(context) < 1) return PROTO_NONE;
    // Args layout produced by py_start_new_thread / py_start_joinable_thread:
    //   args[0]   = ExternalPointer(env)              (optional)
    //   args[1]   = spawner's currentGlobals           (optional, sentinel-tagged)
    //   args[N]   = callable                           (required)
    //   args[N+1..] = positional args for callable
    //
    // The globals slot carries the Python-level globals visible at the
    // moment of the spawn so the new OS thread can resolve LOAD_GLOBAL
    // / lookupName from the same module as the spawner.  Without this,
    // protopyc-generated functions (whose body calls
    // env->lookupName("x")) ran in a fresh thread with NULL
    // s_currentGlobals and saw every name as missing — silently
    // raising and dying before any side effect.
    proto::proto_ulong callableIdx = 0;
    protoPython::PythonEnvironment* env = nullptr;
    const proto::ProtoObject* spawnerGlobals = nullptr;
    if (args->getSize(context) >= 2) {
        const proto::ProtoObject* first = args->getAt(context, 0);
        const proto::ProtoExternalPointer* ep = first ? first->asExternalPointer(context) : nullptr;
        if (ep) {
            env = static_cast<protoPython::PythonEnvironment*>(ep->getPointer(context));
            if (env) {
                callableIdx = 1;
                // Slot args[1] is the inherited globals when args has
                // at least 3 entries.  We never store a callable
                // directly at args[1] from our producers, so a 2-entry
                // tuple is the legacy "no globals" shape and we leave
                // s_currentGlobals untouched.
                if (args->getSize(context) >= 3) {
                    const proto::ProtoObject* second = args->getAt(context, 1);
                    if (second && second != PROTO_NONE) {
                        spawnerGlobals = second;
                        callableIdx = 2;
                    } else if (second == PROTO_NONE) {
                        // Explicit "no globals inheritance" sentinel.
                        callableIdx = 2;
                    }
                }
            }
        }
    }

    const proto::ProtoObject* result = nullptr;
    {
        protoPython::PythonEnvironment::ContextScope scope(env, context);
        const proto::ProtoObject* oldGlobals = protoPython::PythonEnvironment::getCurrentGlobals();
        if (spawnerGlobals) {
            protoPython::PythonEnvironment::setCurrentGlobals(spawnerGlobals);
        }
        const proto::ProtoObject* callable = args->getAt(context, static_cast<int>(callableIdx));
        const proto::ProtoList* argList = context->newList();
        for (proto::proto_ulong i = callableIdx + 1; i < args->getSize(context); ++i)
            argList = argList->appendLast(context, args->getAt(context, static_cast<int>(i)));
        try {
            // A debug session (protopy --dap) sees the thread from here on.
            protoPython::debugger::ThreadScope debugged(context, env, callable);
            result = protoPython::invokePythonCallable(context, callable, argList, nullptr);
        } catch (const proto::ProtoException& e) {
            // An exception escaping the thread body does not leave this
            // entry. CPython reports it through sys.unraisablehook
            // ("Exception ignored in thread started by: <callable>") and
            // ignores SystemExit silently; a foreign value is reported too.
            if (env) {
                try {
                    const proto::ProtoObject* systemExit = env->lookupName("SystemExit");
                    if (!(systemExit && systemExit != PROTO_NONE && e.isInstanceOf(context, systemExit)))
                        env->reportUnraisable(context, e.value(), "Exception ignored in thread started by", callable);
                } catch (...) {
                    // A report that raises is dropped.
                }
            }
            result = nullptr;
        } catch (const std::exception& e) {
            std::cerr << "Exception ignored in thread started by native code: " << e.what() << std::endl;
            result = nullptr;
        } catch (...) {
            std::cerr << "Exception of an unknown C++ type ignored in a thread" << std::endl;
            result = nullptr;
        }
        if (spawnerGlobals) {
            protoPython::PythonEnvironment::setCurrentGlobals(oldGlobals);
        }
        // This OS thread is finished: drop every GC root and every raw
        // thread-local ProtoObject* it still holds, while its context and
        // environment are still registered (releaseThreadState reaches the
        // owning root sets through them, so it cannot run after ContextScope
        // has been destroyed).  Without this, each finished thread left its
        // py_thread record permanently reachable — ~300 marked cells per
        // thread, which is what made the rule-8 conformance case reach its
        // heap ceiling.  See docs/CONFORMANCE.md.
        protoPython::PythonEnvironment::releaseThreadState(context);
    }
    return result;
}

static const proto::ProtoObject* py_start_new_thread(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoArguments* posArgs,
    const proto::ProtoKeywordArguments* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* callable = posArgs->getAt(ctx, 0);
    protoPython::PythonEnvironment* env = protoPython::PythonEnvironment::fromContext(ctx);
    const proto::ProtoList* argsForThread = ctx->newList();
    // Layout consumed by thread_bootstrap: [env, globals, callable, *args].
    // Both env and globals are optional in principle, but py_start_new_thread
    // always has an env (we're inside one).  We pass PROTO_NONE if the
    // spawner has no currentGlobals so bootstrap stays at the same shape.
    if (env) {
        argsForThread = argsForThread->appendLast(ctx, ctx->fromExternalPointer(env, nullptr));
        const proto::ProtoObject* spawnerGlobals = protoPython::PythonEnvironment::getCurrentGlobals();
        argsForThread = argsForThread->appendLast(
            ctx, spawnerGlobals ? spawnerGlobals : PROTO_NONE);
    }
    argsForThread = argsForThread->appendLast(ctx, callable);
    if (posArgs->getSize(ctx) >= 2) {
        const proto::ProtoObject* second = posArgs->getAt(ctx, 1);
        if (::protoPython::PythonEnvironment::tupleView(ctx, second)) {
            ::protoPython::TupleView tup = ::protoPython::PythonEnvironment::tupleView(ctx, second);
            for (proto::proto_ulong i = 0; i < tup->getSize(ctx); ++i)
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
    const proto::ProtoArguments* /*posArgs*/,
    const proto::ProtoKeywordArguments* /*kwargs*/) {
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
    const proto::ProtoArguments* /*posArgs*/,
    const proto::ProtoKeywordArguments* /*kwargs*/) {
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
    const proto::ProtoArguments* /*posArgs*/,
    const proto::ProtoKeywordArguments* /*kwargs*/) {
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
    const proto::ProtoArguments* /*posArgs*/,
    const proto::ProtoKeywordArguments* /*kwargs*/) {
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
    const proto::ProtoArguments* /*posArgs*/,
    const proto::ProtoKeywordArguments* /*kwargs*/) {
    return ctx->fromInteger(current_thread_id());
}

static const proto::ProtoObject* py_getpid(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoArguments* /*posArgs*/,
    const proto::ProtoKeywordArguments* /*kwargs*/) {
    return ctx->fromInteger(current_process_id());
}

static const proto::ProtoObject* py_join_thread(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoArguments* posArgs,
    const proto::ProtoKeywordArguments* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* handle = posArgs->getAt(ctx, 0);
    const proto::ProtoExternalPointer* ext = handle->asExternalPointer(ctx);
    if (!ext) return PROTO_NONE;
    proto::ProtoThread* thread = static_cast<proto::ProtoThread*>(ext->getPointer(ctx));
    if (thread) {
        // 2026-05-25: `Thread.join()` blocks until the target thread
        // exits. Bracket in an unmanaged region so the GC can collect
        // while we wait. The joined thread itself may run protoCore
        // ops up until exit; once it finishes, our park-aware
        // returnFromUnmanaged ensures we don't resume touching
        // ProtoObject* while a STW is in progress.
        proto::ProtoContext::UnmanagedScope u(ctx);
        thread->join(ctx);
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_is_main_interpreter(
    proto::ProtoContext* ctx,
    const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments*, const proto::ProtoKeywordArguments*) {
    return PROTO_TRUE; // For now
}

static const proto::ProtoObject* py_get_main_thread_ident(
    proto::ProtoContext* ctx,
    const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments*, const proto::ProtoKeywordArguments*) {
    return ctx->fromInteger(g_main_thread_id);
}

// _ThreadHandle methods.  The handle is a mutable Python object with two
// internal slots:
//   _proto_thread  — ExternalPointer wrapping a proto::ProtoThread*
//   ident          — integer (the OS-level identity of the underlying thread)
// A handle is "done" when the underlying ProtoThread is no longer registered
// in space->threads (which is how protoCore tracks running threads), or when
// _set_done() marked it.  A handle with no ProtoThread -- not started yet,
// or made by _make_thread_handle for the main or a dummy thread -- is not
// done until _set_done(), as in CPython: reporting it done made
// main_thread().is_alive() false and threading._shutdown() return at once,
// without joining any thread.

static const proto::ProtoObject* py_handle_is_done(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments*, const proto::ProtoKeywordArguments*) {
    if (!self) return PROTO_TRUE;
    const proto::ProtoString* doneAttr =
        proto::ProtoString::createSymbol(ctx, "_done");
    const proto::ProtoObject* doneFlag = self->getAttribute(ctx, doneAttr);
    if (doneFlag == PROTO_TRUE) return PROTO_TRUE;
    const proto::ProtoString* ptrAttr =
        proto::ProtoString::createSymbol(ctx, "_proto_thread");
    const proto::ProtoObject* handlePtr = self->getAttribute(ctx, ptrAttr);
    if (!handlePtr || handlePtr == PROTO_NONE) return PROTO_FALSE;
    const proto::ProtoExternalPointer* ext = handlePtr->asExternalPointer(ctx);
    if (!ext) return PROTO_FALSE;
    proto::ProtoThread* thread =
        static_cast<proto::ProtoThread*>(ext->getPointer(ctx));
    if (!thread) return PROTO_FALSE;
    proto::proto_ulong threadId = reinterpret_cast<uintptr_t>(thread);
    const proto::ProtoSparseList* threads = ctx->space->threads;
    if (!threads) return PROTO_TRUE;
    return (threads->getAt(ctx, threadId) == PROTO_NONE) ? PROTO_TRUE : PROTO_FALSE;
}

static const proto::ProtoObject* py_handle_join(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments*, const proto::ProtoKeywordArguments*) {
    if (!self) return PROTO_NONE;
    const proto::ProtoString* ptrAttr =
        proto::ProtoString::createSymbol(ctx, "_proto_thread");
    const proto::ProtoObject* handlePtr = self->getAttribute(ctx, ptrAttr);
    if (!handlePtr || handlePtr == PROTO_NONE) return PROTO_NONE;
    const proto::ProtoExternalPointer* ext = handlePtr->asExternalPointer(ctx);
    if (!ext) return PROTO_NONE;
    proto::ProtoThread* thread =
        static_cast<proto::ProtoThread*>(ext->getPointer(ctx));
    if (thread) {
        // 2026-05-25: bracket the join() as in py_join_thread above.
        proto::ProtoContext::UnmanagedScope u(ctx);
        thread->join(ctx);
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_handle_set_done(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* self,
    const proto::ParentLink*,
    const proto::ProtoArguments*, const proto::ProtoKeywordArguments*) {
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
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    proto::ProtoObject* handle =
        const_cast<proto::ProtoObject*>(ctx->newObject(true));
    proto::proto_long initialIdent = 0;
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

// The non-daemon threads started by start_joinable_thread, which
// _thread._shutdown joins at interpreter exit.  A ProtoThread is never
// collected (its cells live on a scratch context protoCore never submits),
// and a second ProtoThread::join returns at once, so plain pointers are
// enough.  The mutex is never held across an allocation or a join.  Each
// entry carries the process that started it: a fork child inherits the
// list, but not the parent's threads, and must not join them.
struct NonDaemonThread {
    proto::ProtoThread* thread;
    long long pid;
};
static std::mutex g_nonDaemonMutex;
static std::vector<NonDaemonThread> g_nonDaemonThreads;

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
    const proto::ProtoArguments* posArgs,
    const proto::ProtoKeywordArguments* kwargs) {
    if (!posArgs || posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* callable = posArgs->getAt(ctx, 0);

    // Resolve handle (kwarg "handle"; otherwise build a fresh one).
    // Keyword arguments are keyed by the string hash, like every other
    // native function reads them. Keying by the symbol pointer never
    // matched, so threading.Thread's own handle was never populated and
    // Thread.join() / is_alive() saw a thread that had "already finished".
    const proto::ProtoObject* handle = nullptr;
    if (kwargs) {
        const proto::ProtoString* handleKey =
            proto::ProtoString::createSymbol(ctx, "handle");
        proto::proto_ulong handleHash = handleKey->getHash(ctx);
        if (kwargs->has(ctx, handleHash)) {
            handle = kwargs->getAt(ctx, handleHash);
        }
    }
    if (!handle || handle == PROTO_NONE) {
        handle = py_make_thread_handle(ctx, self, nullptr, argsView(proto::ProtoArguments(nullptr, 0)), nullptr);
    }

    // Build the args list passed to thread_bootstrap:
    //   [env_ptr, spawner_globals_or_None, callable, ...].
    // The globals slot lets the new OS thread resolve LOAD_GLOBAL /
    // lookupName from the same module as the spawner — see the
    // thread_bootstrap comment for the why.
    protoPython::PythonEnvironment* env =
        protoPython::PythonEnvironment::fromContext(ctx);
    const proto::ProtoList* argsForThread = ctx->newList();
    if (env) {
        argsForThread = argsForThread->appendLast(
            ctx, ctx->fromExternalPointer(env, nullptr));
        const proto::ProtoObject* spawnerGlobals =
            protoPython::PythonEnvironment::getCurrentGlobals();
        argsForThread = argsForThread->appendLast(
            ctx, spawnerGlobals ? spawnerGlobals : PROTO_NONE);
    }
    argsForThread = argsForThread->appendLast(ctx, callable);

    // daemon=True threads are not joined at exit (CPython's default is
    // daemon=True for start_joinable_thread; threading passes it always).
    bool daemon = true;
    if (kwargs) {
        const proto::proto_ulong daemonHash =
            proto::ProtoString::createSymbol(ctx, "daemon")->getHash(ctx);
        if (kwargs->has(ctx, daemonHash)) {
            const proto::ProtoObject* d = kwargs->getAt(ctx, daemonHash);
            daemon = !(d == PROTO_FALSE || d == PROTO_NONE ||
                       (d && d->isInteger(ctx) && d->asLong(ctx) == 0));
        }
    }

    // Spawn.
    const proto::ProtoString* name = proto::ProtoString::createSymbol(ctx, "thread");
    const proto::ProtoThread* thread = ctx->space->newThread(
        ctx, name, thread_bootstrap, argsForThread, nullptr);
    if (!daemon && thread) {
        std::lock_guard<std::mutex> lock(g_nonDaemonMutex);
        g_nonDaemonThreads.push_back({const_cast<proto::ProtoThread*>(thread), current_process_id()});
    }

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
    const proto::ProtoArguments*, const proto::ProtoKeywordArguments*) {
    return PROTO_TRUE;
}

// _thread._shutdown — invoked by threading._shutdown at interpreter exit
// (protopy's finishInterpreter calls it, as CPython's Py_Finalize does).
// Joins every non-daemon thread started by start_joinable_thread, including
// threads they start while it waits, so they finish their work (e.g.
// flushing buffered output) and leave the process space before it is
// destroyed.  Daemon threads are not waited for, as in CPython.
static const proto::ProtoObject* py_shutdown(
    proto::ProtoContext* ctx,
    const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments*, const proto::ProtoKeywordArguments*) {
    while (true) {
        std::vector<NonDaemonThread> pending;
        {
            std::lock_guard<std::mutex> lock(g_nonDaemonMutex);
            pending.swap(g_nonDaemonThreads);
        }
        if (pending.empty()) break;
        const auto self = current_process_id();
        for (const NonDaemonThread& t : pending) {
            if (t.pid != self) continue;
            // Each join can take arbitrarily long (a worker still doing work
            // has to drain it).  Bracket so the GC keeps working meanwhile.
            proto::ProtoContext::UnmanagedScope u(ctx);
            t.thread->join(ctx);
        }
    }
    return PROTO_NONE;
}

static const proto::ProtoObject* py_is_alive(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoArguments* posArgs,
    const proto::ProtoKeywordArguments* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 1) return PROTO_FALSE;
    const proto::ProtoObject* handle = posArgs->getAt(ctx, 0);
    const proto::ProtoExternalPointer* ext = handle->asExternalPointer(ctx);
    if (!ext) return PROTO_FALSE;
    proto::ProtoThread* thread = static_cast<proto::ProtoThread*>(ext->getPointer(ctx));
    if (!thread) return PROTO_FALSE;
    
    proto::proto_ulong threadId = reinterpret_cast<uintptr_t>(thread);
    const proto::ProtoSparseList* threads = ctx->space->threads;
    if (threads && threads->getAt(ctx, threadId) != PROTO_NONE)
        return PROTO_TRUE;
    
    return PROTO_FALSE;
}

// _thread.stack_size([size]) -> the previous size: the stack size of the
// threads started from now on (protoCore's ProtoSpace::setThreadStackBytes).
// 0 means the platform's default; anything else must be at least 32 KiB,
// as in CPython.
static const proto::ProtoObject* py_stack_size(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoArguments* posArgs, const proto::ProtoKeywordArguments*) {
    const long long previous = static_cast<long long>(proto::ProtoSpace::threadStackBytes());
    if (posArgs && posArgs->getSize(ctx) > 0) {
        const proto::ProtoObject* sizeObj = posArgs->getAt(ctx, 0);
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        if (!sizeObj || !sizeObj->isInteger(ctx)) {
            if (env) env->raiseTypeError(ctx, "size must be an integer");
            return nullptr;
        }
        const long long size = sizeObj->asLong(ctx);
        if (size != 0 && size < 32768) {
            if (env) env->raiseValueError(ctx, proto::ProtoString::fromUTF8(ctx, ("size not valid: " + std::to_string(size) + " bytes").c_str())->asObject(ctx));
            return nullptr;
        }
        proto::ProtoSpace::setThreadStackBytes(static_cast<size_t>(size));
    }
    return ctx->fromInteger(previous);
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
    lockProt = lockProt->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_at_fork_reinit"),
        ctx->fromMethod(nullptr, py_lock_at_fork_reinit));
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
    rlockProt = rlockProt->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_at_fork_reinit"),
        ctx->fromMethod(nullptr, py_rlock_at_fork_reinit));
    rlockProt = rlockProt->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_is_owned"),
        ctx->fromMethod(nullptr, py_rlock_is_owned));
    rlockProt = rlockProt->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_recursion_count"),
        ctx->fromMethod(nullptr, py_rlock_recursion_count));
    rlockProt = rlockProt->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_release_save"),
        ctx->fromMethod(nullptr, py_rlock_release_save));
    rlockProt = rlockProt->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_acquire_restore"),
        ctx->fromMethod(nullptr, py_rlock_acquire_restore));
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
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "stack_size"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_stack_size));
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

    auto py_thread_count = [](proto::ProtoContext* c, const proto::ProtoObject*, const proto::ParentLink*, const proto::ProtoArguments*, const proto::ProtoKeywordArguments*) -> const proto::ProtoObject* {
        return c->fromInteger(c->space->runningThreads.load());
    };
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "_count"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_thread_count));

    auto py_get_handle = [](proto::ProtoContext* c, const proto::ProtoObject*, const proto::ParentLink*, const proto::ProtoArguments* args, const proto::ProtoKeywordArguments*) -> const proto::ProtoObject* {
        if (!args || args->getSize(c) < 1) return PROTO_NONE;
        proto::proto_ulong tid = (proto::proto_ulong)args->getAt(c, 0)->asLong(c);
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
