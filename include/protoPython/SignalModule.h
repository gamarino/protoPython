#ifndef PROTOPYTHON_SIGNALMODULE_H
#define PROTOPYTHON_SIGNALMODULE_H

#include <protoCore.h>

namespace protoPython {
class PythonEnvironment;

namespace signal_module {

const proto::ProtoObject* initialize(proto::ProtoContext* ctx);

// Cooperative signal-delivery hook. The C-level signal handler is
// async-signal-safe (sets a `volatile sig_atomic_t` flag and a global
// pending counter); the actual Python callback is run from the
// interpreter's safepoint, where the GC and call stack are in a
// known-good state. Call this from the bytecode dispatch loop
// whenever it is convenient — every safepoint poll is a natural
// place. Returns true if any handler was dispatched (so the caller
// can re-check pending exceptions).
bool checkAndDeliverPendingSignals(proto::ProtoContext* ctx, PythonEnvironment* env);

// Cheap predicate: true if at least one signal flag is set. Lets the
// dispatcher avoid the function-call overhead when nothing is pending
// (the common case).
bool hasPendingSignal();

} // namespace signal_module
} // namespace protoPython

#endif // PROTOPYTHON_SIGNALMODULE_H
