#ifndef PROTOPYTHON_MEMORYMANAGER_H
#define PROTOPYTHON_MEMORYMANAGER_H

/**
 * L-Shape architecture: context stack and object promotion.
 * - Objects allocated during a scope are owned by that context.
 * - Only the promoted object (return value) is transferred to the parent; zero-copy.
 * - Cleanup at scope exit is deterministic (RAII).
 */

#include <protoCore.h>
#include <new>
#include <cstddef>
#include <algorithm>

namespace protoPython {

/** Set the return value for the given context. When the context is destroyed (scope exit),
 * the destructor will promote this object to the parent (zero-copy). ctx must be the
 * active context for the scope that is about to be left. */
inline void promote(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (ctx)
        const_cast<proto::ProtoContext*>(ctx)->returnValue = obj;
}

/**
 * RAII scope for a callee ProtoContext. On construction, pushes a new context
 * (parent = caller); on destruction, restores the thread's current context to parent
 * and destroys the callee context (GC and promotion run in ~ProtoContext).
 *
 * Small-buffer optimisation (SBO): for functions with <= SBO_SLOTS local variables,
 * both the ProtoContext struct and its slot array are allocated on the C++ call stack
 * — zero heap allocation per function call on the common path.
 *
 * For functions with more than SBO_SLOTS locals (rare), the slots fall back to heap
 * while the ProtoContext struct itself still lives on the stack.
 */
class ContextScope {
    static constexpr size_t SBO_SLOTS = 64;

    // Stack storage for the ProtoContext struct itself — no heap alloc for the struct.
    alignas(proto::ProtoContext) char ctxStorage_[sizeof(proto::ProtoContext)];

    // Stack storage for the slot array — avoids the internal new[] in ProtoContext.
    // Valid only when totalSlots <= SBO_SLOTS; initialised to PROTO_NONE before use.
    alignas(void*) const proto::ProtoObject* slotsBuf_[SBO_SLOTS];

public:
    ContextScope(proto::ProtoSpace* space,
                 proto::ProtoContext* parent,
                 const proto::ProtoList* parameterNames,
                 const proto::ProtoList* localNames,
                 const proto::ProtoList* args,
                 const proto::ProtoSparseList* kwargs,
                 size_t totalSlots = 0)
        : parent_(parent ? parent : PythonEnvironment::getCurrentContext()),
          ctx_(nullptr) {
        // Prepare the external slot buffer when the count fits in SBO_SLOTS.
        const proto::ProtoObject** extSlots = nullptr;
        if (totalSlots > 0 && totalSlots <= SBO_SLOTS) {
            std::fill(slotsBuf_, slotsBuf_ + totalSlots, PROTO_NONE);
            extSlots = slotsBuf_;
        }
        // Placement-new: ProtoContext lives inside ctxStorage_ (stack memory).
        ctx_ = new (ctxStorage_) proto::ProtoContext(
            space, parent_, parameterNames, localNames, args, kwargs, totalSlots, extSlots);
        // NOTE: setCurrentContext is intentionally omitted. s_threadContext is set once by
        // registerContext() at thread startup and remains valid for the lifetime of the thread.
        // All getPendingException/setPendingException callers only need a valid (non-null)
        // context on the same thread — the root context works fine for those operations.
    }

    ~ContextScope() {
        if (ctx_) {
            // Explicit destructor — context lives in ctxStorage_, not on heap.
            ctx_->~ProtoContext();
            ctx_ = nullptr;
        }
    }

    ContextScope(const ContextScope&) = delete;
    ContextScope& operator=(const ContextScope&) = delete;

    proto::ProtoContext* context() { return ctx_; }
    const proto::ProtoContext* context() const { return ctx_; }

private:
    proto::ProtoContext* parent_;
    proto::ProtoContext* ctx_;
};

} // namespace protoPython

#endif
