#include <gtest/gtest.h>
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>

using namespace protoPython;

class FoundationTest : public ::testing::Test {
protected:
    PythonEnvironment env;
};

TEST_F(FoundationTest, BasicTypesExist) {
    EXPECT_NE(env.getObjectPrototype(), nullptr);
    EXPECT_NE(env.getTypePrototype(), nullptr);
    EXPECT_NE(env.getIntPrototype(), nullptr);
    EXPECT_NE(env.getListPrototype(), nullptr);
}

TEST_F(FoundationTest, ListAppend) {
    auto context = env.getContext();
    
    // 1. Create a list instance
    const proto::ProtoObject* my_list = context->newObject(true);
    my_list = my_list->addParent(context, env.getListPrototype());
    
    // 2. Initialize it with empty ProtoList in __data__
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    my_list->setAttribute(context, dataName, context->newList()->asObject(context));
    
    // 3. Call append(42)
    const proto::ProtoString* appendName = proto::ProtoString::fromUTF8String(context, "append");
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromInteger(42));
    
    my_list->call(context, nullptr, appendName, my_list, args);
    
    // 4. Verify result
    const proto::ProtoObject* updatedData = my_list->getAttribute(context, dataName);
    ASSERT_TRUE(updatedData->asList(context));
    const proto::ProtoList* list = updatedData->asList(context);
    EXPECT_EQ(list->getSize(context), 1);
    EXPECT_EQ(list->getAt(context, 0)->asLong(context), 42);
}

TEST_F(FoundationTest, ResolveBuiltins) {
    EXPECT_EQ(env.resolve("int"), env.getIntPrototype());
    EXPECT_EQ(env.resolve("list"), env.getListPrototype());
    EXPECT_EQ(env.resolve("nonexistent"), PROTO_NONE);
}
