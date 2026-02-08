#include <protoPython/SignalModule.h>
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>
#include <csignal>
#include <iostream>
#include <map>
#include <vector>

namespace protoPython {
namespace signal_module {

// --- Global Signal State ---
// This is a simplification; a robust implementation would use a signalfd or a self-pipe.
static std::map<int, const proto::ProtoObject*> s_signalHandlers;
static proto::ProtoContext* s_signalContext = nullptr;

static void global_signal_handler(int sig) {
    if (s_signalHandlers.count(sig)) {
        // In a real VM, we would set a flag and handle this in the main loop SAFELY.
        // For protoPython, we'll try to invoke it if we have a context, but this is DANGEROUS.
        // Better: just log that it happened for now until we have a proper signal delivery flag.
        if (std::getenv("PROTO_SIGNAL_DIAG")) {
            std::cerr << "[proto-signal] Signal " << sig << " received.\n" << std::flush;
        }
    }
}

static const proto::ProtoObject* py_signal(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 2) return PROTO_NONE;
    const proto::ProtoObject* sigObj = posArgs->getAt(ctx, 0);
    const proto::ProtoObject* handler = posArgs->getAt(ctx, 1);
    
    if (!sigObj->isInteger(ctx)) return PROTO_NONE;
    int sig = static_cast<int>(sigObj->asLong(ctx));
    
    const proto::ProtoObject* oldHandler = PROTO_NONE;
    if (s_signalHandlers.count(sig)) oldHandler = s_signalHandlers[sig];
    
    s_signalHandlers[sig] = handler;
    s_signalContext = ctx;
    
    if (handler == PROTO_NONE) {
        std::signal(sig, SIG_DFL);
    } else {
        std::signal(sig, global_signal_handler);
    }
    
    return oldHandler;
}

static const proto::ProtoObject* py_getsig(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parentLink*/,
    const proto::ProtoList* posArgs,
    const proto::ProtoSparseList* /*kwargs*/) {
    if (posArgs->getSize(ctx) < 1) return PROTO_NONE;
    const proto::ProtoObject* sigObj = posArgs->getAt(ctx, 0);
    if (!sigObj->isInteger(ctx)) return PROTO_NONE;
    int sig = static_cast<int>(sigObj->asLong(ctx));
    
    if (s_signalHandlers.count(sig)) return s_signalHandlers[sig];
    return PROTO_NONE;
}

const proto::ProtoObject* initialize(proto::ProtoContext* ctx) {
    const proto::ProtoObject* mod = ctx->newObject(true);
    
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "signal"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_signal));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "getsignal"),
        ctx->fromMethod(const_cast<proto::ProtoObject*>(mod), py_getsig));
        
    // Constants
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "SIGINT"), ctx->fromInteger(SIGINT));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "SIGTERM"), ctx->fromInteger(SIGTERM));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "SIG_DFL"), ctx->fromInteger(0));
    mod = mod->setAttribute(ctx, proto::ProtoString::fromUTF8String(ctx, "SIG_IGN"), ctx->fromInteger(1));
    
    return mod;
}

} // namespace signal_module
} // namespace protoPython
