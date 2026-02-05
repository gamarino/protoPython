/*
 * ThreadingStrategy.cpp
 *
 * No std::mutex or std::atomic_flag. Task execution is inline until protoCore
 * provides a Work-Stealing Scheduler and per-thread LocalHeap. See
 * docs/REARCHITECTURE_PROTOCORE.md.
 */

#include <protoPython/ThreadingStrategy.h>
#include <protoPython/ExecutionEngine.h>

namespace protoPython {

static int s_workerCount = 0;

void runTaskInline(ExecutionTask* task, const proto::ProtoObject** resultOut) {
    if (!task || !resultOut) return;
    *resultOut = executeBytecodeRange(
        task->ctx,
        task->constants,
        task->bytecode,
        task->names,
        task->frame,
        task->pcStart,
        task->pcEnd
    );
}

void submitTask(ExecutionTask* task) {
    if (!task) return;
    const proto::ProtoObject* result = nullptr;
    runTaskInline(task, &result);
    (void)result;
}

int getWorkerCount() {
    return s_workerCount;
}

} // namespace protoPython
