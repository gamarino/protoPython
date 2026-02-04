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

