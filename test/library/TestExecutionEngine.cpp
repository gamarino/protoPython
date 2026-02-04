#include <gtest/gtest.h>
#include <protoPython/ExecutionEngine.h>
#include <protoCore.h>

static const proto::ProtoObject* callable_returns_42(
    proto::ProtoContext* ctx,
    const proto::ProtoObject* /*self*/,
    const proto::ParentLink* /*parent*/,
    const proto::ProtoList* /*args*/,
    const proto::ProtoSparseList* /*kwargs*/) {
    return ctx->fromInteger(42);
}

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

TEST(ExecutionEngineTest, BinaryAdd) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(10))
        ->appendLast(&ctx, ctx.fromInteger(32));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BINARY_ADD))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(&ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 42);
}

TEST(ExecutionEngineTest, LoadNameStoreName) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* names = ctx.newList()
        ->appendLast(&ctx, ctx.fromUTF8String("x"))
        ->appendLast(&ctx, ctx.fromUTF8String("y"));
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(10))
        ->appendLast(&ctx, ctx.fromInteger(32));
    const proto::ProtoObject* frameObj = ctx.newObject(true);
    proto::ProtoObject* frame = const_cast<proto::ProtoObject*>(frameObj);
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_STORE_NAME))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_STORE_NAME))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_NAME))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_NAME))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BINARY_ADD))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, names, frame);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 42);
}

TEST(ExecutionEngineTest, CallFunction) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoObject* callableObj = ctx.newObject(true);
    proto::ProtoObject* mutableCallable = const_cast<proto::ProtoObject*>(callableObj);
    const proto::ProtoObject* method = ctx.fromMethod(mutableCallable, callable_returns_42);
    mutableCallable->setAttribute(&ctx, proto::ProtoString::fromUTF8String(&ctx, "__call__"), method);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, callableObj)
        ->appendLast(&ctx, ctx.fromInteger(0));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_CALL_FUNCTION))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 42);
}
