#include <gtest/gtest.h>
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>

using namespace protoPython;

class FoundationTest : public ::testing::Test {
protected:
    PythonEnvironment env{STDLIB_PATH};
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

TEST_F(FoundationTest, ModuleImport) {
    proto::ProtoContext* context = env.getContext();
    
    // Attempt to resolve 'os' which should be found in our new StdLib shell
    const proto::ProtoObject* osMod = env.resolve("os");
    ASSERT_NE(osMod, nullptr);
    ASSERT_NE(osMod, PROTO_NONE);
    
    // Check attributes set by PythonModuleProvider
    const proto::ProtoString* nameKey = proto::ProtoString::fromUTF8String(context, "__name__");
    const proto::ProtoObject* nameVal = osMod->getAttribute(context, nameKey);
    ASSERT_NE(nameVal, nullptr);
    ASSERT_TRUE(nameVal->isString(context));
    
    std::string nameStr;
    nameVal->asString(context)->toUTF8String(context, nameStr);
    EXPECT_EQ(nameStr, "os");
    
    const proto::ProtoString* fileKey = proto::ProtoString::fromUTF8String(context, "__file__");
    const proto::ProtoObject* fileVal = osMod->getAttribute(context, fileKey);
    ASSERT_NE(fileVal, nullptr);
    ASSERT_TRUE(fileVal->isString(context));
}

TEST_F(FoundationTest, BuiltinsModule) {
    proto::ProtoContext* context = env.getContext();
    
    // builtins should be resolvable
    const proto::ProtoObject* builtins = env.resolve("builtins");
    ASSERT_NE(builtins, nullptr);
    ASSERT_NE(builtins, PROTO_NONE);
    
    // builtins.int should be the int prototype
    const proto::ProtoObject* pyInt = builtins->getAttribute(context, proto::ProtoString::fromUTF8String(context, "int"));
    EXPECT_EQ(pyInt, env.getIntPrototype());
    
    // resolve("int") should now go through builtins
    EXPECT_EQ(env.resolve("int"), env.getIntPrototype());
}

TEST_F(FoundationTest, SysModule) {
    proto::ProtoContext* context = env.getContext();
    
    const proto::ProtoObject* sys = env.resolve("sys");
    ASSERT_NE(sys, nullptr);
    ASSERT_NE(sys, PROTO_NONE);
    
    // Check sys.platform
    const proto::ProtoObject* platform = sys->getAttribute(context, proto::ProtoString::fromUTF8String(context, "platform"));
    ASSERT_NE(platform, nullptr);
    ASSERT_TRUE(platform->isString(context));
    
    // Check sys.version
    const proto::ProtoObject* version = sys->getAttribute(context, proto::ProtoString::fromUTF8String(context, "version"));
    ASSERT_NE(version, nullptr);
    ASSERT_TRUE(version->isString(context));
}
