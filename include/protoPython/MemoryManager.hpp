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
#if defined(PROTOPY_CHECK_CONTEXT_CHAIN)
#include <cstdio>
#include <cstdlib>
#endif

namespace protoPython {

/** Set the return value for the given context. When the context is destroyed (scope exit),
 * the destructor will promote this object to the parent (zero-copy). ctx must be the
 * active context for the scope that is about to be left. */
inline void promote(proto::ProtoContext* ctx, const proto::ProtoObject* obj) {
    if (ctx)
        const_cast<proto::ProtoContext*>(ctx)->returnValue = obj;
}

/**
 * The context a new ProtoContext must name as its `previous`: the calling
 * thread's current context.
 *
 * protoCore's root scan starts at each thread's current context and follows
 * `previous`, and ~ProtoContext makes `previous` current again. A context
 * created with any other `previous` hides every context between the two --
 * their operand stacks, locals and young generations -- from each collection
 * while it lives, and when it is destroyed it leaves an outer context current
 * while inner frames are still executing, so those frames stay invisible to
 * the collector until they return. Under a heap limit that freed live objects.
 *
 * Native code is routinely handed a context that is not the current one: the
 * interpreter's thread-local context (PythonEnvironment::getCurrentContext,
 * which function calls do not update), a generator's resumer, the context a
 * native method was called with before it re-entered Python. So the parent is
 * taken from the thread, and `ctx` only says which thread (or, for a context
 * without a thread, which space). See test/library/TestContextChain.cpp.
 */
inline proto::ProtoContext* chainParent(proto::ProtoContext* ctx) {
    if (!ctx) return ctx;
    proto::ProtoContext* current = ctx->thread
        ? ctx->thread->getCurrentContext()
        : (ctx->space ? ctx->space->mainContext : nullptr);
#if defined(PROTOPY_CHECK_CONTEXT_CHAIN)
    // The context handed in must still be live on this thread: current, or
    // an ancestor of it. Anything else is a dangling or foreign context.
    bool live = false;
    for (proto::ProtoContext* c = current; c; c = c->previous)
        if (c == ctx) { live = true; break; }
    if (!live) {
        std::fprintf(stderr, "protoPython: context %p handed to a new context is not on the "
                             "thread's chain (current %p)\n", (void*)ctx, (void*)current);
        std::abort();
    }
#endif
    return current ? current : ctx;
}

/**
 * Checks, in builds with PROTOPY_CHECK_CONTEXT_CHAIN, that `ctx` is the
 * thread's current context: contexts are destroyed in the reverse order of
 * their creation, so the one being destroyed must be the innermost.
 */
inline void checkInnermost(proto::ProtoContext* ctx) {
#if defined(PROTOPY_CHECK_CONTEXT_CHAIN)
    proto::ProtoContext* current = ctx->thread
        ? ctx->thread->getCurrentContext()
        : (ctx->space ? ctx->space->mainContext : nullptr);
    if (current != ctx) {
        std::fprintf(stderr, "protoPython: destroying context %p, but the thread's current "
                             "context is %p\n", (void*)ctx, (void*)current);
        std::abort();
    }
#else
    (void)ctx;
#endif
}

/**
 * RAII scope for a callee ProtoContext. On construction, pushes a new context
 * (parent = the thread's current context, see chainParent); on destruction, restores the thread's current context to parent
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
    // SBO_SLOTS caps how many automatic-locals fit on the parent stack frame
    // (small-buffer optimisation) before falling back to ProtoContext's
    // internal heap allocation. Sized to match protoJS's parallel choice
    // (commit b989e88a on protoJS).
    //
    // 64 → 256 (2026-06-15, step #1): perf showed ProtoContext::ProtoContext
    // self-time at 4.5 % on fib(25) AFTER step #3 landed. Most Python
    // functions need 1-3 args + a handful of locals + a small operand stack,
    // well under 64. But functions that even occasionally exceed it spill
    // every call into `new const ProtoObject*[N]` / `delete[]`. Bumping to
    // 256 captures every realistic function's footprint at the cost of
    // 2 KB additional stack per Python call (256 × 8 bytes pointers,
    // negligible against the typical Python call's recursion budget).
    static constexpr size_t SBO_SLOTS = 256;

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
        : parent_(chainParent(parent ? parent : PythonEnvironment::getCurrentContext())),
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
            checkInnermost(ctx_);
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
