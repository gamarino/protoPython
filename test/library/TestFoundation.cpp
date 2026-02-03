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

TEST_F(FoundationTest, BuiltinFunctions) {
    proto::ProtoContext* context = env.getContext();
    
    // Test len() on a list
    const proto::ProtoObject* my_list = context->newObject(true);
    my_list = my_list->addParent(context, env.getListPrototype());
    my_list->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->newList()->appendLast(context, context->fromInteger(1))->asObject(context));
    
    const proto::ProtoObject* pyLen = env.resolve("len");
    ASSERT_NE(pyLen, nullptr);
    
    const proto::ProtoList* args = context->newList()->appendLast(context, my_list);
    const proto::ProtoObject* result = pyLen->asMethod(context)(context, PROTO_NONE, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->asLong(context), 1);
    
    // Test id()
    const proto::ProtoObject* pyId = env.resolve("id");
    ASSERT_NE(pyId, nullptr);
    const proto::ProtoObject* idResult = pyId->asMethod(context)(context, PROTO_NONE, nullptr, args, nullptr);
    ASSERT_NE(idResult, nullptr);
    EXPECT_EQ(idResult->asLong(context), reinterpret_cast<long long>(my_list));
    
    // Test print() - just verify it doesn't crash and returns None
    const proto::ProtoObject* pyPrint = env.resolve("print");
    ASSERT_NE(pyPrint, nullptr);
    const proto::ProtoObject* printResult = pyPrint->asMethod(context)(context, PROTO_NONE, nullptr, args, nullptr);
    EXPECT_EQ(printResult, PROTO_NONE);
}

TEST_F(FoundationTest, AdvancedBuiltins) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* my_list = context->newObject(true)->addParent(context, env.getListPrototype());
    
    // Test isinstance(my_list, list)
    const proto::ProtoObject* pyIsInstance = env.resolve("isinstance");
    ASSERT_NE(pyIsInstance, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, my_list)->appendLast(context, env.getListPrototype());
    const proto::ProtoObject* result = pyIsInstance->asMethod(context)(context, PROTO_NONE, nullptr, args, nullptr);
    EXPECT_EQ(result, PROTO_TRUE);
    
    // Test range(5)
    const proto::ProtoObject* pyRange = env.resolve("range");
    ASSERT_NE(pyRange, nullptr);
    const proto::ProtoList* rangeArgs = context->newList()->appendLast(context, context->fromInteger(5));
    const proto::ProtoObject* rangeObj = pyRange->asMethod(context)(context, PROTO_NONE, nullptr, rangeArgs, nullptr);
    ASSERT_NE(rangeObj, nullptr);
    ASSERT_NE(rangeObj->asList(context), nullptr);
    EXPECT_EQ(rangeObj->asList(context)->getSize(context), 5);
}

TEST_F(FoundationTest, IOModule) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* ioMod = env.resolve("_io");
    ASSERT_NE(ioMod, nullptr);
    ASSERT_NE(ioMod, PROTO_NONE);
    
    // Test _io.open("test.txt")
    const proto::ProtoObject* pyOpen = ioMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "open"));
    ASSERT_NE(pyOpen, nullptr);
    
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromUTF8String("test.txt"));
    const proto::ProtoObject* fileObj = pyOpen->asMethod(context)(context, PROTO_NONE, nullptr, args, nullptr);
    
    ASSERT_NE(fileObj, nullptr);
    const proto::ProtoObject* fileName = fileObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "name"));
    ASSERT_NE(fileName, nullptr);
    std::string name;
    fileName->asString(context)->toUTF8String(context, name);
    EXPECT_EQ(name, "test.txt");
}

TEST_F(FoundationTest, SysPathAndModules) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* sys = env.resolve("sys");
    
    // Check sys.path
    const proto::ProtoObject* pathObj = sys->getAttribute(context, proto::ProtoString::fromUTF8String(context, "path"));
    ASSERT_NE(pathObj, nullptr);
    const proto::ProtoList* path = pathObj->asList(context);
    ASSERT_NE(path, nullptr);
    EXPECT_GT(path->getSize(context), 0);
    
    // Check sys.modules
    const proto::ProtoObject* modules = sys->getAttribute(context, proto::ProtoString::fromUTF8String(context, "modules"));
    ASSERT_NE(modules, nullptr);
    const proto::ProtoObject* sysInModules = modules->getAttribute(context, proto::ProtoString::fromUTF8String(context, "sys"));
    ASSERT_NE(sysInModules, nullptr);
    
    // They should at least have the same version
    const proto::ProtoObject* v1 = sys->getAttribute(context, proto::ProtoString::fromUTF8String(context, "version"));
    const proto::ProtoObject* v2 = sysInModules->getAttribute(context, proto::ProtoString::fromUTF8String(context, "version"));
    EXPECT_EQ(v1, v2);
}

TEST_F(FoundationTest, SysTraceHooks) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* sys = env.resolve("sys");
    
    const proto::ProtoObject* pySetTrace = sys->getAttribute(context, proto::ProtoString::fromUTF8String(context, "settrace"));
    const proto::ProtoObject* pyGetTrace = sys->getAttribute(context, proto::ProtoString::fromUTF8String(context, "gettrace"));
    
    ASSERT_NE(pySetTrace, nullptr);
    ASSERT_NE(pyGetTrace, nullptr);
    
    // Set a mock trace function
    const proto::ProtoObject* mockFunc = context->newObject(false);
    const proto::ProtoList* args = context->newList()->appendLast(context, mockFunc);
    pySetTrace->asMethod(context)(context, sys, nullptr, args, nullptr);
    
    // Verify it's set
    const proto::ProtoObject* currentTrace = pyGetTrace->asMethod(context)(context, sys, nullptr, nullptr, nullptr);
    EXPECT_EQ(currentTrace, mockFunc);
}

TEST_F(FoundationTest, ListGetItemSetItem) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* my_list = context->newObject(true);
    my_list = my_list->addParent(context, env.getListPrototype());
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoList* list = context->newList()
        ->appendLast(context, context->fromInteger(10))
        ->appendLast(context, context->fromInteger(20))
        ->appendLast(context, context->fromInteger(30));
    my_list->setAttribute(context, dataName, list->asObject(context));

    const proto::ProtoString* getitemName = proto::ProtoString::fromUTF8String(context, "__getitem__");
    const proto::ProtoString* setitemName = proto::ProtoString::fromUTF8String(context, "__setitem__");

    const proto::ProtoList* getArgs = context->newList()->appendLast(context, context->fromInteger(1));
    const proto::ProtoObject* val = my_list->call(context, nullptr, getitemName, my_list, getArgs);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(val->asLong(context), 20);

    const proto::ProtoList* setArgs = context->newList()
        ->appendLast(context, context->fromInteger(1))
        ->appendLast(context, context->fromInteger(99));
    my_list->call(context, nullptr, setitemName, my_list, setArgs);
    const proto::ProtoObject* updated = my_list->getAttribute(context, dataName);
    EXPECT_EQ(updated->asList(context)->getAt(context, 1)->asLong(context), 99);

    const proto::ProtoList* sliceSpec = context->newList()
        ->appendLast(context, context->fromInteger(1))
        ->appendLast(context, context->fromInteger(3));
    const proto::ProtoList* sliceArgs = context->newList()->appendLast(context, sliceSpec->asObject(context));
    const proto::ProtoObject* sliceObj = my_list->call(context, nullptr, getitemName, my_list, sliceArgs);
    ASSERT_NE(sliceObj, nullptr);
    const proto::ProtoList* sliceList = sliceObj->asList(context);
    ASSERT_NE(sliceList, nullptr);
    EXPECT_EQ(sliceList->getSize(context), 2);
    EXPECT_EQ(sliceList->getAt(context, 0)->asLong(context), 99);
    EXPECT_EQ(sliceList->getAt(context, 1)->asLong(context), 30);
}

TEST_F(FoundationTest, DictSetItemLen) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* my_dict = context->newObject(true);
    my_dict = my_dict->addParent(context, env.getDictPrototype());
    my_dict->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->newSparseList()->asObject(context));

    const proto::ProtoString* setitemName = proto::ProtoString::fromUTF8String(context, "__setitem__");
    const proto::ProtoString* lenName = proto::ProtoString::fromUTF8String(context, "__len__");
    const proto::ProtoString* getitemName = proto::ProtoString::fromUTF8String(context, "__getitem__");

    const proto::ProtoObject* key = context->fromUTF8String("a");
    const proto::ProtoObject* value = context->fromInteger(42);
    const proto::ProtoList* setArgs = context->newList()->appendLast(context, key)->appendLast(context, value);
    my_dict->call(context, nullptr, setitemName, my_dict, setArgs);

    const proto::ProtoObject* lenResult = my_dict->call(context, nullptr, lenName, my_dict, nullptr);
    ASSERT_NE(lenResult, nullptr);
    EXPECT_EQ(lenResult->asLong(context), 1);

    const proto::ProtoList* getArgs = context->newList()->appendLast(context, key);
    const proto::ProtoObject* got = my_dict->call(context, nullptr, getitemName, my_dict, getArgs);
    ASSERT_NE(got, nullptr);
    EXPECT_EQ(got->asLong(context), 42);

    const proto::ProtoObject* missingKey = context->fromUTF8String("missing");
    const proto::ProtoList* missArgs = context->newList()->appendLast(context, missingKey);
    const proto::ProtoObject* missingVal = my_dict->call(context, nullptr, getitemName, my_dict, missArgs);
    EXPECT_EQ(missingVal, PROTO_NONE);
}

TEST_F(FoundationTest, ListIterNext) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* my_list = context->newObject(true);
    my_list = my_list->addParent(context, env.getListPrototype());
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoList* list = context->newList()
        ->appendLast(context, context->fromInteger(1))
        ->appendLast(context, context->fromInteger(2));
    my_list->setAttribute(context, dataName, list->asObject(context));

    const proto::ProtoObject* pyIter = env.resolve("iter");
    const proto::ProtoObject* pyNext = env.resolve("next");
    ASSERT_NE(pyIter, nullptr);
    ASSERT_NE(pyNext, nullptr);

    const proto::ProtoList* iterArgs = context->newList()->appendLast(context, my_list);
    const proto::ProtoObject* it = pyIter->asMethod(context)(context, PROTO_NONE, nullptr, iterArgs, nullptr);
    ASSERT_NE(it, nullptr);

    const proto::ProtoList* nextArgs = context->newList()->appendLast(context, it);
    const proto::ProtoObject* v1 = pyNext->asMethod(context)(context, PROTO_NONE, nullptr, nextArgs, nullptr);
    const proto::ProtoObject* v2 = pyNext->asMethod(context)(context, PROTO_NONE, nullptr, nextArgs, nullptr);
    ASSERT_NE(v1, nullptr);
    ASSERT_NE(v2, nullptr);
    EXPECT_EQ(v1->asLong(context), 1);
    EXPECT_EQ(v2->asLong(context), 2);
}

TEST_F(FoundationTest, DictIterKeys) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* my_dict = context->newObject(true);
    my_dict = my_dict->addParent(context, env.getDictPrototype());
    my_dict->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->newSparseList()->asObject(context));

    const proto::ProtoObject* pyIter = env.resolve("iter");
    const proto::ProtoObject* pyNext = env.resolve("next");
    const proto::ProtoObject* setitem = my_dict->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__setitem__"));
    ASSERT_NE(pyIter, nullptr);
    ASSERT_NE(pyNext, nullptr);
    ASSERT_NE(setitem, nullptr);

    const proto::ProtoObject* k1 = context->fromUTF8String("a");
    const proto::ProtoObject* k2 = context->fromUTF8String("b");
    const proto::ProtoObject* v1 = context->fromInteger(1);
    const proto::ProtoObject* v2 = context->fromInteger(2);
    const proto::ProtoList* setArgs1 = context->newList()->appendLast(context, k1)->appendLast(context, v1);
    const proto::ProtoList* setArgs2 = context->newList()->appendLast(context, k2)->appendLast(context, v2);
    setitem->asMethod(context)(context, my_dict, nullptr, setArgs1, nullptr);
    setitem->asMethod(context)(context, my_dict, nullptr, setArgs2, nullptr);

    const proto::ProtoList* iterArgs = context->newList()->appendLast(context, my_dict);
    const proto::ProtoObject* it = pyIter->asMethod(context)(context, PROTO_NONE, nullptr, iterArgs, nullptr);
    ASSERT_NE(it, nullptr);

    const proto::ProtoList* nextArgs = context->newList()->appendLast(context, it);
    const proto::ProtoObject* r1 = pyNext->asMethod(context)(context, PROTO_NONE, nullptr, nextArgs, nullptr);
    const proto::ProtoObject* r2 = pyNext->asMethod(context)(context, PROTO_NONE, nullptr, nextArgs, nullptr);
    ASSERT_NE(r1, nullptr);
    ASSERT_NE(r2, nullptr);

    std::string s1;
    std::string s2;
    r1->asString(context)->toUTF8String(context, s1);
    r2->asString(context)->toUTF8String(context, s2);
    EXPECT_EQ(s1, "a");
    EXPECT_EQ(s2, "b");
}

TEST_F(FoundationTest, ContainsDunder) {
    proto::ProtoContext* context = env.getContext();

    const proto::ProtoObject* my_list = context->newObject(true);
    my_list = my_list->addParent(context, env.getListPrototype());
    const proto::ProtoString* listData = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoList* list = context->newList()
        ->appendLast(context, context->fromInteger(5))
        ->appendLast(context, context->fromInteger(6));
    my_list->setAttribute(context, listData, list->asObject(context));

    const proto::ProtoObject* listContains = my_list->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__contains__"));
    ASSERT_NE(listContains, nullptr);
    const proto::ProtoList* listArgs = context->newList()->appendLast(context, context->fromInteger(6));
    const proto::ProtoObject* listResult = listContains->asMethod(context)(context, my_list, nullptr, listArgs, nullptr);
    EXPECT_EQ(listResult, PROTO_TRUE);

    const proto::ProtoObject* my_dict = context->newObject(true);
    my_dict = my_dict->addParent(context, env.getDictPrototype());
    my_dict->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->newSparseList()->asObject(context));
    const proto::ProtoObject* setitem = my_dict->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__setitem__"));
    ASSERT_NE(setitem, nullptr);
    const proto::ProtoObject* key = context->fromUTF8String("k");
    const proto::ProtoList* setArgs = context->newList()->appendLast(context, key)->appendLast(context, context->fromInteger(7));
    setitem->asMethod(context)(context, my_dict, nullptr, setArgs, nullptr);

    const proto::ProtoObject* dictContains = my_dict->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__contains__"));
    ASSERT_NE(dictContains, nullptr);
    const proto::ProtoList* dictArgs = context->newList()->appendLast(context, key);
    const proto::ProtoObject* dictResult = dictContains->asMethod(context)(context, my_dict, nullptr, dictArgs, nullptr);
    EXPECT_EQ(dictResult, PROTO_TRUE);
}

TEST_F(FoundationTest, EqualsDunder) {
    proto::ProtoContext* context = env.getContext();

    const proto::ProtoObject* listObj = context->newObject(true)->addParent(context, env.getListPrototype());
    const proto::ProtoObject* listObj2 = context->newObject(true)->addParent(context, env.getListPrototype());
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoList* list = context->newList()
        ->appendLast(context, context->fromInteger(1))
        ->appendLast(context, context->fromInteger(2));
    listObj->setAttribute(context, dataName, list->asObject(context));
    listObj2->setAttribute(context, dataName, list->asObject(context));

    const proto::ProtoObject* listEq = listObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__eq__"));
    ASSERT_NE(listEq, nullptr);
    const proto::ProtoList* listArgs = context->newList()->appendLast(context, listObj2);
    const proto::ProtoObject* listEqResult = listEq->asMethod(context)(context, listObj, nullptr, listArgs, nullptr);
    EXPECT_EQ(listEqResult, PROTO_TRUE);

    const proto::ProtoObject* dict1 = context->newObject(true)->addParent(context, env.getDictPrototype());
    const proto::ProtoObject* dict2 = context->newObject(true)->addParent(context, env.getDictPrototype());
    dict1->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->newSparseList()->asObject(context));
    dict2->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->newSparseList()->asObject(context));
    const proto::ProtoObject* setitem = dict1->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__setitem__"));
    ASSERT_NE(setitem, nullptr);
    const proto::ProtoObject* key = context->fromUTF8String("k");
    const proto::ProtoObject* val = context->fromInteger(7);
    const proto::ProtoList* setArgs = context->newList()->appendLast(context, key)->appendLast(context, val);
    setitem->asMethod(context)(context, dict1, nullptr, setArgs, nullptr);
    setitem->asMethod(context)(context, dict2, nullptr, setArgs, nullptr);

    const proto::ProtoObject* dictEq = dict1->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__eq__"));
    ASSERT_NE(dictEq, nullptr);
    const proto::ProtoList* dictArgs = context->newList()->appendLast(context, dict2);
    const proto::ProtoObject* dictEqResult = dictEq->asMethod(context)(context, dict1, nullptr, dictArgs, nullptr);
    EXPECT_EQ(dictEqResult, PROTO_TRUE);
}

TEST_F(FoundationTest, OrderingDunder) {
    proto::ProtoContext* context = env.getContext();

    const proto::ProtoObject* list1 = context->newObject(true)->addParent(context, env.getListPrototype());
    const proto::ProtoObject* list2 = context->newObject(true)->addParent(context, env.getListPrototype());
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoList* a = context->newList()->appendLast(context, context->fromInteger(1))->appendLast(context, context->fromInteger(2));
    const proto::ProtoList* b = context->newList()->appendLast(context, context->fromInteger(1))->appendLast(context, context->fromInteger(3));
    list1->setAttribute(context, dataName, a->asObject(context));
    list2->setAttribute(context, dataName, b->asObject(context));

    const proto::ProtoObject* lt = list1->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__lt__"));
    const proto::ProtoObject* ge = list1->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__ge__"));
    ASSERT_NE(lt, nullptr);
    ASSERT_NE(ge, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, list2);
    const proto::ProtoObject* ltRes = lt->asMethod(context)(context, list1, nullptr, args, nullptr);
    const proto::ProtoObject* geRes = ge->asMethod(context)(context, list1, nullptr, args, nullptr);
    EXPECT_EQ(ltRes, PROTO_TRUE);
    EXPECT_EQ(geRes, PROTO_FALSE);

}

TEST_F(FoundationTest, ReprStrDunder) {
    proto::ProtoContext* context = env.getContext();

    const proto::ProtoObject* listObj = context->newObject(true)->addParent(context, env.getListPrototype());
    listObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"),
                          context->newList()->appendLast(context, context->fromInteger(1))->appendLast(context, context->fromInteger(2))->asObject(context));
    const proto::ProtoObject* listRepr = listObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__repr__"));
    ASSERT_NE(listRepr, nullptr);
    const proto::ProtoObject* listStrObj = listRepr->asMethod(context)(context, listObj, nullptr, nullptr, nullptr);
    ASSERT_NE(listStrObj, nullptr);
    ASSERT_TRUE(listStrObj->isString(context));
    std::string listStr;
    listStrObj->asString(context)->toUTF8String(context, listStr);
    EXPECT_EQ(listStr, "[1, 2]");

    const proto::ProtoObject* dictObj = context->newObject(true)->addParent(context, env.getDictPrototype());
    dictObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->newSparseList()->asObject(context));
    const proto::ProtoObject* setitem = dictObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__setitem__"));
    ASSERT_NE(setitem, nullptr);
    const proto::ProtoObject* key = context->fromUTF8String("k");
    const proto::ProtoObject* val = context->fromInteger(9);
    const proto::ProtoList* setArgs = context->newList()->appendLast(context, key)->appendLast(context, val);
    setitem->asMethod(context)(context, dictObj, nullptr, setArgs, nullptr);

    const proto::ProtoObject* dictRepr = dictObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__repr__"));
    ASSERT_NE(dictRepr, nullptr);
    const proto::ProtoObject* dictStrObj = dictRepr->asMethod(context)(context, dictObj, nullptr, nullptr, nullptr);
    ASSERT_NE(dictStrObj, nullptr);
    ASSERT_TRUE(dictStrObj->isString(context));
    std::string dictStr;
    dictStrObj->asString(context)->toUTF8String(context, dictStr);
    EXPECT_EQ(dictStr, "{k: 9}");
}

TEST_F(FoundationTest, BoolDunder) {
    proto::ProtoContext* context = env.getContext();

    const proto::ProtoObject* listObj = context->newObject(true)->addParent(context, env.getListPrototype());
    listObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->newList()->asObject(context));
    const proto::ProtoObject* listBool = listObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__bool__"));
    ASSERT_NE(listBool, nullptr);
    const proto::ProtoObject* listFalse = listBool->asMethod(context)(context, listObj, nullptr, nullptr, nullptr);
    EXPECT_EQ(listFalse, PROTO_FALSE);

    listObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"),
                          context->newList()->appendLast(context, context->fromInteger(1))->asObject(context));
    const proto::ProtoObject* listTrue = listBool->asMethod(context)(context, listObj, nullptr, nullptr, nullptr);
    EXPECT_EQ(listTrue, PROTO_TRUE);

    const proto::ProtoObject* dictObj = context->newObject(true)->addParent(context, env.getDictPrototype());
    dictObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->newSparseList()->asObject(context));
    const proto::ProtoObject* dictBool = dictObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__bool__"));
    ASSERT_NE(dictBool, nullptr);
    const proto::ProtoObject* dictFalse = dictBool->asMethod(context)(context, dictObj, nullptr, nullptr, nullptr);
    EXPECT_EQ(dictFalse, PROTO_FALSE);

    const proto::ProtoObject* setitem = dictObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__setitem__"));
    const proto::ProtoObject* key = context->fromUTF8String("k");
    const proto::ProtoList* setArgs = context->newList()->appendLast(context, key)->appendLast(context, context->fromInteger(1));
    setitem->asMethod(context)(context, dictObj, nullptr, setArgs, nullptr);
    const proto::ProtoObject* dictTrue = dictBool->asMethod(context)(context, dictObj, nullptr, nullptr, nullptr);
    EXPECT_EQ(dictTrue, PROTO_TRUE);
}

TEST_F(FoundationTest, DictViews) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* dictObj = context->newObject(true)->addParent(context, env.getDictPrototype());
    dictObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->newSparseList()->asObject(context));
    const proto::ProtoObject* setitem = dictObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__setitem__"));
    ASSERT_NE(setitem, nullptr);
    const proto::ProtoObject* key = context->fromUTF8String("k");
    const proto::ProtoObject* val = context->fromInteger(5);
    const proto::ProtoList* setArgs = context->newList()->appendLast(context, key)->appendLast(context, val);
    setitem->asMethod(context)(context, dictObj, nullptr, setArgs, nullptr);

    const proto::ProtoObject* keysMethod = dictObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "keys"));
    const proto::ProtoObject* valuesMethod = dictObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "values"));
    const proto::ProtoObject* itemsMethod = dictObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "items"));
    ASSERT_NE(keysMethod, nullptr);
    ASSERT_NE(valuesMethod, nullptr);
    ASSERT_NE(itemsMethod, nullptr);

    const proto::ProtoObject* keysObj = keysMethod->asMethod(context)(context, dictObj, nullptr, nullptr, nullptr);
    const proto::ProtoObject* valuesObj = valuesMethod->asMethod(context)(context, dictObj, nullptr, nullptr, nullptr);
    const proto::ProtoObject* itemsObj = itemsMethod->asMethod(context)(context, dictObj, nullptr, nullptr, nullptr);
    ASSERT_TRUE(keysObj->asList(context));
    ASSERT_TRUE(valuesObj->asList(context));
    ASSERT_TRUE(itemsObj->asList(context));
    EXPECT_EQ(keysObj->asList(context)->getSize(context), 1);
    EXPECT_EQ(valuesObj->asList(context)->getSize(context), 1);
    EXPECT_EQ(itemsObj->asList(context)->getSize(context), 1);
}

TEST_F(FoundationTest, DictGetSetDefault) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* dictObj = context->newObject(true)->addParent(context, env.getDictPrototype());
    dictObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->newSparseList()->asObject(context));
    const proto::ProtoObject* getMethod = dictObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "get"));
    const proto::ProtoObject* setdefaultMethod = dictObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "setdefault"));
    ASSERT_NE(getMethod, nullptr);
    ASSERT_NE(setdefaultMethod, nullptr);

    const proto::ProtoObject* key = context->fromUTF8String("k");
    const proto::ProtoObject* defaultVal = context->fromInteger(7);
    const proto::ProtoList* getArgs = context->newList()->appendLast(context, key)->appendLast(context, defaultVal);
    const proto::ProtoObject* getVal = getMethod->asMethod(context)(context, dictObj, nullptr, getArgs, nullptr);
    EXPECT_EQ(getVal, defaultVal);

    const proto::ProtoList* setArgs = context->newList()->appendLast(context, key)->appendLast(context, defaultVal);
    const proto::ProtoObject* setVal = setdefaultMethod->asMethod(context)(context, dictObj, nullptr, setArgs, nullptr);
    EXPECT_EQ(setVal, defaultVal);

    const proto::ProtoObject* getVal2 = getMethod->asMethod(context)(context, dictObj, nullptr, getArgs, nullptr);
    EXPECT_EQ(getVal2, defaultVal);
}

TEST_F(FoundationTest, ListPopExtend) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* listObj = context->newObject(true)->addParent(context, env.getListPrototype());
    listObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"),
                          context->newList()->appendLast(context, context->fromInteger(1))->appendLast(context, context->fromInteger(2))->asObject(context));

    const proto::ProtoObject* pop = listObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "pop"));
    ASSERT_NE(pop, nullptr);
    const proto::ProtoObject* popped = pop->asMethod(context)(context, listObj, nullptr, nullptr, nullptr);
    ASSERT_NE(popped, nullptr);
    EXPECT_EQ(popped->asLong(context), 2);

    const proto::ProtoObject* extend = listObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "extend"));
    ASSERT_NE(extend, nullptr);
    const proto::ProtoList* extList = context->newList()->appendLast(context, context->fromInteger(3))->appendLast(context, context->fromInteger(4));
    const proto::ProtoList* extArgs = context->newList()->appendLast(context, extList->asObject(context));
    extend->asMethod(context)(context, listObj, nullptr, extArgs, nullptr);

    const proto::ProtoObject* data = listObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"));
    const proto::ProtoList* list = data->asList(context);
    ASSERT_NE(list, nullptr);
    EXPECT_EQ(list->getSize(context), 3);
    EXPECT_EQ(list->getAt(context, 0)->asLong(context), 1);
    EXPECT_EQ(list->getAt(context, 1)->asLong(context), 3);
    EXPECT_EQ(list->getAt(context, 2)->asLong(context), 4);
}

TEST_F(FoundationTest, LenStringTuple) {
    proto::ProtoContext* context = env.getContext();

    const proto::ProtoObject* pyLen = env.resolve("len");
    ASSERT_NE(pyLen, nullptr);
    const proto::ProtoObject* strObj = context->fromUTF8String("abcd");
    const proto::ProtoList* lenArgs = context->newList()->appendLast(context, strObj);
    const proto::ProtoObject* strLen = pyLen->asMethod(context)(context, PROTO_NONE, nullptr, lenArgs, nullptr);
    ASSERT_NE(strLen, nullptr);
    EXPECT_EQ(strLen->asLong(context), 4);

    const proto::ProtoObject* tupleObj = context->newObject(true)->addParent(context, env.getTuplePrototype());
    const proto::ProtoList* tupleList = context->newList()->appendLast(context, context->fromInteger(1))->appendLast(context, context->fromInteger(2));
    const proto::ProtoTuple* tuple = context->newTupleFromList(tupleList);
    tupleObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), tuple->asObject(context));
    const proto::ProtoList* tupleArgs = context->newList()->appendLast(context, tupleObj);
    const proto::ProtoObject* tupleLen = pyLen->asMethod(context)(context, PROTO_NONE, nullptr, tupleArgs, nullptr);
    ASSERT_NE(tupleLen, nullptr);
    EXPECT_EQ(tupleLen->asLong(context), 2);
}

TEST_F(FoundationTest, DequeBasic) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* collections = env.resolve("_collections");
    ASSERT_NE(collections, nullptr);
    
    const proto::ProtoObject* pyDeque = collections->getAttribute(context, proto::ProtoString::fromUTF8String(context, "deque"));
    ASSERT_NE(pyDeque, nullptr);
    
    // Create deque: d = deque()
    const proto::ProtoObject* d = pyDeque->asMethod(context)(context, collections, nullptr, nullptr, nullptr);
    ASSERT_NE(d, nullptr);
    
    const proto::ProtoObject* append = d->getAttribute(context, proto::ProtoString::fromUTF8String(context, "append"));
    const proto::ProtoObject* appendleft = d->getAttribute(context, proto::ProtoString::fromUTF8String(context, "appendleft"));
    const proto::ProtoObject* pop = d->getAttribute(context, proto::ProtoString::fromUTF8String(context, "pop"));
    const proto::ProtoObject* popleft = d->getAttribute(context, proto::ProtoString::fromUTF8String(context, "popleft"));
    const proto::ProtoObject* lenFunc = d->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__len__"));
    
    // Test append
    const proto::ProtoObject* v1 = context->fromInteger(10);
    const proto::ProtoList* args1 = context->newList()->appendLast(context, v1);
    append->asMethod(context)(context, d, nullptr, args1, nullptr);
    
    // Test appendleft
    const proto::ProtoObject* v2 = context->fromInteger(20);
    const proto::ProtoList* args2 = context->newList()->appendLast(context, v2);
    appendleft->asMethod(context)(context, d, nullptr, args2, nullptr);
    
    // Check length
    const proto::ProtoObject* length = lenFunc->asMethod(context)(context, d, nullptr, nullptr, nullptr);
    EXPECT_EQ(length->asLong(context), 2);
    
    // Test popleft (should be 20)
    const proto::ProtoObject* p1 = popleft->asMethod(context)(context, d, nullptr, nullptr, nullptr);
    EXPECT_EQ(p1->asLong(context), 20);
    
    // Test pop (should be 10)
    const proto::ProtoObject* p2 = pop->asMethod(context)(context, d, nullptr, nullptr, nullptr);
    EXPECT_EQ(p2->asLong(context), 10);
    
    // Check length after pops
    length = lenFunc->asMethod(context)(context, d, nullptr, nullptr, nullptr);
    EXPECT_EQ(length->asLong(context), 0);
}
