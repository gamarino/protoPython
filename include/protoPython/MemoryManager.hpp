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
 * Use stack-allocated storage for the context so the destructor always runs.
 */
class ContextScope {
public:
    /** Construct a new context with previous = parent, and set it as current on parent's thread.
     * Parameters match ProtoContext(space, previous, parameterNames, localNames, args, kwargs).
     * May throw if ProtoContext constructor throws. */
    ContextScope(proto::ProtoSpace* space,
                 proto::ProtoContext* parent,
                 const proto::ProtoList* parameterNames,
                 const proto::ProtoList* localNames,
                 const proto::ProtoList* args,
                 const proto::ProtoSparseList* kwargs)
        : parent_(parent)
        , thread_(parent ? parent->thread : nullptr)
        , constructed_(false) {
        new (storage_) proto::ProtoContext(space, parent, parameterNames, localNames, args, kwargs);
        constructed_ = true;
    }

    ~ContextScope() {
        if (constructed_) {
            if (thread_)
                thread_->setCurrentContext(parent_);
            context()->proto::ProtoContext::~ProtoContext();
            constructed_ = false;
        }
    }

    ContextScope(const ContextScope&) = delete;
    ContextScope& operator=(const ContextScope&) = delete;

    proto::ProtoContext* context() {
        return reinterpret_cast<proto::ProtoContext*>(storage_);
    }
    const proto::ProtoContext* context() const {
        return reinterpret_cast<const proto::ProtoContext*>(storage_);
    }

private:
    proto::ProtoContext* parent_;
    proto::ProtoThread* thread_;
    bool constructed_;
    alignas(proto::ProtoContext) unsigned char storage_[sizeof(proto::ProtoContext)];
};

} // namespace protoPython

#endif
