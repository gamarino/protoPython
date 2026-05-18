#include <protoPython/SignalModule.h>
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>
#include <csignal>
#include <cstring>

namespace protoPython {

// Forward decl — defined in ExecutionEngine.cpp at namespace scope.
extern const proto::ProtoObject* invokePythonCallable(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* callable,
    const proto::ProtoList* args,
    const proto::ProtoSparseList* kwargs);

namespace signal_module {

// --- Signal delivery state ---
//
// The C-level signal handler is invoked from an OS-async context: it
// MUST be async-signal-safe. POSIX guarantees writes to
// `volatile sig_atomic_t` are atomic with respect to other signals on
// the same thread, so storing the per-signal pending flag and a global
// pending counter through that type is sound.
//
// Every other field below is touched only from the interpreter's
// synchronous safepoint, never from the OS handler, so plain types
// suffice.
//
// Handler objects are pinned in a `ProtoRootSet` so the GC keeps them
// reachable across asynchronous arrival of the signal — between
// `signal.signal(SIG, h)` registering `h` and the actual signal firing
// minutes later, the bytecode loop may have triggered many cycles, and
// without an explicit pin `h` could be collected.

static constexpr int kMaxSignal = 64;

static const proto::ProtoObject* s_handlers[kMaxSignal] = {};
static proto::ProtoRootSet::Handle s_handlerHandles[kMaxSignal] = {};
static volatile sig_atomic_t s_pending[kMaxSignal] = {};
static volatile sig_atomic_t s_pendingAny = 0;

static proto::ProtoRootSet* s_rootSet = nullptr;

// SIG_DFL/SIG_IGN are stored as integer sentinels in the handler array
// only conceptually — we keep the array slot null and remember the
// disposition through std::signal directly. We mark them with explicit
// integer cells so getsignal() can return them. Lazily created on
// first use to avoid touching the runtime before initialize().
static const proto::ProtoObject* sigDflMarker(proto::ProtoContext* ctx) {
    return ctx->fromInteger(0);
}
static const proto::ProtoObject* sigIgnMarker(proto::ProtoContext* ctx) {
    return ctx->fromInteger(1);
}

// Async-signal-safe: only writes to volatile sig_atomic_t.
static void global_signal_handler(int sig) {
    if (sig >= 0 && sig < kMaxSignal) {
        s_pending[sig] = 1;
        s_pendingAny = 1;
    }
}

bool hasPendingSignal() {
    return s_pendingAny != 0;
}

bool checkAndDeliverPendingSignals(proto::ProtoContext* ctx, PythonEnvironment* env) {
    if (s_pendingAny == 0) return false;
    // Clear before scanning so a second arrival during dispatch
    // re-arms the flag and we re-enter on the next safepoint.
    s_pendingAny = 0;
    bool delivered = false;
    for (int sig = 0; sig < kMaxSignal; ++sig) {
        if (!s_pending[sig]) continue;
        s_pending[sig] = 0;
        const proto::ProtoObject* handler = s_handlers[sig];
        if (!handler || handler == PROTO_NONE) continue;
        // SIG_DFL/SIG_IGN sentinels: nothing to call.
        if (handler->isInteger(ctx)) continue;
        const proto::ProtoList* args = ctx->newList()
            ->appendLast(ctx, ctx->fromInteger(sig))
            ->appendLast(ctx, PROTO_NONE);
        if (handler->asMethod(ctx)) {
            handler->asMethod(ctx)(ctx, nullptr, nullptr, args, nullptr);
        } else {
            invokePythonCallable(ctx, handler, args,
                env ? env->getEmptySparseList() : nullptr);
        }
        delivered = true;
        // Don't suppress a pending exception — let the bytecode
        // loop's standard exception path handle KeyboardInterrupt
        // and friends.
    }
    return delivered;
}

static void install_handler_root_pin(int sig,
                                     proto::ProtoContext* ctx,
                                     const proto::ProtoObject* handler) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    if (!env) return;
    if (!s_rootSet) {
        s_rootSet = env->getSpace()->createRootSet("signal-handlers");
    }
    // Drop the prior pin (if any) before installing the new one.
    if (s_handlerHandles[sig] != proto::ProtoRootSet::kNullHandle) {
        s_rootSet->remove(s_handlerHandles[sig]);
        s_handlerHandles[sig] = proto::ProtoRootSet::kNullHandle;
    }
    if (handler && handler != PROTO_NONE && !handler->isInteger(ctx)) {
        s_handlerHandles[sig] = s_rootSet->add(handler);
    }
}

static const proto::ProtoObject* py_signal(
    proto::ProtoContext* ctx,
    const proto::ProtoObject*,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (!posArgs || posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* sigObj = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* handler = posArgs->getAt(ctx, 1);
    if (!sigObj || !sigObj->isInteger(ctx)) return PROTO_NONE;
    int sig = static_cast<int>(sigObj->asLong(ctx));
    if (sig < 0 || sig >= kMaxSignal) {
        PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
        if (env) env->raiseValueError(ctx,
            PythonEnvironment::getInternedString(ctx, "signal number out of range")->asObject(ctx));
        return nullptr;
    }

    const proto::ProtoObject* oldHandler = s_handlers[sig];
    if (!oldHandler) oldHandler = sigDflMarker(ctx);

    // Update OS-level disposition first; even though there's a small
    // window between this and updating s_handlers[], the slot already
    // held the previous value, so any signal firing in the gap routes
    // to the previous handler, not a half-installed new one.
    bool isDfl = handler && handler->isInteger(ctx) && handler->asLong(ctx) == 0;
    bool isIgn = handler && handler->isInteger(ctx) && handler->asLong(ctx) == 1;
    if (isDfl) {
        std::signal(sig, SIG_DFL);
    } else if (isIgn) {
        std::signal(sig, SIG_IGN);
    } else if (handler == PROTO_NONE) {
        // CPython treats `signal.signal(sig, None)` as an error; we
        // tolerate it as SIG_DFL for compatibility with stub callers.
        std::signal(sig, SIG_DFL);
    } else {
        std::signal(sig, global_signal_handler);
    }

    install_handler_root_pin(sig, ctx, handler);
    s_handlers[sig] = handler;
    return oldHandler;
}

static const proto::ProtoObject* py_getsig(
    proto::ProtoContext* ctx,
    const proto::ProtoObject*,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (!posArgs || posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* sigObj = posArgs->getAt(ctx, 0);
    if (!sigObj || !sigObj->isInteger(ctx)) return PROTO_NONE;
    int sig = static_cast<int>(sigObj->asLong(ctx));
    if (sig < 0 || sig >= kMaxSignal) return PROTO_NONE;
    const proto::ProtoObject* h = s_handlers[sig];
    return h ? h : sigDflMarker(ctx);
}

// signal.strsignal(sig) -> str.  Wraps ::strsignal() (glibc) with a
// fallback for portability.
static const proto::ProtoObject* py_strsignal(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (!posArgs || posArgs->getSize(ctx) < 1) return PROTO_NONE;
    int sig = static_cast<int>(posArgs->getAt(ctx, 0)->asLong(ctx));
    const char* s = ::strsignal(sig);
    return PythonEnvironment::getInternedString(ctx, s ? s : "Unknown signal")->asObject(ctx);
}

// signal.alarm(seconds) -> previous remaining alarm in seconds.
static const proto::ProtoObject* py_alarm(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    unsigned int sec = 0;
    if (posArgs && posArgs->getSize(ctx) >= 1) {
        sec = static_cast<unsigned int>(posArgs->getAt(ctx, 0)->asLong(ctx));
    }
    return ctx->fromInteger(::alarm(sec));
}

// signal.pause() -> None.  Blocks until a signal arrives.
static const proto::ProtoObject* py_pause(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    ::pause();
    return PROTO_NONE;
}

// signal.siginterrupt(sig, flag) -> None.  ::siginterrupt was deprecated
// in POSIX 2008 in favour of sigaction's SA_RESTART bit, but stdlib
// code still pokes at it.
static const proto::ProtoObject* py_siginterrupt(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    if (!posArgs || posArgs->getSize(ctx) < 2) return PROTO_NONE;
    int sig = static_cast<int>(posArgs->getAt(ctx, 0)->asLong(ctx));
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    int flag = (env && env->isTrue(posArgs->getAt(ctx, 1))) ? 1 : 0;
    ::siginterrupt(sig, flag);
    return PROTO_NONE;
}

// signal.set_wakeup_fd(fd) -> previous fd (or -1).  Stub: protoPython
// doesn't wire a wakeup-fd channel into the signal handler, but
// asyncio and select-loop initialisation call this unconditionally
// and crash if it raises.  Track the last value and report it back.
static int s_wakeup_fd = -1;
static const proto::ProtoObject* py_set_wakeup_fd(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    int old = s_wakeup_fd;
    if (posArgs && posArgs->getSize(ctx) >= 1) {
        s_wakeup_fd = static_cast<int>(posArgs->getAt(ctx, 0)->asLong(ctx));
    }
    return ctx->fromInteger(old);
}

// signal.valid_signals() -> set of valid signal numbers.
static const proto::ProtoObject* py_valid_signals(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    PythonEnvironment* env = PythonEnvironment::fromContext(ctx);
    const proto::ProtoSet* s = ctx->newSet();
    for (int i = 1; i < NSIG; ++i) {
        s = s->add(ctx, ctx->fromInteger(i));
    }
    (void)env;
    return s->asObject(ctx);
}

// Synchronous helper exposed as `signal.raise_signal(sig)`: triggers a
// signal in the current process. Useful for tests and for code that
// wants to invoke its own handler.
static const proto::ProtoObject* py_raise_signal(
    proto::ProtoContext* ctx,
    const proto::ProtoObject*,
    const proto::ParentLink*,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList*) {
    if (!posArgs || posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* sigObj = posArgs->getAt(ctx, 0);
    if (!sigObj || !sigObj->isInteger(ctx)) return PROTO_NONE;
    int sig = static_cast<int>(sigObj->asLong(ctx));
    std::raise(sig);
    return PROTO_NONE;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(false);

    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "signal"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_signal));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "getsignal"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_getsig));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "raise_signal"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_raise_signal));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "strsignal"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_strsignal));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "alarm"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_alarm));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "pause"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_pause));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "siginterrupt"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_siginterrupt));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "set_wakeup_fd"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_set_wakeup_fd));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "valid_signals"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_valid_signals));

    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "SIGINT"), ctx->fromInteger(SIGINT));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "SIGTERM"), ctx->fromInteger(SIGTERM));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "SIGUSR1"), ctx->fromInteger(SIGUSR1));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "SIGUSR2"), ctx->fromInteger(SIGUSR2));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "SIGHUP"), ctx->fromInteger(SIGHUP));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "SIGALRM"), ctx->fromInteger(SIGALRM));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "SIG_DFL"), ctx->fromInteger(0));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "SIG_IGN"), ctx->fromInteger(1));
    // Signals subprocess.py touches by name (SIGKILL/SIGPIPE/SIGCHLD/
    // SIGABRT/SIGSEGV/SIGFPE/SIGILL/SIGQUIT/SIGSTOP/SIGCONT) plus a
    // few extras the stdlib expects to be present unconditionally.
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "SIGKILL"), ctx->fromInteger(SIGKILL));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "SIGPIPE"), ctx->fromInteger(SIGPIPE));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "SIGCHLD"), ctx->fromInteger(SIGCHLD));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "SIGABRT"), ctx->fromInteger(SIGABRT));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "SIGSEGV"), ctx->fromInteger(SIGSEGV));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "SIGFPE"),  ctx->fromInteger(SIGFPE));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "SIGILL"),  ctx->fromInteger(SIGILL));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "SIGQUIT"), ctx->fromInteger(SIGQUIT));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "SIGSTOP"), ctx->fromInteger(SIGSTOP));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "SIGCONT"), ctx->fromInteger(SIGCONT));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "SIGTSTP"), ctx->fromInteger(SIGTSTP));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "SIGTTIN"), ctx->fromInteger(SIGTTIN));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "SIGTTOU"), ctx->fromInteger(SIGTTOU));
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "SIGBUS"),  ctx->fromInteger(SIGBUS));
#ifdef SIGSYS
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "SIGSYS"),  ctx->fromInteger(SIGSYS));
#endif
#ifdef SIGIO
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "SIGIO"),   ctx->fromInteger(SIGIO));
#endif
    mod = mod->setAttribute(ctx, proto::ProtoString::createSymbol(ctx, "NSIG"),    ctx->fromInteger(NSIG));

    return mod;
}

} // namespace signal_module
} // namespace protoPython
