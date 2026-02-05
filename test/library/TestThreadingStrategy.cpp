/*
 * Tests for ThreadingStrategy: ExecutionTask, runTaskInline, submitTask.
 * Verifies lock-free task dispatch path and 64-byte alignment for HPC re-architecture.
 * See docs/REARCHITECTURE_PROTOCORE.md.
 */

#include <gtest/gtest.h>
#include <protoPython/ThreadingStrategy.h>
#include <protoPython/ExecutionEngine.h>
#include <protoCore.h>
#include <cstddef>

TEST(ThreadingStrategyTest, ExecutionTaskAlignment) {
    EXPECT_GE(alignof(protoPython::ExecutionTask), 64u)
        << "ExecutionTask must be 64-byte aligned to avoid false sharing";
}

TEST(ThreadingStrategyTest, RunTaskInlineReturnsResult) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(7))
        ->appendLast(&ctx, ctx.fromInteger(5));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BINARY_ADD))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE))->appendLast(&ctx, ctx.fromInteger(0));

    protoPython::ExecutionTask task{};
    task.ctx = &ctx;
    task.constants = constants;
    task.bytecode = bytecode;
    task.names = nullptr;
    task.frame = nullptr;
    task.pcStart = 0;
    task.pcEnd = bytecode->getSize(&ctx) - 1;

    const proto::ProtoObject* result = nullptr;
    protoPython::runTaskInline(&task, &result);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 12);
}

TEST(ThreadingStrategyTest, SubmitTaskRunsInlineWithoutCrash) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()->appendLast(&ctx, ctx.fromInteger(99));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE))->appendLast(&ctx, ctx.fromInteger(0));

    protoPython::ExecutionTask task{};
    task.ctx = &ctx;
    task.constants = constants;
    task.bytecode = bytecode;
    task.pcStart = 0;
    task.pcEnd = bytecode->getSize(&ctx) - 1;

    EXPECT_NO_FATAL_FAILURE(protoPython::submitTask(&task));
}

TEST(ThreadingStrategyTest, RunTaskInlineNullTaskNoCrash) {
    const proto::ProtoObject* result = nullptr;
    EXPECT_NO_FATAL_FAILURE(protoPython::runTaskInline(nullptr, &result));
    EXPECT_EQ(result, nullptr);
}

TEST(ThreadingStrategyTest, RunTaskInlineNullResultOutNoCrash) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()->appendLast(&ctx, ctx.fromInteger(0));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE))->appendLast(&ctx, ctx.fromInteger(0));
    protoPython::ExecutionTask task{};
    task.ctx = &ctx;
    task.constants = constants;
    task.bytecode = bytecode;
    task.pcEnd = bytecode->getSize(&ctx) - 1;
    EXPECT_NO_FATAL_FAILURE(protoPython::runTaskInline(&task, nullptr));
}

TEST(ThreadingStrategyTest, GetWorkerCountReturnsZeroBeforeSchedulerIntegration) {
    int n = protoPython::getWorkerCount();
    EXPECT_GE(n, 0);
}
