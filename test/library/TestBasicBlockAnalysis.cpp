/*
 * Tests for BasicBlockAnalysis: basic block boundary computation from bytecode.
 * See docs/REARCHITECTURE_PROTOCORE.md (task batching, compiler basic-block pass).
 */

#include <gtest/gtest.h>
#include <protoPython/BasicBlockAnalysis.h>
#include <protoPython/ExecutionEngine.h>
#include <protoCore.h>

TEST(BasicBlockAnalysisTest, EmptyBytecodeReturnsEmpty) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* emptyCode = ctx.newList();
    auto blocks = protoPython::getBasicBlockBoundaries(&ctx, emptyCode);
    EXPECT_TRUE(blocks.empty());
}

TEST(BasicBlockAnalysisTest, SingleReturnOneBlock) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE))->appendLast(&ctx, ctx.fromInteger(0));
    auto blocks = protoPython::getBasicBlockBoundaries(&ctx, bytecode);
    ASSERT_EQ(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].first, 0u);
    EXPECT_EQ(blocks[0].second, 2u);
}

TEST(BasicBlockAnalysisTest, JumpAbsoluteTwoBlocks) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    // 0: JUMP_ABSOLUTE 2, 1: arg(2), 2: LOAD_CONST 0, 3: arg(0), 4: RETURN_VALUE, 5: arg(0)
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_JUMP_ABSOLUTE))->appendLast(&ctx, ctx.fromInteger(2))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE))->appendLast(&ctx, ctx.fromInteger(0));
    auto blocks = protoPython::getBasicBlockBoundaries(&ctx, bytecode);
    ASSERT_GE(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].first, 0u);
    EXPECT_EQ(blocks[0].second, 0u);
    bool hasBlock2 = false;
    for (const auto& b : blocks) {
        if (b.first == 2) {
            hasBlock2 = true;
            EXPECT_EQ(b.second, 4u);  /* block ends at RETURN_VALUE opcode index */
            break;
        }
    }
    EXPECT_TRUE(hasBlock2);
}

TEST(BasicBlockAnalysisTest, PopJumpIfFalseAddsTargetAsBlockStart) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    // 0: LOAD_CONST 0, 1: 0, 2: POP_JUMP_IF_FALSE 6, 3: 6, 4: LOAD_CONST 1, 5: 1, 6: RETURN_VALUE, 7: 0
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_POP_JUMP_IF_FALSE))->appendLast(&ctx, ctx.fromInteger(6))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE))->appendLast(&ctx, ctx.fromInteger(0));
    auto blocks = protoPython::getBasicBlockBoundaries(&ctx, bytecode);
    ASSERT_GE(blocks.size(), 1u);
    EXPECT_EQ(blocks[0].first, 0u);
    bool hasBlock6 = false;
    for (const auto& b : blocks) {
        if (b.first == 6u) {
            hasBlock6 = true;
            EXPECT_EQ(b.second, 6u);  /* block is just RETURN_VALUE at index 6 */
            break;
        }
    }
    EXPECT_TRUE(hasBlock6);
}

TEST(BasicBlockAnalysisTest, NullContextReturnsEmpty) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE))->appendLast(&ctx, ctx.fromInteger(0));
    auto blocks = protoPython::getBasicBlockBoundaries(nullptr, bytecode);
    EXPECT_TRUE(blocks.empty());
}

TEST(BasicBlockAnalysisTest, NullBytecodeReturnsEmpty) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    auto blocks = protoPython::getBasicBlockBoundaries(&ctx, nullptr);
    EXPECT_TRUE(blocks.empty());
}
