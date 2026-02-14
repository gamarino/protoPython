/*
 * ThreadingStrategy.h
 *
 * Re-architecture: no mutexes in the task dispatch path. This module defines
 * the interface for bulk task submission to be backed by protoCore's native
 * Work-Stealing Scheduler once available. Until then, tasks run inline on the
 * calling thread (zero contention, no locks).
 *
 * See docs/REARCHITECTURE_PROTOCORE.md.
 */

#ifndef PROTOPYTHON_THREADINGSTRATEGY_H
#define PROTOPYTHON_THREADINGSTRATEGY_H

#include <protoCore.h>
#include <cstddef>
#include <cstdint>

namespace protoPython {

/** Opaque task handle for bulk bytecode execution. Fits in a single cache line when possible. */
struct alignas(64) ExecutionTask {
    proto::ProtoContext* ctx{nullptr};
    const proto::ProtoList* constants{nullptr};
    const proto::ProtoList* bytecode{nullptr};
    const proto::ProtoList* names{nullptr};
    proto::ProtoObject* frame{nullptr};
    uint64_t pcStart{0};
    uint64_t pcEnd{0};
    /* Reserved for future: stack snapshot, result slot, dependency id. */
    char padding[64 - (5 * sizeof(void*) + 2 * sizeof(uint64_t))];
};

/**
 * Run a single task inline on the current thread. No mutex, no queue.
 * Intended to be replaced by protoCore Work-Stealing Scheduler: the scheduler
 * will pull tasks from a lock-free queue and run them on worker threads, each
 * with its own LocalHeap.
 */
void runTaskInline(ExecutionTask* task, const proto::ProtoObject** resultOut);

/**
 * Submit a task for execution. Current implementation: runs inline (runTaskInline).
 * Future: push to protoCore's work-stealing queue; no mutex in submit path.
 */
void submitTask(ExecutionTask* task);

/** Number of worker threads to use when protoCore scheduler is integrated. 0 = inline only. */
int getWorkerCount();

} // namespace protoPython

#endif
