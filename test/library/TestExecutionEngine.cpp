#include <gtest/gtest.h>
#include <protoPython/ExecutionEngine.h>
#include <protoPython/Compiler.h>
#include <protoPython/Parser.h>
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>
#include <array>
#include <mutex>
#include <thread>

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

TEST(ExecutionEngineTest, BinarySubtract) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(10))
        ->appendLast(&ctx, ctx.fromInteger(3));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BINARY_SUBTRACT))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 7);
}

TEST(ExecutionEngineTest, BinaryTrueDivide) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(10))
        ->appendLast(&ctx, ctx.fromInteger(2));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BINARY_TRUE_DIVIDE))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    double val = result->isDouble(&ctx) ? result->asDouble(&ctx) : static_cast<double>(result->asLong(&ctx));
    EXPECT_DOUBLE_EQ(val, 5.0);
}

TEST(ExecutionEngineTest, BinaryPower) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(2))
        ->appendLast(&ctx, ctx.fromInteger(10));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BINARY_POWER))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 1024);
}

TEST(ExecutionEngineTest, BinaryFloorDivide) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(10))
        ->appendLast(&ctx, ctx.fromInteger(3));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BINARY_FLOOR_DIVIDE))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 3);
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

TEST(ExecutionEngineTest, LoadAttr) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoObject* obj = ctx.newObject(true);
    proto::ProtoObject* mutableObj = const_cast<proto::ProtoObject*>(obj);
    mutableObj->setAttribute(&ctx, proto::ProtoString::fromUTF8String(&ctx, "x"), ctx.fromInteger(42));
    const proto::ProtoList* constants = ctx.newList()->appendLast(&ctx, obj);
    const proto::ProtoList* names = ctx.newList()->appendLast(&ctx, ctx.fromUTF8String("x"));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_ATTR))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, names, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 42);
}

TEST(ExecutionEngineTest, StoreAttr) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoObject* obj = ctx.newObject(true);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, obj)
        ->appendLast(&ctx, ctx.fromInteger(99));
    const proto::ProtoList* names = ctx.newList()->appendLast(&ctx, ctx.fromUTF8String("x"));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_STORE_ATTR))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_ATTR))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, names, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 99);
}

TEST(ExecutionEngineTest, BuildList) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(2))
        ->appendLast(&ctx, ctx.fromInteger(3));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(2))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BUILD_LIST))->appendLast(&ctx, ctx.fromInteger(3))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    const proto::ProtoObject* data = result->getAttribute(&ctx, proto::ProtoString::fromUTF8String(&ctx, "__data__"));
    ASSERT_NE(data, nullptr);
    const proto::ProtoList* list = data->asList(&ctx);
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->getSize(&ctx), 3u);
    EXPECT_EQ(list->getAt(&ctx, 0)->asLong(&ctx), 1);
    EXPECT_EQ(list->getAt(&ctx, 1)->asLong(&ctx), 2);
    EXPECT_EQ(list->getAt(&ctx, 2)->asLong(&ctx), 3);
}

TEST(ExecutionEngineTest, BinarySubscr) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(2))
        ->appendLast(&ctx, ctx.fromInteger(3))
        ->appendLast(&ctx, ctx.fromInteger(1));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(2))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BUILD_LIST))->appendLast(&ctx, ctx.fromInteger(3))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(3))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BINARY_SUBSCR))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 2);
}

TEST(ExecutionEngineTest, BuildMap) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromUTF8String("a"))
        ->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromUTF8String("b"))
        ->appendLast(&ctx, ctx.fromInteger(2));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(2))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(3))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BUILD_MAP))->appendLast(&ctx, ctx.fromInteger(2))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    const proto::ProtoObject* data = result->getAttribute(&ctx, proto::ProtoString::fromUTF8String(&ctx, "__data__"));
    ASSERT_NE(data, nullptr);
    ASSERT_NE(data->asSparseList(&ctx), nullptr);
    const proto::ProtoObject* keys = result->getAttribute(&ctx, proto::ProtoString::fromUTF8String(&ctx, "__keys__"));
    ASSERT_NE(keys, nullptr);
    ASSERT_NE(keys->asList(&ctx), nullptr);
    const proto::ProtoList* keysList = keys->asList(&ctx);
    EXPECT_EQ(keysList->getSize(&ctx), 2u);
    unsigned long h0 = keysList->getAt(&ctx, 0)->getHash(&ctx);
    unsigned long h1 = keysList->getAt(&ctx, 1)->getHash(&ctx);
    const proto::ProtoObject* v0 = data->asSparseList(&ctx)->getAt(&ctx, h0);
    const proto::ProtoObject* v1 = data->asSparseList(&ctx)->getAt(&ctx, h1);
    ASSERT_NE(v0, nullptr);
    ASSERT_NE(v1, nullptr);
    long a0 = v0->asLong(&ctx);
    long a1 = v1->asLong(&ctx);
    EXPECT_TRUE((a0 == 1 && a1 == 2) || (a0 == 2 && a1 == 1));
}

TEST(ExecutionEngineTest, BuildTuple) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(10))
        ->appendLast(&ctx, ctx.fromInteger(20));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BUILD_TUPLE))->appendLast(&ctx, ctx.fromInteger(2))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result->asTuple(&ctx), nullptr);
    const proto::ProtoTuple* tup = result->asTuple(&ctx);
    EXPECT_EQ(tup->getSize(&ctx), 2u);
    EXPECT_EQ(tup->getAt(&ctx, 0)->asLong(&ctx), 10);
    EXPECT_EQ(tup->getAt(&ctx, 1)->asLong(&ctx), 20);
}

TEST(ExecutionEngineTest, UnpackSequence) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* names = ctx.newList()
        ->appendLast(&ctx, ctx.fromUTF8String("a"))
        ->appendLast(&ctx, ctx.fromUTF8String("b"));
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(10))
        ->appendLast(&ctx, ctx.fromInteger(20));
    const proto::ProtoObject* frameObj = ctx.newObject(true);
    proto::ProtoObject* frame = const_cast<proto::ProtoObject*>(frameObj);
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BUILD_TUPLE))->appendLast(&ctx, ctx.fromInteger(2))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_UNPACK_SEQUENCE))->appendLast(&ctx, ctx.fromInteger(2))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_STORE_NAME))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_STORE_NAME))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_NAME))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_NAME))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BINARY_ADD))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, names, frame);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 30);
}

TEST(ExecutionEngineTest, LoadGlobalStoreGlobal) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* names = ctx.newList()->appendLast(&ctx, ctx.fromUTF8String("x"));
    const proto::ProtoList* constants = ctx.newList()->appendLast(&ctx, ctx.fromInteger(99));
    const proto::ProtoObject* frameObj = ctx.newObject(true);
    proto::ProtoObject* frame = const_cast<proto::ProtoObject*>(frameObj);
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_STORE_GLOBAL))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_GLOBAL))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, names, frame);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 99);
}

TEST(ExecutionEngineTest, BuildSlice) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(4));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BUILD_SLICE))->appendLast(&ctx, ctx.fromInteger(2))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    const proto::ProtoObject* start = result->getAttribute(&ctx, proto::ProtoString::fromUTF8String(&ctx, "start"));
    const proto::ProtoObject* stop = result->getAttribute(&ctx, proto::ProtoString::fromUTF8String(&ctx, "stop"));
    const proto::ProtoObject* step = result->getAttribute(&ctx, proto::ProtoString::fromUTF8String(&ctx, "step"));
    ASSERT_NE(start, nullptr);
    ASSERT_NE(stop, nullptr);
    ASSERT_NE(step, nullptr);
    EXPECT_EQ(start->asLong(&ctx), 1);
    EXPECT_EQ(stop->asLong(&ctx), 4);
    EXPECT_EQ(step->asLong(&ctx), 1);
}

TEST(ExecutionEngineTest, RotTwo) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(10))
        ->appendLast(&ctx, ctx.fromInteger(20));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_ROT_TWO))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 10);
}

TEST(ExecutionEngineTest, DupTop) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()->appendLast(&ctx, ctx.fromInteger(7));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_DUP_TOP))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BINARY_ADD))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 14);
}

TEST(ExecutionEngineTest, UnaryInvert) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()->appendLast(&ctx, ctx.fromInteger(5));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_UNARY_INVERT))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), -6);  // In Python ~5 == -6
}

TEST(ExecutionEngineTest, PopTop) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(2));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_POP_TOP))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 1);  // TOS was 2, popped; return 1
}

TEST(ExecutionEngineTest, InplaceAdd) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(1000000010))
        ->appendLast(&ctx, ctx.fromInteger(3));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_INPLACE_ADD))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 1000000013);
}

TEST(ExecutionEngineTest, Nop) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()->appendLast(&ctx, ctx.fromInteger(7));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_NOP))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 7);
}

TEST(ExecutionEngineTest, BinaryLshift) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(3));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BINARY_LSHIFT))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 8);  // 1 << 3 == 8
}

TEST(ExecutionEngineTest, InplaceSubtract) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(10))
        ->appendLast(&ctx, ctx.fromInteger(3));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_INPLACE_SUBTRACT))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 7);
}

TEST(ExecutionEngineTest, BinaryAnd) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(12))
        ->appendLast(&ctx, ctx.fromInteger(10));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BINARY_AND))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 8);  // 12 & 10 == 8
}

TEST(ExecutionEngineTest, InplaceMultiply) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(3))
        ->appendLast(&ctx, ctx.fromInteger(4));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_INPLACE_MULTIPLY))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 12);  // 3 * 4 == 12
}

TEST(ExecutionEngineTest, InplaceTrueDivide) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromDouble(10.0))
        ->appendLast(&ctx, ctx.fromDouble(4.0));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_INPLACE_TRUE_DIVIDE))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    double val = result->isDouble(&ctx) ? result->asDouble(&ctx) : static_cast<double>(result->asLong(&ctx));
    EXPECT_NEAR(val, 2.5, 1e-10);  // 10 / 4 == 2.5 (true divide)
}

TEST(ExecutionEngineTest, InplaceModulo) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(17))
        ->appendLast(&ctx, ctx.fromInteger(5));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_INPLACE_MODULO))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 2);  // 17 % 5 == 2
}

TEST(ExecutionEngineTest, InplaceLshift) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(4))
        ->appendLast(&ctx, ctx.fromInteger(2));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_INPLACE_LSHIFT))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 16);  // 4 << 2 == 16
}

TEST(ExecutionEngineTest, InplaceAnd) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(15))
        ->appendLast(&ctx, ctx.fromInteger(7));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_INPLACE_AND))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 7);  // 15 & 7 == 7
}

TEST(ExecutionEngineTest, RotThree) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(2))
        ->appendLast(&ctx, ctx.fromInteger(3));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(2))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_ROT_THREE))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 1);  // stack 1,2,3 (3 top); ROT_THREE -> 2,3,1; top=1
}

TEST(ExecutionEngineTest, RotFour) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(2))
        ->appendLast(&ctx, ctx.fromInteger(3))
        ->appendLast(&ctx, ctx.fromInteger(4));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(2))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(3))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_ROT_FOUR))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 1);  // stack 1,2,3,4 (4 top); ROT_FOUR lifts fourth to top -> top=1
}

TEST(ExecutionEngineTest, DupTopTwo) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(10))
        ->appendLast(&ctx, ctx.fromInteger(20));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_DUP_TOP_TWO))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 20);  // stack was 10,20; DUP_TOP_TWO -> 10,20,10,20; top=20
}

TEST(ExecutionEngineTest, StoreSubscr) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromDouble(10.0))
        ->appendLast(&ctx, ctx.fromDouble(20.0))
        ->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromDouble(99.0));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BUILD_LIST))->appendLast(&ctx, ctx.fromInteger(2))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_DUP_TOP))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(2))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(3))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_STORE_SUBSCR))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(2))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BINARY_SUBSCR))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isDouble(&ctx) || result->isInteger(&ctx));
    if (result->isDouble(&ctx))
        EXPECT_NEAR(result->asDouble(&ctx), 10.0, 1e-10);
    else
        EXPECT_TRUE(result->asLong(&ctx) == 10 || result->asLong(&ctx) == 0);
}

TEST(ExecutionEngineTest, UnaryNegative) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()->appendLast(&ctx, ctx.fromInteger(42));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_UNARY_NEGATIVE))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), -42);
}

TEST(ExecutionEngineTest, UnaryNot) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(1));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_UNARY_NOT))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result, PROTO_TRUE);
    const proto::ProtoList* bytecode2 = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_UNARY_NOT))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result2 = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode2, nullptr, nullptr);
    ASSERT_NE(result2, nullptr);
    EXPECT_EQ(result2, PROTO_FALSE);
}

TEST(ExecutionEngineTest, UnaryPositive) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()->appendLast(&ctx, ctx.fromInteger(1000007));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_UNARY_POSITIVE))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 1000007);
}

TEST(ExecutionEngineTest, BinaryRshift) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(8))
        ->appendLast(&ctx, ctx.fromInteger(2));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BINARY_RSHIFT))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 2);
}

TEST(ExecutionEngineTest, BinaryOr) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(4))
        ->appendLast(&ctx, ctx.fromInteger(8));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BINARY_OR))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 12);
}

TEST(ExecutionEngineTest, BinaryXor) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(12))
        ->appendLast(&ctx, ctx.fromInteger(10));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BINARY_XOR))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 6);
}

TEST(ExecutionEngineTest, BinaryModulo) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(17))
        ->appendLast(&ctx, ctx.fromInteger(5));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BINARY_MODULO))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 2);
}

TEST(ExecutionEngineTest, InplaceFloorDivide) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(1000010))
        ->appendLast(&ctx, ctx.fromInteger(4));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_INPLACE_FLOOR_DIVIDE))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 250002);
}

TEST(ExecutionEngineTest, InplacePower) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(1000002))
        ->appendLast(&ctx, ctx.fromInteger(1));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_INPLACE_POWER))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 1000002);
}

TEST(ExecutionEngineTest, InplaceRshift) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(1000008))
        ->appendLast(&ctx, ctx.fromInteger(2));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_INPLACE_RSHIFT))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 250002);
}

TEST(ExecutionEngineTest, InplaceOr) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(1000004))
        ->appendLast(&ctx, ctx.fromInteger(8));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_INPLACE_OR))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 1000012);
}

TEST(ExecutionEngineTest, InplaceXor) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(1000012))
        ->appendLast(&ctx, ctx.fromInteger(10));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_INPLACE_XOR))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 1000006);  // 1000012 ^ 10 == 1000006
}

TEST(ExecutionEngineTest, PopJumpIfFalse) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(2));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_POP_JUMP_IF_FALSE))->appendLast(&ctx, ctx.fromInteger(4))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(2))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE))->appendLast(&ctx, ctx.fromInteger(0));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 2);
}

TEST(ExecutionEngineTest, JumpAbsolute) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()->appendLast(&ctx, ctx.fromInteger(42));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_JUMP_ABSOLUTE))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isInteger(&ctx));
    EXPECT_EQ(result->asLong(&ctx), 42);
}

// GET_ITER and FOR_ITER: The execution engine builds raw list objects (with __data__ only) in BUILD_LIST,
// so they do not have __iter__/__next__. These opcodes are exercised when running real Python scripts
// where lists come from the PythonEnvironment (full list prototype with __iter__).
TEST(ExecutionEngineTest, GetIterNoCrash) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(2));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BUILD_LIST))->appendLast(&ctx, ctx.fromInteger(2))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_GET_ITER))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    const proto::ProtoObject* result = protoPython::executeMinimalBytecode(
        &ctx, constants, bytecode, nullptr, nullptr);
    (void)result;
    EXPECT_TRUE(result == nullptr || result == PROTO_NONE);
}

// Step 5: Hand-built bytecode for assignment then run (isolates engine vs compiler).
TEST(ExecutionEngineTest, HandBuiltStoreNameInFrame) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    const proto::ProtoList* constants = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(2));
    const proto::ProtoList* names = ctx.newList()
        ->appendLast(&ctx, reinterpret_cast<const proto::ProtoObject*>(proto::ProtoString::fromUTF8String(&ctx, "a")));
    const proto::ProtoList* bytecode = ctx.newList()
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_LOAD_CONST))->appendLast(&ctx, ctx.fromInteger(1))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_BINARY_ADD))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_STORE_NAME))->appendLast(&ctx, ctx.fromInteger(0))
        ->appendLast(&ctx, ctx.fromInteger(protoPython::OP_RETURN_VALUE));
    proto::ProtoObject* frame = const_cast<proto::ProtoObject*>(ctx.newObject(true));
    protoPython::executeMinimalBytecode(&ctx, constants, bytecode, names, frame);
    const proto::ProtoObject* aVal = frame->getAttribute(&ctx, proto::ProtoString::fromUTF8String(&ctx, "a"));
    ASSERT_NE(aVal, nullptr);
    EXPECT_TRUE(aVal->isInteger(&ctx));
    EXPECT_EQ(aVal->asLong(&ctx), 3);
}

// Step 5: Parser/compiler emit full opcode set — compile and run a simple assignment.
TEST(ExecutionEngineTest, CompiledAssignment) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    protoPython::Parser parser("a = 1 + 2");
    std::unique_ptr<protoPython::ModuleNode> mod = parser.parseModule();
    ASSERT_NE(mod, nullptr);
    ASSERT_FALSE(mod->body.empty());
    protoPython::Compiler compiler(&ctx, "<test>");
    ASSERT_TRUE(compiler.compileModule(mod.get()));
    const proto::ProtoObject* codeObj = protoPython::makeCodeObject(
        &ctx, compiler.getConstants(), compiler.getNames(), compiler.getBytecode());
    ASSERT_NE(codeObj, nullptr);
    proto::ProtoObject* frame = const_cast<proto::ProtoObject*>(ctx.newObject(true));
    protoPython::runCodeObject(&ctx, codeObj, frame);
    const proto::ProtoObject* aVal = frame->getAttribute(&ctx,
        proto::ProtoString::fromUTF8String(&ctx, "a"));
    ASSERT_NE(aVal, nullptr);
    ASSERT_TRUE(aVal->isInteger(&ctx));
    EXPECT_EQ(aVal->asLong(&ctx), 3);
}

// Step 5: Compile and run list literal expression (engine returns object with __data__).
TEST(ExecutionEngineTest, CompiledListLiteral) {
    proto::ProtoSpace space;
    proto::ProtoContext ctx(&space);
    protoPython::Parser parser("[1, 2, 3]");
    std::unique_ptr<protoPython::ASTNode> expr = parser.parseExpression();
    ASSERT_NE(expr, nullptr);
    protoPython::Compiler compiler(&ctx, "<test>");
    ASSERT_TRUE(compiler.compileExpression(expr.get()));
    const proto::ProtoObject* codeObj = protoPython::makeCodeObject(
        &ctx, compiler.getConstants(), compiler.getNames(), compiler.getBytecode());
    ASSERT_NE(codeObj, nullptr);
    proto::ProtoObject* frame = const_cast<proto::ProtoObject*>(ctx.newObject(true));
    const proto::ProtoObject* result = protoPython::runCodeObject(&ctx, codeObj, frame);
    ASSERT_NE(result, nullptr);
    ASSERT_NE(result, PROTO_NONE);
    const proto::ProtoObject* data = result->getAttribute(&ctx,
        proto::ProtoString::fromUTF8String(&ctx, "__data__"));
    ASSERT_NE(data, nullptr);
    const proto::ProtoList* list = data->asList(&ctx);
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->getSize(&ctx), 3);
    EXPECT_EQ(list->getAt(&ctx, 0)->asLong(&ctx), 1);
    EXPECT_EQ(list->getAt(&ctx, 1)->asLong(&ctx), 2);
    EXPECT_EQ(list->getAt(&ctx, 2)->asLong(&ctx), 3);
}

// Phase 4: Concurrent execution from multiple threads with a single PythonEnvironment
// and single ProtoContext. ProtoObjects and GC depend on ProtoSpace, ProtoContext, and
// ProtoThread (per-thread allocation); sharing one context from multiple OS threads
// without per-thread ProtoThread registration would corrupt allocation. We therefore
// serialize the actual compile+run so only one thread allocates at a time, while still
// launching two threads that both call into the env (stressing context map, resolve
// cache, trace) and run independent code on distinct frames.
#ifndef STDLIB_PATH
#define STDLIB_PATH ""
#endif
TEST(ExecutionEngineTest, ConcurrentExecutionTwoThreads) {
    protoPython::PythonEnvironment env(STDLIB_PATH);
    proto::ProtoContext* ctx = env.getContext();
    std::array<proto::ProtoObject*, 2> frames = {nullptr, nullptr};
    std::mutex runMutex;

    auto runOne = [ctx, &frames, &runMutex](size_t idx, const std::string& source) {
        protoPython::Parser parser(source);
        std::unique_ptr<protoPython::ModuleNode> mod = parser.parseModule();
        if (!mod || mod->body.empty()) return;
        const proto::ProtoObject* codeObj = nullptr;
        {
            std::lock_guard<std::mutex> lock(runMutex);
            protoPython::Compiler compiler(ctx, "<concurrent_test>");
            if (!compiler.compileModule(mod.get())) return;
            codeObj = protoPython::makeCodeObject(
                ctx, compiler.getConstants(), compiler.getNames(), compiler.getBytecode());
        }
        if (!codeObj) return;
        proto::ProtoObject* frame = nullptr;
        {
            std::lock_guard<std::mutex> lock(runMutex);
            frame = const_cast<proto::ProtoObject*>(ctx->newObject(true));
            protoPython::runCodeObject(ctx, codeObj, frame);
            frames[idx] = frame;
        }
    };

    std::thread t1(runOne, 0, "a = 1");
    std::thread t2(runOne, 1, "b = 2");
    t1.join();
    t2.join();

    ASSERT_NE(frames[0], nullptr);
    ASSERT_NE(frames[1], nullptr);
    const proto::ProtoObject* aVal = frames[0]->getAttribute(ctx,
        proto::ProtoString::fromUTF8String(ctx, "a"));
    const proto::ProtoObject* bVal = frames[1]->getAttribute(ctx,
        proto::ProtoString::fromUTF8String(ctx, "b"));
    ASSERT_NE(aVal, nullptr);
    ASSERT_NE(bVal, nullptr);
    EXPECT_TRUE(aVal->isInteger(ctx));
    EXPECT_TRUE(bVal->isInteger(ctx));
    EXPECT_EQ(aVal->asLong(ctx), 1);
    EXPECT_EQ(bVal->asLong(ctx), 2);
}
