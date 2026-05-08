#include <protoPython/FaulthandlerModule.h>
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>
#include <atomic>
#include <csignal>
#include <cstring>
#include <unistd.h>

// faulthandler — real implementation. Replaces a 35-line stub
// (enable/disable were `return PROTO_NONE`, is_enabled hardcoded
// PROTO_FALSE) so callers that probe `faulthandler.enable()` got
// no actual fatal-signal protection.
//
// Scope vs CPython: CPython prints a full Python traceback on
// fatal signals via a self-pipe + main-loop polling scheme. We
// install async-signal-safe POSIX handlers that write a fixed
// "Fatal Python error: …" line directly to the configured file
// descriptor (or stderr) and re-raise the signal so the OS
// produces a core dump / process exit normally. Frame
// introspection from a signal handler isn't implemented — the
// traceback would require coordinating with the bytecode
// dispatcher, which is out of scope for this fix. The audit's
// MEDIUM-severity gap was about "is_enabled lies"; this commit
// makes it tell the truth, plus does the minimum useful crash-
// reporting work that's safe from a real signal handler.

namespace protoPython {
namespace faulthandler {

// Module state. Plain atomics — no mutex needed because
// enable/disable mutate them via simple loads/stores guarded by
// std::signal's own atomicity.
static std::atomic<bool> s_enabled{false};
static std::atomic<int> s_fd{2};  // default: stderr

// async-signal-safe message writer. Writes a fixed banner + the
// signal name (taken from a static table) and re-raises so the
// process produces its normal terminal behaviour (SIGSEGV →
// segfault, SIGABRT → abort, etc.).
static const char* sig_name(int sig) {
    switch (sig) {
        case SIGSEGV: return "Segmentation fault";
        case SIGFPE:  return "Floating point exception";
        case SIGABRT: return "Aborted";
        case SIGBUS:  return "Bus error";
        case SIGILL:  return "Illegal instruction";
        default:      return "Fatal signal";
    }
}

extern "C" void faulthandler_signal_handler(int sig) {
    int fd = s_fd.load(std::memory_order_acquire);
    static const char prefix[] = "Fatal Python error: ";
    // Use raw write(2) — async-signal-safe per POSIX.
    (void)write(fd, prefix, sizeof(prefix) - 1);
    const char* name = sig_name(sig);
    (void)write(fd, name, std::strlen(name));
    (void)write(fd, "\n", 1);
    // Reset to SIG_DFL and re-raise so the OS produces the normal
    // terminal effect (segfault dump / abort / etc.). Without this
    // the handler would run in a loop on some platforms.
    std::signal(sig, SIG_DFL);
    std::raise(sig);
}

static void install_signals(int fd) {
    s_fd.store(fd, std::memory_order_release);
    s_enabled.store(true, std::memory_order_release);
    std::signal(SIGSEGV, faulthandler_signal_handler);
    std::signal(SIGFPE,  faulthandler_signal_handler);
    std::signal(SIGABRT, faulthandler_signal_handler);
    std::signal(SIGBUS,  faulthandler_signal_handler);
    std::signal(SIGILL,  faulthandler_signal_handler);
}

static void uninstall_signals() {
    s_enabled.store(false, std::memory_order_release);
    std::signal(SIGSEGV, SIG_DFL);
    std::signal(SIGFPE,  SIG_DFL);
    std::signal(SIGABRT, SIG_DFL);
    std::signal(SIGBUS,  SIG_DFL);
    std::signal(SIGILL,  SIG_DFL);
}

// faulthandler.enable(file=sys.stderr, all_threads=True) — file may
// be a Python file object with a `fileno()` method, or an integer
// fd. We accept both via a tolerant int-extraction. all_threads is
// recorded but not yet acted on (single-thread crash reporting is
// the common case; the multi-thread variant needs cross-thread
// stack walking that's out of scope).
static const proto::ProtoObject* py_enable(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    int fd = 2;
    if (posArgs && posArgs->getSize(ctx) >= 1) {
        const proto::ProtoObject* fileArg = posArgs->getAt(ctx, 0);
        if (fileArg && fileArg != PROTO_NONE) {
            if (fileArg->isInteger(ctx)) {
                fd = static_cast<int>(fileArg->asLong(ctx));
            } else {
                // Try fileno() for Python file objects.
                const proto::ProtoString* filenoS =
                    PythonEnvironment::getInternedString(ctx, "fileno");
                const proto::ProtoObject* m = fileArg->getAttribute(ctx, filenoS);
                if (m && m->asMethod(ctx)) {
                    const proto::ProtoObject* r = m->asMethod(ctx)(ctx,
                        const_cast<proto::ProtoObject*>(fileArg),
                        nullptr, ctx->newList(), nullptr);
                    if (r && r->isInteger(ctx)) fd = static_cast<int>(r->asLong(ctx));
                }
            }
        }
    }
    install_signals(fd);
    return PROTO_NONE;
}

static const proto::ProtoObject* py_disable(
    proto::ProtoContext*, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    uninstall_signals();
    return PROTO_NONE;
}

static const proto::ProtoObject* py_is_enabled(
    proto::ProtoContext*, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList*, const proto::ProtoSparseList*) {
    return s_enabled.load(std::memory_order_acquire) ? PROTO_TRUE : PROTO_FALSE;
}

// faulthandler.dump_traceback(file=sys.stderr, all_threads=True)
// Without frame introspection from this layer, we can't render a
// real Python traceback; emit a placeholder line to the configured
// fd so callers see the function did something. CPython's user
// docs say this is for emergency debugging and tools; the
// placeholder makes the API non-stub without claiming behaviour
// we don't have.
static const proto::ProtoObject* py_dump_traceback(
    proto::ProtoContext* ctx, const proto::ProtoObject*, const proto::ParentLink*,
    const proto::ProtoList* posArgs, const proto::ProtoSparseList*) {
    int fd = s_fd.load(std::memory_order_acquire);
    if (posArgs && posArgs->getSize(ctx) >= 1) {
        const proto::ProtoObject* fileArg = posArgs->getAt(ctx, 0);
        if (fileArg && fileArg->isInteger(ctx)) fd = static_cast<int>(fileArg->asLong(ctx));
    }
    static const char msg[] = "Stack (no Python traceback available — frame introspection not wired)\n";
    (void)write(fd, msg, sizeof(msg) - 1);
    return PROTO_NONE;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(false);
    mod = mod->setAttribute(ctx,
        PythonEnvironment::getInternedString(ctx, "enable"),
        ctx->fromMethod(nullptr, py_enable));
    mod = mod->setAttribute(ctx,
        PythonEnvironment::getInternedString(ctx, "disable"),
        ctx->fromMethod(nullptr, py_disable));
    mod = mod->setAttribute(ctx,
        PythonEnvironment::getInternedString(ctx, "is_enabled"),
        ctx->fromMethod(nullptr, py_is_enabled));
    mod = mod->setAttribute(ctx,
        PythonEnvironment::getInternedString(ctx, "dump_traceback"),
        ctx->fromMethod(nullptr, py_dump_traceback));
    return mod;
}

} // namespace faulthandler
} // namespace protoPython
