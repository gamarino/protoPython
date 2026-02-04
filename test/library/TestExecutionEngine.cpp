#include <gtest/gtest.h>
#include <protoPython/ExecutionEngine.h>
#include <protoCore.h>

TEST(ExecutionEngineTest, LoadConstReturnValue) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(42))
        ->appendLast(&ctx, ctx.fromUTF8String("hello"));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))
        ->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(&ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 42);
}

TEST(ExecutionEngineTest, LoadConstString) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()->appendLast(&ctx, ctx.fromUTF8String("ok"));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))
        ->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(&ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isString(&ctx));
    std::string s;
    result->asString(&ctx)->toUTF8String(&ctx, s);
    EXPECT_EQ(s, "ok");
}

TEST(ExecutionEngineTest, BinaryMultiply) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(6))
        ->appendLast(&ctx, ctx.fromInteger(7));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BINARY_MULTIPLY))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(&ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 42);
}

TEST(ExecutionEngineTest, CompareOp) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(3))
        ->appendLast(&ctx, ctx.fromInteger(5));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_COMPARE_OP))->appendLast(&ctx, ctx.fromInteger(2))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(&ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result, PROTO_TRUE);
}
