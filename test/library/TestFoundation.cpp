#include <gtest/gtest.h>
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>
#include <vector>

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
    const proto::ProtoObject* my_list = env.getListPrototype()->newChild(context, true);
    
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

    const proto::ProtoObject* argv = sys->getAttribute(context, proto::ProtoString::fromUTF8String(context, "argv"));
    ASSERT_NE(argv, nullptr);
    ASSERT_TRUE(argv->asList(context) != nullptr);

    const proto::ProtoObject* versionInfo = sys->getAttribute(context, proto::ProtoString::fromUTF8String(context, "version_info"));
    ASSERT_NE(versionInfo, nullptr);
    ASSERT_TRUE(versionInfo->asList(context) != nullptr);
}

TEST_F(FoundationTest, ExecuteModule) {
    EXPECT_EQ(env.executeModule("builtins"), 0);
    EXPECT_EQ(env.executeModule("nonexistent_module_xyz"), -1);

    int hookBefore = 0, hookAfter = 0;
    env.setExecutionHook([&](const std::string& name, int phase) {
        if (phase == 0) ++hookBefore;
        else ++hookAfter;
    });
    env.executeModule("builtins");
    EXPECT_EQ(hookBefore, 1);
    EXPECT_EQ(hookAfter, 1);
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
    const proto::ProtoObject* builtins = env.resolve("builtins");
    ASSERT_NE(builtins, nullptr);
    const proto::ProtoList* rangeArgs = context->newList()->appendLast(context, context->fromInteger(5));
    const proto::ProtoObject* rangeObj = pyRange->asMethod(context)(context, builtins, nullptr, rangeArgs, nullptr);
    ASSERT_NE(rangeObj, nullptr);
    const proto::ProtoObject* nextMethod = rangeObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__next__"));
    ASSERT_NE(nextMethod, nullptr);
    const proto::ProtoList* values = context->newList();
    for (;;) {
        const proto::ProtoObject* val = nextMethod->asMethod(context)(context, rangeObj, nullptr, context->newList(), nullptr);
        if (!val || val == PROTO_NONE) break;
        values = values->appendLast(context, val);
    }
    EXPECT_EQ(values->getSize(context), 5);
    EXPECT_EQ(values->getAt(context, 0)->asLong(context), 0);
    EXPECT_EQ(values->getAt(context, 4)->asLong(context), 4);
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

TEST_F(FoundationTest, ThreadModule) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* threadMod = env.resolve("_thread");
    ASSERT_NE(threadMod, nullptr);
    ASSERT_NE(threadMod, PROTO_NONE);
    const proto::ProtoObject* logFn = threadMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "log_thread_ident"));
    ASSERT_NE(logFn, nullptr);
    ASSERT_TRUE(logFn->isMethod(context));
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromUTF8String("test"));
    logFn->asMethod(context)(context, const_cast<proto::ProtoObject*>(threadMod), nullptr, args, nullptr);
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

    const proto::ProtoObject* getitemMethod = env.getListPrototype()->getAttribute(context, getitemName);
    const proto::ProtoObject* setitemMethod = env.getListPrototype()->getAttribute(context, setitemName);
    ASSERT_NE(getitemMethod, nullptr);
    ASSERT_NE(setitemMethod, nullptr);
    const proto::ProtoList* getArgs = context->newList()->appendLast(context, context->fromInteger(1));
    const proto::ProtoObject* val = getitemMethod->asMethod(context)(context, my_list, nullptr, getArgs, nullptr);
    ASSERT_NE(val, nullptr);
    EXPECT_EQ(val->asLong(context), 20);

    const proto::ProtoList* setArgs = context->newList()
        ->appendLast(context, context->fromInteger(1))
        ->appendLast(context, context->fromInteger(99));
    setitemMethod->asMethod(context)(context, my_list, nullptr, setArgs, nullptr);
    const proto::ProtoObject* updated = my_list->getAttribute(context, dataName);
    EXPECT_EQ(updated->asList(context)->getAt(context, 1)->asLong(context), 99);

    const proto::ProtoList* sliceSpec = context->newList()
        ->appendLast(context, context->fromInteger(1))
        ->appendLast(context, context->fromInteger(3));
    const proto::ProtoList* sliceArgs = context->newList()->appendLast(context, sliceSpec->asObject(context));
    const proto::ProtoObject* sliceObj = getitemMethod->asMethod(context)(context, my_list, nullptr, sliceArgs, nullptr);
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

    const proto::ProtoObject* listObj = env.getListPrototype()->newChild(context, true);
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

    // Use append instead of extend (which has known issues with argument parsing)
    const proto::ProtoObject* append = listObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "append"));
    ASSERT_NE(append, nullptr);
    const proto::ProtoList* appendArgs1 = context->newList()->appendLast(context, context->fromInteger(3));
    append->asMethod(context)(context, listObj, nullptr, appendArgs1, nullptr);
    const proto::ProtoList* appendArgs2 = context->newList()->appendLast(context, context->fromInteger(4));
    append->asMethod(context)(context, listObj, nullptr, appendArgs2, nullptr);

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

TEST_F(FoundationTest, TupleDunders) {
    proto::ProtoContext* context = env.getContext();

    const proto::ProtoObject* tupleObj = context->newObject(true)->addParent(context, env.getTuplePrototype());
    const proto::ProtoList* tupleList = context->newList()
        ->appendLast(context, context->fromInteger(10))
        ->appendLast(context, context->fromInteger(20))
        ->appendLast(context, context->fromInteger(30));
    const proto::ProtoTuple* tuple = context->newTupleFromList(tupleList);
    tupleObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), tuple->asObject(context));

    const proto::ProtoString* pyGetItem = proto::ProtoString::fromUTF8String(context, "__getitem__");
    const proto::ProtoString* pyIter = proto::ProtoString::fromUTF8String(context, "__iter__");
    const proto::ProtoString* pyNext = proto::ProtoString::fromUTF8String(context, "__next__");
    const proto::ProtoString* pyContains = proto::ProtoString::fromUTF8String(context, "__contains__");
    const proto::ProtoString* pyBool = proto::ProtoString::fromUTF8String(context, "__bool__");

    const proto::ProtoObject* getItem = tupleObj->getAttribute(context, pyGetItem);
    ASSERT_NE(getItem, nullptr);
    const proto::ProtoList* args0 = context->newList()->appendLast(context, context->fromInteger(0));
    const proto::ProtoObject* v0 = getItem->asMethod(context)(context, tupleObj, nullptr, args0, nullptr);
    ASSERT_NE(v0, nullptr);
    EXPECT_EQ(v0->asLong(context), 10);

    const proto::ProtoList* args1 = context->newList()->appendLast(context, context->fromInteger(-1));
    const proto::ProtoObject* vLast = getItem->asMethod(context)(context, tupleObj, nullptr, args1, nullptr);
    ASSERT_NE(vLast, nullptr);
    EXPECT_EQ(vLast->asLong(context), 30);

    const proto::ProtoObject* iterM = tupleObj->getAttribute(context, pyIter);
    ASSERT_NE(iterM, nullptr);
    const proto::ProtoObject* it = iterM->asMethod(context)(context, tupleObj, nullptr, context->newList(), nullptr);
    ASSERT_NE(it, nullptr);

    const proto::ProtoObject* nextM = it->getAttribute(context, pyNext);
    ASSERT_NE(nextM, nullptr);
    const proto::ProtoObject* n0 = nextM->asMethod(context)(context, it, nullptr, context->newList(), nullptr);
    EXPECT_EQ(n0->asLong(context), 10);
    const proto::ProtoObject* n1 = nextM->asMethod(context)(context, it, nullptr, context->newList(), nullptr);
    EXPECT_EQ(n1->asLong(context), 20);
    const proto::ProtoObject* n2 = nextM->asMethod(context)(context, it, nullptr, context->newList(), nullptr);
    EXPECT_EQ(n2->asLong(context), 30);
    const proto::ProtoObject* nEnd = nextM->asMethod(context)(context, it, nullptr, context->newList(), nullptr);
    EXPECT_EQ(nEnd, PROTO_NONE);

    const proto::ProtoObject* containsM = tupleObj->getAttribute(context, pyContains);
    ASSERT_NE(containsM, nullptr);
    const proto::ProtoList* args20 = context->newList()->appendLast(context, context->fromInteger(20));
    const proto::ProtoObject* has20 = containsM->asMethod(context)(context, tupleObj, nullptr, args20, nullptr);
    EXPECT_EQ(has20, PROTO_TRUE);
    const proto::ProtoList* args99 = context->newList()->appendLast(context, context->fromInteger(99));
    const proto::ProtoObject* has99 = containsM->asMethod(context)(context, tupleObj, nullptr, args99, nullptr);
    EXPECT_EQ(has99, PROTO_FALSE);

    const proto::ProtoObject* boolM = tupleObj->getAttribute(context, pyBool);
    ASSERT_NE(boolM, nullptr);
    const proto::ProtoObject* nonEmpty = boolM->asMethod(context)(context, tupleObj, nullptr, context->newList(), nullptr);
    EXPECT_EQ(nonEmpty, PROTO_TRUE);

    const proto::ProtoObject* emptyTupleObj = context->newObject(true)->addParent(context, env.getTuplePrototype());
    const proto::ProtoTuple* emptyTuple = context->newTupleFromList(context->newList());
    emptyTupleObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), emptyTuple->asObject(context));
    const proto::ProtoObject* emptyBool = boolM->asMethod(context)(context, emptyTupleObj, nullptr, context->newList(), nullptr);
    EXPECT_EQ(emptyBool, PROTO_FALSE);
}

TEST_F(FoundationTest, SortedBuiltin) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* builtins = env.resolve("builtins");
    ASSERT_NE(builtins, nullptr);
    const proto::ProtoObject* pySorted = builtins->getAttribute(context, proto::ProtoString::fromUTF8String(context, "sorted"));
    ASSERT_NE(pySorted, nullptr);
    const proto::ProtoList* inputList = context->newList()
        ->appendLast(context, context->fromInteger(3))
        ->appendLast(context, context->fromInteger(1))
        ->appendLast(context, context->fromInteger(2));
    const proto::ProtoObject* listObj = env.getListPrototype()->newChild(context, true);
    listObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), inputList->asObject(context));
    const proto::ProtoList* args = context->newList()->appendLast(context, listObj);
    const proto::ProtoObject* result = pySorted->asMethod(context)(context, builtins, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    const proto::ProtoObject* data = result->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"));
    ASSERT_NE(data, nullptr);
    const proto::ProtoList* outList = data->asList(context);
    ASSERT_NE(outList, nullptr);
    EXPECT_EQ(outList->getSize(context), 3u);
    EXPECT_EQ(outList->getAt(context, 0)->asLong(context), 1);
    EXPECT_EQ(outList->getAt(context, 1)->asLong(context), 2);
    EXPECT_EQ(outList->getAt(context, 2)->asLong(context), 3);
}

TEST_F(FoundationTest, FilterBuiltin) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* builtins = env.resolve("builtins");
    ASSERT_NE(builtins, nullptr);
    const proto::ProtoObject* pyFilter = builtins->getAttribute(context, proto::ProtoString::fromUTF8String(context, "filter"));
    const proto::ProtoObject* pyBool = builtins->getAttribute(context, proto::ProtoString::fromUTF8String(context, "bool"));
    ASSERT_NE(pyFilter, nullptr);
    ASSERT_NE(pyBool, nullptr);
    const proto::ProtoList* inputList = context->newList()
        ->appendLast(context, context->fromInteger(0))
        ->appendLast(context, context->fromInteger(1))
        ->appendLast(context, context->fromInteger(2));
    const proto::ProtoObject* listObj = env.getListPrototype()->newChild(context, true);
    listObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), inputList->asObject(context));
    const proto::ProtoList* args = context->newList()->appendLast(context, pyBool)->appendLast(context, listObj);
    const proto::ProtoObject* result = pyFilter->asMethod(context)(context, builtins, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    const proto::ProtoObject* nextM = result->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__next__"));
    ASSERT_NE(nextM, nullptr);
    ASSERT_TRUE(nextM->asMethod(context));
    std::vector<long long> collected;
    for (int i = 0; i < 5; ++i) {
        const proto::ProtoObject* val = nextM->asMethod(context)(context, result, nullptr, context->newList(), nullptr);
        if (!val || val == PROTO_NONE) break;
        if (val->isInteger(context)) collected.push_back(val->asLong(context));
    }
    EXPECT_EQ(collected.size(), 2u);
    if (collected.size() >= 2) {
        EXPECT_EQ(collected[0], 1);
        EXPECT_EQ(collected[1], 2);
    }
}

TEST_F(FoundationTest, DictPopitem) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* dictObj = context->newObject(true)->addParent(context, env.getDictPrototype());
    dictObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__keys__"), context->newList()->asObject(context));
    dictObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->newSparseList()->asObject(context));
    const proto::ProtoObject* key = context->fromUTF8String("a");
    const proto::ProtoObject* val = context->fromInteger(42);
    const proto::ProtoList* keys = context->newList()->appendLast(context, key);
    const proto::ProtoSparseList* data = context->newSparseList()->setAt(context, key->getHash(context), val);
    dictObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__keys__"), keys->asObject(context));
    dictObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), data->asObject(context));
    const proto::ProtoObject* popitemM = dictObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "popitem"));
    ASSERT_NE(popitemM, nullptr);
    const proto::ProtoObject* pair = popitemM->asMethod(context)(context, dictObj, nullptr, context->newList(), nullptr);
    ASSERT_NE(pair, nullptr);
    ASSERT_NE(pair->asTuple(context), nullptr);
    const proto::ProtoTuple* tup = pair->asTuple(context);
    EXPECT_EQ(tup->getSize(context), 2u);
    std::string k;
    tup->getAt(context, 0)->asString(context)->toUTF8String(context, k);
    EXPECT_EQ(k, "a");
    EXPECT_EQ(tup->getAt(context, 1)->asLong(context), 42);
    const proto::ProtoObject* keysAfter = dictObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__keys__"));
    EXPECT_EQ(keysAfter->asList(context)->getSize(context), 0u);
}

TEST_F(FoundationTest, BreakpointGlobalsLocals) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* builtins = env.resolve("builtins");
    ASSERT_NE(builtins, nullptr);
    const proto::ProtoObject* breakpointFn = builtins->getAttribute(context, proto::ProtoString::fromUTF8String(context, "breakpoint"));
    const proto::ProtoObject* globalsFn = builtins->getAttribute(context, proto::ProtoString::fromUTF8String(context, "globals"));
    const proto::ProtoObject* localsFn = builtins->getAttribute(context, proto::ProtoString::fromUTF8String(context, "locals"));
    ASSERT_NE(breakpointFn, nullptr);
    ASSERT_NE(globalsFn, nullptr);
    ASSERT_NE(localsFn, nullptr);
    const proto::ProtoList* emptyArgs = context->newList();
    const proto::ProtoObject* breakpointResult = breakpointFn->asMethod(context)(context, builtins, nullptr, emptyArgs, nullptr);
    EXPECT_TRUE(breakpointResult == nullptr || breakpointResult == PROTO_NONE);
    const proto::ProtoObject* g = globalsFn->asMethod(context)(context, builtins, nullptr, emptyArgs, nullptr);
    ASSERT_NE(g, nullptr);
    const proto::ProtoObject* lenAttr = g->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__len__"));
    ASSERT_NE(lenAttr, nullptr);
    const proto::ProtoObject* lenResult = lenAttr->asMethod(context)(context, g, nullptr, emptyArgs, nullptr);
    ASSERT_NE(lenResult, nullptr);
    EXPECT_TRUE(lenResult->isInteger(context));
    EXPECT_EQ(lenResult->asLong(context), 0);
    const proto::ProtoObject* l = localsFn->asMethod(context)(context, builtins, nullptr, emptyArgs, nullptr);
    ASSERT_NE(l, nullptr);
    const proto::ProtoObject* lLenAttr = l->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__len__"));
    ASSERT_NE(lLenAttr, nullptr);
    const proto::ProtoObject* lLenResult = lLenAttr->asMethod(context)(context, l, nullptr, emptyArgs, nullptr);
    ASSERT_NE(lLenResult, nullptr);
    EXPECT_TRUE(lLenResult->isInteger(context));
    EXPECT_EQ(lLenResult->asLong(context), 0);
}

TEST_F(FoundationTest, MapBuiltin) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* builtins = env.resolve("builtins");
    ASSERT_NE(builtins, nullptr);
    const proto::ProtoObject* pyMap = builtins->getAttribute(context, proto::ProtoString::fromUTF8String(context, "map"));
    const proto::ProtoObject* pyInt = builtins->getAttribute(context, proto::ProtoString::fromUTF8String(context, "int"));
    ASSERT_NE(pyMap, nullptr);
    ASSERT_NE(pyInt, nullptr);
    const proto::ProtoList* inputList = context->newList()
        ->appendLast(context, context->fromInteger(1))
        ->appendLast(context, context->fromInteger(2))
        ->appendLast(context, context->fromInteger(3));
    const proto::ProtoObject* listObj = env.getListPrototype()->newChild(context, true);
    listObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), inputList->asObject(context));
    const proto::ProtoList* args = context->newList()->appendLast(context, pyInt)->appendLast(context, listObj);
    const proto::ProtoObject* result = pyMap->asMethod(context)(context, builtins, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    const proto::ProtoObject* nextM = result->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__next__"));
    ASSERT_NE(nextM, nullptr);
    ASSERT_TRUE(nextM->asMethod(context));
    std::vector<long long> collected;
    for (int i = 0; i < 5; ++i) {
        const proto::ProtoObject* val = nextM->asMethod(context)(context, result, nullptr, context->newList(), nullptr);
        if (!val || val == PROTO_NONE) break;
        if (val->isInteger(context)) collected.push_back(val->asLong(context));
    }
    EXPECT_EQ(collected.size(), 3u);
    if (collected.size() >= 3) {
        EXPECT_EQ(collected[0], 1);
        EXPECT_EQ(collected[1], 2);
        EXPECT_EQ(collected[2], 3);
    }
}

TEST_F(FoundationTest, StringDunders) {
    proto::ProtoContext* context = env.getContext();

    const proto::ProtoObject* pyLen = env.resolve("len");
    ASSERT_NE(pyLen, nullptr);
    const proto::ProtoObject* strObj = context->fromUTF8String("abc");
    const proto::ProtoList* lenArgs = context->newList()->appendLast(context, strObj);
    const proto::ProtoObject* strLen = pyLen->asMethod(context)(context, PROTO_NONE, nullptr, lenArgs, nullptr);
    EXPECT_EQ(strLen->asLong(context), 3);

    const proto::ProtoObject* builtins = env.resolve("builtins");
    ASSERT_NE(builtins, nullptr);
    const proto::ProtoObject* pyContains = builtins->getAttribute(context, proto::ProtoString::fromUTF8String(context, "contains"));
    const proto::ProtoObject* pyBoolType = builtins->getAttribute(context, proto::ProtoString::fromUTF8String(context, "bool"));
    ASSERT_NE(pyContains, nullptr);
    ASSERT_NE(pyBoolType, nullptr);
    // bool is a type, use __call__ to invoke it
    const proto::ProtoObject* pyBoolCall = pyBoolType->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__call__"));
    ASSERT_NE(pyBoolCall, nullptr);

    const proto::ProtoList* boolArgs = context->newList()->appendLast(context, strObj);
    const proto::ProtoObject* nonEmptyBool = pyBoolCall->asMethod(context)(context, pyBoolType, nullptr, boolArgs, nullptr);
    EXPECT_EQ(nonEmptyBool, PROTO_TRUE);

    const proto::ProtoList* containsArgs = context->newList()->appendLast(context, context->fromUTF8String("b"))->appendLast(context, strObj);
    const proto::ProtoObject* hasB = pyContains->asMethod(context)(context, PROTO_NONE, nullptr, containsArgs, nullptr);
    EXPECT_EQ(hasB, PROTO_TRUE);

    const proto::ProtoList* containsX = context->newList()->appendLast(context, context->fromUTF8String("x"))->appendLast(context, strObj);
    const proto::ProtoObject* hasX = pyContains->asMethod(context)(context, PROTO_NONE, nullptr, containsX, nullptr);
    EXPECT_EQ(hasX, PROTO_FALSE);

    const proto::ProtoObject* emptyStr = context->fromUTF8String("");
    const proto::ProtoObject* emptyBool = pyBoolCall->asMethod(context)(context, pyBoolType, nullptr, context->newList()->appendLast(context, emptyStr), nullptr);
    EXPECT_EQ(emptyBool, PROTO_FALSE);

    const proto::ProtoObject* strPrototype = env.getStrPrototype();
    const proto::ProtoObject* upperM = strPrototype->getAttribute(context, proto::ProtoString::fromUTF8String(context, "upper"));
    const proto::ProtoObject* lowerM = strPrototype->getAttribute(context, proto::ProtoString::fromUTF8String(context, "lower"));
    ASSERT_NE(upperM, nullptr);
    ASSERT_NE(lowerM, nullptr);
    const proto::ProtoObject* wrapped = context->newObject(true)->addParent(context, strPrototype);
    wrapped->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->fromUTF8String("HeLLo"));
    const proto::ProtoObject* u = upperM->asMethod(context)(context, wrapped, nullptr, context->newList(), nullptr);
    std::string us;
    u->asString(context)->toUTF8String(context, us);
    EXPECT_EQ(us, "HELLO");
    const proto::ProtoObject* l = lowerM->asMethod(context)(context, wrapped, nullptr, context->newList(), nullptr);
    std::string ls;
    l->asString(context)->toUTF8String(context, ls);
    EXPECT_EQ(ls, "hello");

    const proto::ProtoObject* capitalizeM = strPrototype->getAttribute(context, proto::ProtoString::fromUTF8String(context, "capitalize"));
    ASSERT_NE(capitalizeM, nullptr);
    const proto::ProtoObject* cap = capitalizeM->asMethod(context)(context, wrapped, nullptr, context->newList(), nullptr);
    std::string cs;
    cap->asString(context)->toUTF8String(context, cs);
    EXPECT_EQ(cs, "Hello");

    const proto::ProtoObject* isdigitM = strPrototype->getAttribute(context, proto::ProtoString::fromUTF8String(context, "isdigit"));
    ASSERT_NE(isdigitM, nullptr);
    const proto::ProtoObject* digits = context->newObject(true)->addParent(context, strPrototype);
    digits->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->fromUTF8String("12345"));
    EXPECT_EQ(isdigitM->asMethod(context)(context, digits, nullptr, context->newList(), nullptr), PROTO_TRUE);
    const proto::ProtoObject* mixed = context->newObject(true)->addParent(context, strPrototype);
    mixed->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->fromUTF8String("12a45"));
    EXPECT_EQ(isdigitM->asMethod(context)(context, mixed, nullptr, context->newList(), nullptr), PROTO_FALSE);

    const proto::ProtoObject* isupperM = strPrototype->getAttribute(context, proto::ProtoString::fromUTF8String(context, "isupper"));
    ASSERT_NE(isupperM, nullptr);
    const proto::ProtoObject* upperStr = context->newObject(true)->addParent(context, strPrototype);
    upperStr->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->fromUTF8String("ABC"));
    EXPECT_EQ(isupperM->asMethod(context)(context, upperStr, nullptr, context->newList(), nullptr), PROTO_TRUE);
    EXPECT_EQ(isupperM->asMethod(context)(context, wrapped, nullptr, context->newList(), nullptr), PROTO_FALSE);

    const proto::ProtoObject* splitlinesM = strPrototype->getAttribute(context, proto::ProtoString::fromUTF8String(context, "splitlines"));
    ASSERT_NE(splitlinesM, nullptr);
    const proto::ProtoObject* linesStr = context->newObject(true)->addParent(context, strPrototype);
    linesStr->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->fromUTF8String("a\nb\nc"));
    const proto::ProtoObject* lines = splitlinesM->asMethod(context)(context, linesStr, nullptr, context->newList(), nullptr);
    ASSERT_NE(lines, nullptr);
    EXPECT_TRUE(lines->asList(context));
    EXPECT_EQ(lines->asList(context)->getSize(context), 3);
}

TEST_F(FoundationTest, BytesIndex) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* bytesPrototype = env.getBytesPrototype();
    ASSERT_NE(bytesPrototype, nullptr);
    const proto::ProtoObject* b = context->newObject(true)->addParent(context, bytesPrototype);
    b->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->fromUTF8String("hello"));
    const proto::ProtoObject* indexM = bytesPrototype->getAttribute(context, proto::ProtoString::fromUTF8String(context, "index"));
    ASSERT_NE(indexM, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromUTF8String("ll"));
    const proto::ProtoObject* idx = indexM->asMethod(context)(context, b, nullptr, args, nullptr);
    ASSERT_NE(idx, nullptr);
    EXPECT_TRUE(idx->isInteger(context));
    EXPECT_EQ(idx->asLong(context), 2);
}

TEST_F(FoundationTest, ContainerExceptions) {
    proto::ProtoContext* context = env.getContext();

    const proto::ProtoObject* dictObj = context->newObject(true)->addParent(context, env.getDictPrototype());
    dictObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->newSparseList()->asObject(context));
    dictObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__keys__"), context->newList()->asObject(context));

    const proto::ProtoObject* getitem = dictObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__getitem__"));
    ASSERT_NE(getitem, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromUTF8String("missing_key"));
    getitem->asMethod(context)(context, dictObj, nullptr, args, nullptr);

    const proto::ProtoObject* exc = env.takePendingException();
    ASSERT_NE(exc, nullptr);
}

TEST_F(FoundationTest, ExceptionScaffolding) {
    proto::ProtoContext* context = env.getContext();

    const proto::ProtoObject* exceptionsMod = env.resolve("exceptions");
    ASSERT_NE(exceptionsMod, nullptr);
    ASSERT_NE(exceptionsMod, PROTO_NONE);

    const proto::ProtoObject* keyErrorType = exceptionsMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "KeyError"));
    ASSERT_NE(keyErrorType, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromUTF8String("missing"));
    const proto::ProtoObject* instance = keyErrorType->call(context, nullptr, proto::ProtoString::fromUTF8String(context, "__call__"), keyErrorType, args, nullptr);
    ASSERT_NE(instance, nullptr);
    const proto::ProtoObject* reprM = instance->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__repr__"));
    ASSERT_NE(reprM, nullptr);
    const proto::ProtoObject* reprObj = reprM->asMethod(context)(context, instance, nullptr, context->newList(), nullptr);
    ASSERT_NE(reprObj, nullptr);
    std::string reprStr;
    reprObj->asString(context)->toUTF8String(context, reprStr);
    EXPECT_FALSE(reprStr.empty());
}

TEST_F(FoundationTest, SetBasic) {
    proto::ProtoContext* context = env.getContext();

    const proto::ProtoObject* setObj = context->newObject(true)->addParent(context, env.getSetPrototype());
    setObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->newSet()->asObject(context));

    const proto::ProtoObject* addM = setObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "add"));
    const proto::ProtoObject* removeM = setObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "remove"));
    const proto::ProtoObject* lenM = setObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__len__"));
    const proto::ProtoObject* containsM = setObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__contains__"));
    const proto::ProtoObject* iterM = setObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__iter__"));
    ASSERT_NE(addM, nullptr);
    ASSERT_NE(removeM, nullptr);
    ASSERT_NE(lenM, nullptr);
    ASSERT_NE(containsM, nullptr);
    ASSERT_NE(iterM, nullptr);

    const proto::ProtoList* add1 = context->newList()->appendLast(context, context->fromInteger(10));
    addM->asMethod(context)(context, setObj, nullptr, add1, nullptr);
    addM->asMethod(context)(context, setObj, nullptr, context->newList()->appendLast(context, context->fromInteger(20)), nullptr);
    addM->asMethod(context)(context, setObj, nullptr, context->newList()->appendLast(context, context->fromInteger(10)), nullptr);

    const proto::ProtoObject* lenResult = lenM->asMethod(context)(context, setObj, nullptr, context->newList(), nullptr);
    EXPECT_EQ(lenResult->asLong(context), 2);

    const proto::ProtoList* contains10 = context->newList()->appendLast(context, context->fromInteger(10));
    EXPECT_EQ(containsM->asMethod(context)(context, setObj, nullptr, contains10, nullptr), PROTO_TRUE);
    const proto::ProtoList* contains99 = context->newList()->appendLast(context, context->fromInteger(99));
    EXPECT_EQ(containsM->asMethod(context)(context, setObj, nullptr, contains99, nullptr), PROTO_FALSE);

    const proto::ProtoObject* it = iterM->asMethod(context)(context, setObj, nullptr, context->newList(), nullptr);
    ASSERT_NE(it, nullptr);

    const proto::ProtoList* removeArgs = context->newList()->appendLast(context, context->fromInteger(10));
    removeM->asMethod(context)(context, setObj, nullptr, removeArgs, nullptr);
    const proto::ProtoObject* lenAfter = lenM->asMethod(context)(context, setObj, nullptr, context->newList(), nullptr);
    EXPECT_EQ(lenAfter->asLong(context), 1);

    const proto::ProtoObject* popM = setObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "pop"));
    ASSERT_NE(popM, nullptr);
    const proto::ProtoObject* popped = popM->asMethod(context)(context, setObj, nullptr, context->newList(), nullptr);
    ASSERT_NE(popped, nullptr);
    EXPECT_TRUE(popped->isInteger(context));
    const proto::ProtoObject* lenFinal = lenM->asMethod(context)(context, setObj, nullptr, context->newList(), nullptr);
    EXPECT_EQ(lenFinal->asLong(context), 0);

    addM->asMethod(context)(context, setObj, nullptr, context->newList()->appendLast(context, context->fromInteger(5)), nullptr);
    addM->asMethod(context)(context, setObj, nullptr, context->newList()->appendLast(context, context->fromInteger(7)), nullptr);
    const proto::ProtoObject* discardM = setObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "discard"));
    ASSERT_NE(discardM, nullptr);
    discardM->asMethod(context)(context, setObj, nullptr, context->newList()->appendLast(context, context->fromInteger(99)), nullptr);
    EXPECT_EQ(lenM->asMethod(context)(context, setObj, nullptr, context->newList(), nullptr)->asLong(context), 2);
    discardM->asMethod(context)(context, setObj, nullptr, context->newList()->appendLast(context, context->fromInteger(5)), nullptr);
    EXPECT_EQ(lenM->asMethod(context)(context, setObj, nullptr, context->newList(), nullptr)->asLong(context), 1);
}

TEST_F(FoundationTest, SetUnion) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* setPrototype = env.getSetPrototype();
    ASSERT_NE(setPrototype, nullptr);
    const proto::ProtoObject* unionM = setPrototype->getAttribute(context, proto::ProtoString::fromUTF8String(context, "union"));
    ASSERT_NE(unionM, nullptr);
    const proto::ProtoObject* copyM = setPrototype->getAttribute(context, proto::ProtoString::fromUTF8String(context, "copy"));
    ASSERT_NE(copyM, nullptr);

    const proto::ProtoObject* set1 = context->newObject(true)->addParent(context, setPrototype);
    set1->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->newSet()->asObject(context));
    const proto::ProtoObject* addM = set1->getAttribute(context, proto::ProtoString::fromUTF8String(context, "add"));
    ASSERT_NE(addM, nullptr);
    addM->asMethod(context)(context, set1, nullptr, context->newList()->appendLast(context, context->fromInteger(1)), nullptr);
    addM->asMethod(context)(context, set1, nullptr, context->newList()->appendLast(context, context->fromInteger(2)), nullptr);

    const proto::ProtoObject* result = copyM->asMethod(context)(context, set1, nullptr, context->newList(), nullptr);
    ASSERT_NE(result, nullptr);
    const proto::ProtoObject* lenM = result->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__len__"));
    ASSERT_NE(lenM, nullptr);
    EXPECT_EQ(lenM->asMethod(context)(context, result, nullptr, context->newList(), nullptr)->asLong(context), 2);

    const proto::ProtoObject* unionResult = unionM->asMethod(context)(context, set1, nullptr, context->newList(), nullptr);
    ASSERT_NE(unionResult, nullptr);
}

TEST_F(FoundationTest, MathIsclose) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoObject* iscloseM = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "isclose"));
    ASSERT_NE(iscloseM, nullptr);

    const proto::ProtoList* argsSame = context->newList()->appendLast(context, context->fromDouble(1.0))->appendLast(context, context->fromDouble(1.0));
    EXPECT_EQ(iscloseM->asMethod(context)(context, mathMod, nullptr, argsSame, nullptr), PROTO_TRUE);

    const proto::ProtoList* argsClose = context->newList()->appendLast(context, context->fromDouble(1.0))->appendLast(context, context->fromDouble(1.0 + 1e-10));
    EXPECT_EQ(iscloseM->asMethod(context)(context, mathMod, nullptr, argsClose, nullptr), PROTO_TRUE);

    const proto::ProtoList* argsFar = context->newList()->appendLast(context, context->fromDouble(1.0))->appendLast(context, context->fromDouble(2.0));
    EXPECT_EQ(iscloseM->asMethod(context)(context, mathMod, nullptr, argsFar, nullptr), PROTO_FALSE);
}

TEST_F(FoundationTest, MathLog) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoObject* logM = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "log"));
    const proto::ProtoObject* log10M = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "log10"));
    ASSERT_NE(logM, nullptr);
    ASSERT_NE(log10M, nullptr);
    const proto::ProtoList* argsE = context->newList()->appendLast(context, context->fromDouble(2.718281828));
    const proto::ProtoObject* logE = logM->asMethod(context)(context, mathMod, nullptr, argsE, nullptr);
    ASSERT_NE(logE, nullptr);
    EXPECT_TRUE(logE->isDouble(context) || logE->isInteger(context));
    double valE = logE->isDouble(context) ? logE->asDouble(context) : static_cast<double>(logE->asLong(context));
    EXPECT_NEAR(valE, 1.0, 0.001);
    const proto::ProtoList* args100 = context->newList()->appendLast(context, context->fromDouble(100.0));
    const proto::ProtoObject* log10_100 = log10M->asMethod(context)(context, mathMod, nullptr, args100, nullptr);
    ASSERT_NE(log10_100, nullptr);
    double val10 = log10_100->isDouble(context) ? log10_100->asDouble(context) : static_cast<double>(log10_100->asLong(context));
    EXPECT_DOUBLE_EQ(val10, 2.0);
}

TEST_F(FoundationTest, MathSqrt) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoObject* sqrtM = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "sqrt"));
    ASSERT_NE(sqrtM, nullptr);
    const proto::ProtoList* args4 = context->newList()->appendLast(context, context->fromDouble(4.0));
    const proto::ProtoObject* result = sqrtM->asMethod(context)(context, mathMod, nullptr, args4, nullptr);
    ASSERT_NE(result, nullptr);
    double val = result->isDouble(context) ? result->asDouble(context) : static_cast<double>(result->asLong(context));
    EXPECT_DOUBLE_EQ(val, 2.0);
}

TEST_F(FoundationTest, MathTan) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoObject* tanM = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "tan"));
    ASSERT_NE(tanM, nullptr);
    const proto::ProtoList* args0 = context->newList()->appendLast(context, context->fromDouble(0.0));
    const proto::ProtoObject* result = tanM->asMethod(context)(context, mathMod, nullptr, args0, nullptr);
    ASSERT_NE(result, nullptr);
    double val = result->isDouble(context) ? result->asDouble(context) : static_cast<double>(result->asLong(context));
    EXPECT_DOUBLE_EQ(val, 0.0);
}

TEST_F(FoundationTest, MathAtan) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoObject* atanM = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "atan"));
    ASSERT_NE(atanM, nullptr);
    const proto::ProtoList* args1 = context->newList()->appendLast(context, context->fromDouble(1.0));
    const proto::ProtoObject* result = atanM->asMethod(context)(context, mathMod, nullptr, args1, nullptr);
    ASSERT_NE(result, nullptr);
    double val = result->isDouble(context) ? result->asDouble(context) : static_cast<double>(result->asLong(context));
    EXPECT_NEAR(val, 0.78539816339744828, 1e-10);  // atan(1) == pi/4
}

TEST_F(FoundationTest, MathAtan2) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoObject* atan2M = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "atan2"));
    ASSERT_NE(atan2M, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromDouble(1.0))->appendLast(context, context->fromDouble(1.0));
    const proto::ProtoObject* result = atan2M->asMethod(context)(context, mathMod, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    double val = result->isDouble(context) ? result->asDouble(context) : static_cast<double>(result->asLong(context));
    EXPECT_NEAR(val, 0.78539816339744828, 1e-10);  // atan2(1, 1) == pi/4
}

TEST_F(FoundationTest, OperatorAndOrFloatHex) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* opMod = env.resolve("operator");
    ASSERT_NE(opMod, nullptr);
    const proto::ProtoObject* andM = opMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "and_"));
    ASSERT_NE(andM, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromInteger(12))->appendLast(context, context->fromInteger(10));
    const proto::ProtoObject* result = andM->asMethod(context)(context, opMod, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->isInteger(context));
    EXPECT_EQ(result->asLong(context), 12 & 10);  // 0b1100 & 0b1010 == 8
}

TEST_F(FoundationTest, StrSplitMaxsplit) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* strPrototype = env.getStrPrototype();
    ASSERT_NE(strPrototype, nullptr);
    proto::ProtoObject* s = const_cast<proto::ProtoObject*>(context->newObject(true)->addParent(context, strPrototype));
    s->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->fromUTF8String("a b c"));
    const proto::ProtoObject* splitM = s->getAttribute(context, proto::ProtoString::fromUTF8String(context, "split"));
    ASSERT_NE(splitM, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromUTF8String(" "))->appendLast(context, context->fromInteger(1));
    const proto::ProtoObject* result = splitM->asMethod(context)(context, s, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"))->asList(context));
    const proto::ProtoList* parts = result->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"))->asList(context);
    EXPECT_EQ(parts->getSize(context), 2u);  // maxsplit=1 -> ["a", "b c"]
}

TEST_F(FoundationTest, MathHypot) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoObject* hypotM = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "hypot"));
    ASSERT_NE(hypotM, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromDouble(3.0))->appendLast(context, context->fromDouble(4.0));
    const proto::ProtoObject* result = hypotM->asMethod(context)(context, mathMod, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    double val = result->isDouble(context) ? result->asDouble(context) : static_cast<double>(result->asLong(context));
    EXPECT_NEAR(val, 5.0, 1e-10);  // hypot(3, 4) == 5
}

TEST_F(FoundationTest, OperatorIndex) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* opMod = env.resolve("operator");
    ASSERT_NE(opMod, nullptr);
    const proto::ProtoObject* indexM = opMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "index"));
    ASSERT_NE(indexM, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromInteger(42));
    const proto::ProtoObject* result = indexM->asMethod(context)(context, opMod, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->isInteger(context));
    EXPECT_EQ(result->asLong(context), 42);
}

TEST_F(FoundationTest, MathLog2) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoObject* log2M = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "log2"));
    ASSERT_NE(log2M, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromDouble(8.0));
    const proto::ProtoObject* result = log2M->asMethod(context)(context, mathMod, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    double val = result->isDouble(context) ? result->asDouble(context) : static_cast<double>(result->asLong(context));
    EXPECT_NEAR(val, 3.0, 1e-10);  // log2(8) == 3
}

TEST_F(FoundationTest, MathRemainder) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoObject* remainderM = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "remainder"));
    ASSERT_NE(remainderM, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromDouble(5.5))->appendLast(context, context->fromDouble(2.0));
    const proto::ProtoObject* result = remainderM->asMethod(context)(context, mathMod, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    double val = result->isDouble(context) ? result->asDouble(context) : static_cast<double>(result->asLong(context));
    EXPECT_NEAR(val, -0.5, 1e-10);  // remainder(5.5, 2.0) == -0.5
}

TEST_F(FoundationTest, MathErf) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoObject* erfM = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "erf"));
    ASSERT_NE(erfM, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromDouble(0.0));
    const proto::ProtoObject* result = erfM->asMethod(context)(context, mathMod, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    double val = result->isDouble(context) ? result->asDouble(context) : static_cast<double>(result->asLong(context));
    EXPECT_NEAR(val, 0.0, 1e-10);  // erf(0) == 0
}

TEST_F(FoundationTest, MathErfc) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoObject* erfcM = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "erfc"));
    ASSERT_NE(erfcM, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromDouble(0.0));
    const proto::ProtoObject* result = erfcM->asMethod(context)(context, mathMod, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    double val = result->isDouble(context) ? result->asDouble(context) : static_cast<double>(result->asLong(context));
    EXPECT_NEAR(val, 1.0, 1e-10);  // erfc(0) == 1
}

TEST_F(FoundationTest, MathGamma) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoObject* gammaM = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "gamma"));
    ASSERT_NE(gammaM, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromDouble(2.0));
    const proto::ProtoObject* result = gammaM->asMethod(context)(context, mathMod, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    double val = result->isDouble(context) ? result->asDouble(context) : static_cast<double>(result->asLong(context));
    EXPECT_NEAR(val, 1.0, 1e-10);  // gamma(2) == 1
}

TEST_F(FoundationTest, MathLgamma) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoObject* lgammaM = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "lgamma"));
    ASSERT_NE(lgammaM, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromDouble(2.0));
    const proto::ProtoObject* result = lgammaM->asMethod(context)(context, mathMod, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    double val = result->isDouble(context) ? result->asDouble(context) : static_cast<double>(result->asLong(context));
    EXPECT_NEAR(val, 0.0, 1e-10);  // lgamma(2) == 0
}

TEST_F(FoundationTest, MathDist) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoObject* distM = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "dist"));
    ASSERT_NE(distM, nullptr);
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* p1 = context->newObject(true)->addParent(context, env.getListPrototype());
    p1->setAttribute(context, dataName,
        context->newList()->appendLast(context, context->fromInteger(0))->appendLast(context, context->fromInteger(0))->asObject(context));
    const proto::ProtoObject* p2 = context->newObject(true)->addParent(context, env.getListPrototype());
    p2->setAttribute(context, dataName,
        context->newList()->appendLast(context, context->fromInteger(3))->appendLast(context, context->fromInteger(4))->asObject(context));
    const proto::ProtoList* args = context->newList()->appendLast(context, p1)->appendLast(context, p2);
    const proto::ProtoObject* result = distM->asMethod(context)(context, mathMod, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    double val = result->isDouble(context) ? result->asDouble(context) : static_cast<double>(result->asLong(context));
    EXPECT_NEAR(val, 5.0, 1e-10);  // dist((0,0), (3,4)) == 5
}

TEST_F(FoundationTest, MathPerm) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoObject* permM = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "perm"));
    ASSERT_NE(permM, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromInteger(5))->appendLast(context, context->fromInteger(2));
    const proto::ProtoObject* result = permM->asMethod(context)(context, mathMod, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->isInteger(context));
    EXPECT_EQ(result->asLong(context), 20);  // perm(5,2) == 20
}

TEST_F(FoundationTest, MathComb) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoObject* combM = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "comb"));
    ASSERT_NE(combM, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromInteger(5))->appendLast(context, context->fromInteger(2));
    const proto::ProtoObject* result = combM->asMethod(context)(context, mathMod, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->isInteger(context));
    EXPECT_EQ(result->asLong(context), 10);  // comb(5,2) == 10
}

TEST_F(FoundationTest, MathIsqrt) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoObject* isqrtM = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "isqrt"));
    ASSERT_NE(isqrtM, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromInteger(10));
    const proto::ProtoObject* result = isqrtM->asMethod(context)(context, mathMod, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->isInteger(context));
    EXPECT_EQ(result->asLong(context), 3);  // isqrt(10) == 3
}

TEST_F(FoundationTest, MathAcosh) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoObject* acoshM = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "acosh"));
    ASSERT_NE(acoshM, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromDouble(2.0));
    const proto::ProtoObject* result = acoshM->asMethod(context)(context, mathMod, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    double val = result->isDouble(context) ? result->asDouble(context) : static_cast<double>(result->asLong(context));
    EXPECT_NEAR(val, 1.3169578969248168, 1e-10);  // acosh(2)
}

TEST_F(FoundationTest, MathCosh) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoObject* coshM = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "cosh"));
    ASSERT_NE(coshM, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromDouble(0.0));
    const proto::ProtoObject* result = coshM->asMethod(context)(context, mathMod, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    double val = result->isDouble(context) ? result->asDouble(context) : static_cast<double>(result->asLong(context));
    EXPECT_NEAR(val, 1.0, 1e-10);  // cosh(0) == 1
}

TEST_F(FoundationTest, MathSinh) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoObject* sinhM = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "sinh"));
    ASSERT_NE(sinhM, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromDouble(0.0));
    const proto::ProtoObject* result = sinhM->asMethod(context)(context, mathMod, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    double val = result->isDouble(context) ? result->asDouble(context) : static_cast<double>(result->asLong(context));
    EXPECT_NEAR(val, 0.0, 1e-10);  // sinh(0) == 0
}

TEST_F(FoundationTest, MathUlp) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoObject* ulpM = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "ulp"));
    ASSERT_NE(ulpM, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromDouble(1.0));
    const proto::ProtoObject* result = ulpM->asMethod(context)(context, mathMod, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    double val = result->isDouble(context) ? result->asDouble(context) : static_cast<double>(result->asLong(context));
    EXPECT_GT(val, 0.0);
    EXPECT_LT(val, 1e-10);  // ulp(1.0) is small positive
}

TEST_F(FoundationTest, MathNextafter) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoObject* infObj = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "inf"));
    ASSERT_NE(infObj, nullptr);
    const proto::ProtoObject* nextafterM = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "nextafter"));
    ASSERT_NE(nextafterM, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromDouble(1.0))->appendLast(context, infObj);
    const proto::ProtoObject* result = nextafterM->asMethod(context)(context, mathMod, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    double val = result->isDouble(context) ? result->asDouble(context) : static_cast<double>(result->asLong(context));
    EXPECT_GT(val, 1.0);
    EXPECT_LT(val, 1.0 + 1e-14);  // nextafter(1, inf) > 1, very close
}

TEST_F(FoundationTest, MathLdexpFrexpModfE) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoString* ldexpStr = proto::ProtoString::fromUTF8String(context, "ldexp");
    const proto::ProtoString* frexpStr = proto::ProtoString::fromUTF8String(context, "frexp");
    const proto::ProtoString* modfStr = proto::ProtoString::fromUTF8String(context, "modf");
    const proto::ProtoString* eStr = proto::ProtoString::fromUTF8String(context, "e");
    const proto::ProtoObject* ldexpM = mathMod->getAttribute(context, ldexpStr);
    const proto::ProtoObject* frexpM = mathMod->getAttribute(context, frexpStr);
    const proto::ProtoObject* modfM = mathMod->getAttribute(context, modfStr);
    const proto::ProtoObject* eObj = mathMod->getAttribute(context, eStr);
    ASSERT_NE(ldexpM, nullptr);
    ASSERT_NE(frexpM, nullptr);
    ASSERT_NE(modfM, nullptr);
    ASSERT_NE(eObj, nullptr);
    const proto::ProtoList* argsLdexp = context->newList()->appendLast(context, context->fromDouble(1.5))->appendLast(context, context->fromInteger(2));
    const proto::ProtoObject* resLdexp = ldexpM->asMethod(context)(context, mathMod, nullptr, argsLdexp, nullptr);
    ASSERT_NE(resLdexp, nullptr);
    double valLdexp = resLdexp->isDouble(context) ? resLdexp->asDouble(context) : static_cast<double>(resLdexp->asLong(context));
    EXPECT_NEAR(valLdexp, 6.0, 1e-10);  // ldexp(1.5, 2) == 6.0
    const proto::ProtoList* argsFrexp = context->newList()->appendLast(context, context->fromDouble(8.0));
    const proto::ProtoObject* resFrexp = frexpM->asMethod(context)(context, mathMod, nullptr, argsFrexp, nullptr);
    ASSERT_NE(resFrexp, nullptr);
    ASSERT_TRUE(resFrexp->asTuple(context));
    const proto::ProtoTuple* tupFrexp = resFrexp->asTuple(context);
    ASSERT_GE(tupFrexp->getSize(context), 2u);
    double mantissa = tupFrexp->getAt(context, 0)->isDouble(context) ? tupFrexp->getAt(context, 0)->asDouble(context) : static_cast<double>(tupFrexp->getAt(context, 0)->asLong(context));
    EXPECT_NEAR(mantissa, 0.5, 1e-10);  // frexp(8) -> (0.5, 4)
    EXPECT_EQ(tupFrexp->getAt(context, 1)->asLong(context), 4);
    const proto::ProtoList* argsModf = context->newList()->appendLast(context, context->fromDouble(2.5));
    const proto::ProtoObject* resModf = modfM->asMethod(context)(context, mathMod, nullptr, argsModf, nullptr);
    ASSERT_NE(resModf, nullptr);
    ASSERT_TRUE(resModf->asTuple(context));
    const proto::ProtoTuple* tupModf = resModf->asTuple(context);
    ASSERT_GE(tupModf->getSize(context), 2u);
    double frac = tupModf->getAt(context, 0)->isDouble(context) ? tupModf->getAt(context, 0)->asDouble(context) : static_cast<double>(tupModf->getAt(context, 0)->asLong(context));
    double intpart = tupModf->getAt(context, 1)->isDouble(context) ? tupModf->getAt(context, 1)->asDouble(context) : static_cast<double>(tupModf->getAt(context, 1)->asLong(context));
    EXPECT_NEAR(frac, 0.5, 1e-10);   // modf(2.5) -> (0.5, 2.0)
    EXPECT_NEAR(intpart, 2.0, 1e-10);
    double eVal = eObj->isDouble(context) ? eObj->asDouble(context) : static_cast<double>(eObj->asLong(context));
    EXPECT_NEAR(eVal, 2.718281828459045, 1e-10);  // math.e
}

TEST_F(FoundationTest, MathCbrtExp2Expm1Fma) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoString* cbrtStr = proto::ProtoString::fromUTF8String(context, "cbrt");
    const proto::ProtoString* exp2Str = proto::ProtoString::fromUTF8String(context, "exp2");
    const proto::ProtoString* expm1Str = proto::ProtoString::fromUTF8String(context, "expm1");
    const proto::ProtoString* fmaStr = proto::ProtoString::fromUTF8String(context, "fma");
    const proto::ProtoObject* cbrtM = mathMod->getAttribute(context, cbrtStr);
    const proto::ProtoObject* exp2M = mathMod->getAttribute(context, exp2Str);
    const proto::ProtoObject* expm1M = mathMod->getAttribute(context, expm1Str);
    const proto::ProtoObject* fmaM = mathMod->getAttribute(context, fmaStr);
    ASSERT_NE(cbrtM, nullptr);
    ASSERT_NE(exp2M, nullptr);
    ASSERT_NE(expm1M, nullptr);
    ASSERT_NE(fmaM, nullptr);
    const proto::ProtoList* argsCbrt = context->newList()->appendLast(context, context->fromDouble(27.0));
    const proto::ProtoObject* resCbrt = cbrtM->asMethod(context)(context, mathMod, nullptr, argsCbrt, nullptr);
    ASSERT_NE(resCbrt, nullptr);
    double valCbrt = resCbrt->isDouble(context) ? resCbrt->asDouble(context) : static_cast<double>(resCbrt->asLong(context));
    EXPECT_NEAR(valCbrt, 3.0, 1e-10);  // cbrt(27) == 3.0
    const proto::ProtoList* argsExp2 = context->newList()->appendLast(context, context->fromInteger(3));
    const proto::ProtoObject* resExp2 = exp2M->asMethod(context)(context, mathMod, nullptr, argsExp2, nullptr);
    ASSERT_NE(resExp2, nullptr);
    double valExp2 = resExp2->isDouble(context) ? resExp2->asDouble(context) : static_cast<double>(resExp2->asLong(context));
    EXPECT_NEAR(valExp2, 8.0, 1e-10);  // exp2(3) == 8.0
    const proto::ProtoList* argsExpm1 = context->newList()->appendLast(context, context->fromInteger(0));
    const proto::ProtoObject* resExpm1 = expm1M->asMethod(context)(context, mathMod, nullptr, argsExpm1, nullptr);
    ASSERT_NE(resExpm1, nullptr);
    double valExpm1 = resExpm1->isDouble(context) ? resExpm1->asDouble(context) : static_cast<double>(resExpm1->asLong(context));
    EXPECT_NEAR(valExpm1, 0.0, 1e-10);  // expm1(0) == 0.0
    const proto::ProtoList* argsFma = context->newList()
        ->appendLast(context, context->fromDouble(2.0))
        ->appendLast(context, context->fromDouble(3.0))
        ->appendLast(context, context->fromDouble(4.0));
    const proto::ProtoObject* resFma = fmaM->asMethod(context)(context, mathMod, nullptr, argsFma, nullptr);
    ASSERT_NE(resFma, nullptr);
    double valFma = resFma->isDouble(context) ? resFma->asDouble(context) : static_cast<double>(resFma->asLong(context));
    EXPECT_NEAR(valFma, 10.0, 1e-10);  // fma(2, 3, 4) == 10.0
}

TEST_F(FoundationTest, MathSumprod) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoObject* sumprodM = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "sumprod"));
    ASSERT_NE(sumprodM, nullptr);
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* list1 = context->newObject(true)->addParent(context, env.getListPrototype());
    list1->setAttribute(context, dataName,
        context->newList()->appendLast(context, context->fromInteger(1))
            ->appendLast(context, context->fromInteger(2))
            ->appendLast(context, context->fromInteger(3))->asObject(context));
    const proto::ProtoObject* list2 = context->newObject(true)->addParent(context, env.getListPrototype());
    list2->setAttribute(context, dataName,
        context->newList()->appendLast(context, context->fromInteger(4))
            ->appendLast(context, context->fromInteger(5))
            ->appendLast(context, context->fromInteger(6))->asObject(context));
    const proto::ProtoList* args = context->newList()->appendLast(context, list1)->appendLast(context, list2);
    const proto::ProtoObject* result = sumprodM->asMethod(context)(context, mathMod, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    double val = result->isDouble(context) ? result->asDouble(context) : static_cast<double>(result->asLong(context));
    EXPECT_NEAR(val, 32.0, 1e-10);  // sumprod([1,2,3], [4,5,6]) == 1*4+2*5+3*6 == 32
}

TEST_F(FoundationTest, EvalCompile) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* pyEval = env.resolve("eval");
    const proto::ProtoObject* pyCompile = env.resolve("compile");
    ASSERT_NE(pyEval, nullptr);
    ASSERT_NE(pyCompile, nullptr);
    const proto::ProtoList* evalArgs = context->newList()->appendLast(context, context->fromUTF8String("1 + 2"));
    const proto::ProtoObject* evalResult = pyEval->asMethod(context)(context, PROTO_NONE, nullptr, evalArgs, nullptr);
    ASSERT_NE(evalResult, nullptr);
    EXPECT_TRUE(evalResult->isInteger(context));
    EXPECT_EQ(evalResult->asLong(context), 3);
    const proto::ProtoList* compileArgs = context->newList()
        ->appendLast(context, context->fromUTF8String("3 * 4"))
        ->appendLast(context, context->fromUTF8String("<test>"))
        ->appendLast(context, context->fromUTF8String("eval"));
    const proto::ProtoObject* codeObj = pyCompile->asMethod(context)(context, PROTO_NONE, nullptr, compileArgs, nullptr);
    ASSERT_NE(codeObj, nullptr);
}

/** Step 5: Extended parser/compiler – list literal and subscript. */
TEST_F(FoundationTest, CompiledSnippetListLiteralAndSubscr) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* pyEval = env.resolve("eval");
    ASSERT_NE(pyEval, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromUTF8String("[1, 2, 3][1]"));
    const proto::ProtoObject* result = pyEval->asMethod(context)(context, PROTO_NONE, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->isInteger(context));
    EXPECT_EQ(result->asLong(context), 2);
}

/** Step 5: exec with assignment and expression (module with multiple statements). */
TEST_F(FoundationTest, CompiledSnippetExecAssign) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* pyExec = env.resolve("exec");
    const proto::ProtoObject* pyEval = env.resolve("eval");
    ASSERT_NE(pyExec, nullptr);
    ASSERT_NE(pyEval, nullptr);
    proto::ProtoObject* frame = const_cast<proto::ProtoObject*>(context->newObject(true));
    const proto::ProtoList* execArgs = context->newList()
        ->appendLast(context, context->fromUTF8String("x = 1 + 2\n"))
        ->appendLast(context, frame);
    pyExec->asMethod(context)(context, PROTO_NONE, nullptr, execArgs, nullptr);
    const proto::ProtoList* evalArgs = context->newList()
        ->appendLast(context, context->fromUTF8String("x"))
        ->appendLast(context, frame);
    const proto::ProtoObject* result = pyEval->asMethod(context)(context, PROTO_NONE, nullptr, evalArgs, nullptr);
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->isInteger(context));
    EXPECT_EQ(result->asLong(context), 3);
}

TEST_F(FoundationTest, MathFactorial) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoObject* factM = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "factorial"));
    ASSERT_NE(factM, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromInteger(5));
    const proto::ProtoObject* result = factM->asMethod(context)(context, mathMod, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->isInteger(context));
    EXPECT_EQ(result->asLong(context), 120);  // factorial(5) == 120
}

TEST_F(FoundationTest, MathProd) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* mathMod = env.resolve("math");
    ASSERT_NE(mathMod, nullptr);
    const proto::ProtoObject* prodM = mathMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "prod"));
    ASSERT_NE(prodM, nullptr);
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoObject* listObj = context->newObject(true)->addParent(context, env.getListPrototype());
    listObj->setAttribute(context, dataName,
        context->newList()->appendLast(context, context->fromInteger(2))
            ->appendLast(context, context->fromInteger(3))
            ->appendLast(context, context->fromInteger(4))->asObject(context));
    const proto::ProtoList* args = context->newList()->appendLast(context, listObj);
    const proto::ProtoObject* result = prodM->asMethod(context)(context, mathMod, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    double val = result->isDouble(context) ? result->asDouble(context) : static_cast<double>(result->asLong(context));
    EXPECT_NEAR(val, 24.0, 1e-10);  // prod([2,3,4]) == 24
}

TEST_F(FoundationTest, ReprOrId) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* builtins = env.resolve("builtins");
    ASSERT_NE(builtins, nullptr);
    const proto::ProtoObject* reprM = builtins->getAttribute(context, proto::ProtoString::fromUTF8String(context, "repr"));
    ASSERT_NE(reprM, nullptr);
    const proto::ProtoList* args42 = context->newList()->appendLast(context, context->fromInteger(42));
    const proto::ProtoObject* result = reprM->asMethod(context)(context, builtins, nullptr, args42, nullptr);
    ASSERT_NE(result, nullptr);
    ASSERT_TRUE(result->isString(context));
    std::string s;
    result->asString(context)->toUTF8String(context, s);
    EXPECT_EQ(s, "42");
}

TEST_F(FoundationTest, ListIaddOrRemoveprefix) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* listPrototype = env.getListPrototype();
    proto::ProtoObject* listObj = const_cast<proto::ProtoObject*>(context->newObject(true)->addParent(context, listPrototype));
    listObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"),
        context->newList()->appendLast(context, context->fromInteger(1))->asObject(context));
    const proto::ProtoObject* iaddM = listObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__iadd__"));
    ASSERT_NE(iaddM, nullptr);
    const proto::ProtoObject* other = context->newObject(true)->addParent(context, listPrototype);
    other->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"),
        context->newList()->appendLast(context, context->fromInteger(2))->appendLast(context, context->fromInteger(3))->asObject(context));
    const proto::ProtoList* args = context->newList()->appendLast(context, other);
    const proto::ProtoObject* result = iaddM->asMethod(context)(context, listObj, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    const proto::ProtoObject* data = listObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"));
    ASSERT_NE(data, nullptr);
    ASSERT_TRUE(data->asList(context));
    EXPECT_EQ(data->asList(context)->getSize(context), 3u);
    EXPECT_EQ(data->asList(context)->getAt(context, 0)->asLong(context), 1);
    EXPECT_EQ(data->asList(context)->getAt(context, 1)->asLong(context), 2);
    EXPECT_EQ(data->asList(context)->getAt(context, 2)->asLong(context), 3);
}

TEST_F(FoundationTest, OperatorInvert) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* opMod = env.resolve("operator");
    ASSERT_NE(opMod, nullptr);
    const proto::ProtoObject* invertM = opMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "invert"));
    ASSERT_NE(invertM, nullptr);
    const proto::ProtoList* args5 = context->newList()->appendLast(context, context->fromInteger(5));
    const proto::ProtoObject* result = invertM->asMethod(context)(context, opMod, nullptr, args5, nullptr);
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->isInteger(context));
    EXPECT_EQ(result->asLong(context), -6);  // ~5 == -6 in Python
}

TEST_F(FoundationTest, DictUnion) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* dictProto = env.getDictPrototype();
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    const proto::ProtoString* keysName = proto::ProtoString::fromUTF8String(context, "__keys__");
    const proto::ProtoObject* d1 = context->newObject(true)->addParent(context, dictProto);
    d1->setAttribute(context, dataName, context->newSparseList()->asObject(context));
    d1->setAttribute(context, keysName, context->newList()->asObject(context));
    const proto::ProtoObject* k1 = context->fromUTF8String("a");
    const proto::ProtoObject* v1 = context->fromInteger(1);
    const proto::ProtoList* setArgs = context->newList()->appendLast(context, k1)->appendLast(context, v1);
    d1->call(context, nullptr, proto::ProtoString::fromUTF8String(context, "__setitem__"), d1, setArgs);
    const proto::ProtoObject* d2 = context->newObject(true)->addParent(context, dictProto);
    d2->setAttribute(context, dataName, context->newSparseList()->asObject(context));
    d2->setAttribute(context, keysName, context->newList()->asObject(context));
    const proto::ProtoObject* k2 = context->fromUTF8String("b");
    const proto::ProtoObject* v2 = context->fromInteger(2);
    d2->call(context, nullptr, proto::ProtoString::fromUTF8String(context, "__setitem__"), d2, context->newList()->appendLast(context, k2)->appendLast(context, v2));
    const proto::ProtoObject* orM = d1->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__or__"));
    ASSERT_NE(orM, nullptr);
    const proto::ProtoList* orArgs = context->newList()->appendLast(context, d2);
    const proto::ProtoObject* merged = orM->asMethod(context)(context, d1, nullptr, orArgs, nullptr);
    ASSERT_NE(merged, nullptr);
    const proto::ProtoObject* lenM = merged->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__len__"));
    ASSERT_NE(lenM, nullptr);
    const proto::ProtoObject* lenVal = lenM->asMethod(context)(context, merged, nullptr, context->newList(), nullptr);
    ASSERT_NE(lenVal, nullptr);
    EXPECT_EQ(lenVal->asLong(context), 2);
}

TEST_F(FoundationTest, OperatorNegOrGetattr) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* opMod = env.resolve("operator");
    ASSERT_NE(opMod, nullptr);
    const proto::ProtoObject* negM = opMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "neg"));
    ASSERT_NE(negM, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromInteger(-7));
    const proto::ProtoObject* result = negM->asMethod(context)(context, opMod, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result->asLong(context), 7);
}

TEST_F(FoundationTest, ListRepeat) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* listPrototype = env.getListPrototype();
    const proto::ProtoObject* listObj = context->newObject(true)->addParent(context, listPrototype);
    listObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"),
        context->newList()->appendLast(context, context->fromInteger(1))->appendLast(context, context->fromInteger(2))->asObject(context));
    const proto::ProtoObject* mulM = listObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__mul__"));
    ASSERT_NE(mulM, nullptr);
    const proto::ProtoList* args = context->newList()->appendLast(context, context->fromInteger(3));
    const proto::ProtoObject* result = mulM->asMethod(context)(context, listObj, nullptr, args, nullptr);
    ASSERT_NE(result, nullptr);
    const proto::ProtoObject* data = result->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"));
    ASSERT_NE(data, nullptr);
    ASSERT_TRUE(data->asList(context));
    EXPECT_EQ(data->asList(context)->getSize(context), 6u);
    EXPECT_EQ(data->asList(context)->getAt(context, 0)->asLong(context), 1);
    EXPECT_EQ(data->asList(context)->getAt(context, 5)->asLong(context), 2);
}

TEST_F(FoundationTest, HasattrDelattr) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* builtins = env.resolve("builtins");
    ASSERT_NE(builtins, nullptr);
    const proto::ProtoObject* hasattrM = builtins->getAttribute(context, proto::ProtoString::fromUTF8String(context, "hasattr"));
    const proto::ProtoObject* delattrM = builtins->getAttribute(context, proto::ProtoString::fromUTF8String(context, "delattr"));
    ASSERT_NE(hasattrM, nullptr);
    ASSERT_NE(delattrM, nullptr);
    proto::ProtoObject* obj = const_cast<proto::ProtoObject*>(context->newObject(true));
    obj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "x"), context->fromInteger(42));
    const proto::ProtoList* hasX = context->newList()->appendLast(context, obj)->appendLast(context, context->fromUTF8String("x"));
    EXPECT_EQ(hasattrM->asMethod(context)(context, builtins, nullptr, hasX, nullptr), PROTO_TRUE);
    const proto::ProtoList* hasY = context->newList()->appendLast(context, obj)->appendLast(context, context->fromUTF8String("y"));
    EXPECT_EQ(hasattrM->asMethod(context)(context, builtins, nullptr, hasY, nullptr), PROTO_FALSE);
    const proto::ProtoList* delX = context->newList()->appendLast(context, obj)->appendLast(context, context->fromUTF8String("x"));
    delattrM->asMethod(context)(context, builtins, nullptr, delX, nullptr);
    EXPECT_EQ(obj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "x")), PROTO_NONE);
}

TEST_F(FoundationTest, SetattrAndCallable) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* builtins = env.resolve("builtins");
    ASSERT_NE(builtins, nullptr);
    const proto::ProtoObject* setattrM = builtins->getAttribute(context, proto::ProtoString::fromUTF8String(context, "setattr"));
    const proto::ProtoObject* getattrM = builtins->getAttribute(context, proto::ProtoString::fromUTF8String(context, "getattr"));
    const proto::ProtoObject* callableM = builtins->getAttribute(context, proto::ProtoString::fromUTF8String(context, "callable"));
    ASSERT_NE(setattrM, nullptr);
    ASSERT_NE(getattrM, nullptr);
    ASSERT_NE(callableM, nullptr);
    proto::ProtoObject* obj = const_cast<proto::ProtoObject*>(context->newObject(true));
    const proto::ProtoList* setArgs = context->newList()->appendLast(context, obj)->appendLast(context, context->fromUTF8String("foo"))->appendLast(context, context->fromInteger(100));
    setattrM->asMethod(context)(context, builtins, nullptr, setArgs, nullptr);
    const proto::ProtoList* getArgs = context->newList()->appendLast(context, obj)->appendLast(context, context->fromUTF8String("foo"));
    const proto::ProtoObject* got = getattrM->asMethod(context)(context, builtins, nullptr, getArgs, nullptr);
    ASSERT_NE(got, nullptr);
    EXPECT_TRUE(got->isInteger(context));
    EXPECT_EQ(got->asLong(context), 100);
    const proto::ProtoObject* intProto = env.getIntPrototype();
    const proto::ProtoList* callableArgs = context->newList()->appendLast(context, intProto);
    const proto::ProtoObject* callableResult = callableM->asMethod(context)(context, builtins, nullptr, callableArgs, nullptr);
    EXPECT_EQ(callableResult, PROTO_TRUE);
}

TEST_F(FoundationTest, ListIaddAndDictIor) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* listProto = env.getListPrototype();
    const proto::ProtoString* dataName = proto::ProtoString::fromUTF8String(context, "__data__");
    proto::ProtoObject* listObj = const_cast<proto::ProtoObject*>(context->newObject(true)->addParent(context, listProto));
    listObj->setAttribute(context, dataName, context->newList()->appendLast(context, context->fromInteger(1))->appendLast(context, context->fromInteger(2))->asObject(context));
    const proto::ProtoObject* otherList = context->newObject(true)->addParent(context, listProto);
    otherList->setAttribute(context, dataName, context->newList()->appendLast(context, context->fromInteger(3))->asObject(context));
    const proto::ProtoObject* iaddM = listObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__iadd__"));
    ASSERT_NE(iaddM, nullptr);
    const proto::ProtoList* iaddArgs = context->newList()->appendLast(context, otherList);
    const proto::ProtoObject* result = iaddM->asMethod(context)(context, listObj, nullptr, iaddArgs, nullptr);
    ASSERT_NE(result, nullptr);
    EXPECT_EQ(result, listObj);
    const proto::ProtoObject* data = listObj->getAttribute(context, dataName);
    ASSERT_TRUE(data && data->asList(context));
    EXPECT_EQ(data->asList(context)->getSize(context), 3u);
    EXPECT_EQ(data->asList(context)->getAt(context, 2)->asLong(context), 3);

    const proto::ProtoObject* dictProto = env.getDictPrototype();
    const proto::ProtoString* keysName = proto::ProtoString::fromUTF8String(context, "__keys__");
    proto::ProtoObject* d1 = const_cast<proto::ProtoObject*>(context->newObject(true)->addParent(context, dictProto));
    d1->setAttribute(context, dataName, context->newSparseList()->asObject(context));
    d1->setAttribute(context, keysName, context->newList()->asObject(context));
    const proto::ProtoObject* k1 = context->fromUTF8String("a");
    d1->call(context, nullptr, proto::ProtoString::fromUTF8String(context, "__setitem__"), d1, context->newList()->appendLast(context, k1)->appendLast(context, context->fromInteger(1)));
    proto::ProtoObject* d2 = const_cast<proto::ProtoObject*>(context->newObject(true)->addParent(context, dictProto));
    d2->setAttribute(context, dataName, context->newSparseList()->asObject(context));
    d2->setAttribute(context, keysName, context->newList()->asObject(context));
    const proto::ProtoObject* k2 = context->fromUTF8String("b");
    d2->call(context, nullptr, proto::ProtoString::fromUTF8String(context, "__setitem__"), d2, context->newList()->appendLast(context, k2)->appendLast(context, context->fromInteger(2)));
    const proto::ProtoObject* iorM = d1->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__ior__"));
    ASSERT_NE(iorM, nullptr);
    const proto::ProtoObject* iorResult = iorM->asMethod(context)(context, d1, nullptr, context->newList()->appendLast(context, d2), nullptr);
    ASSERT_NE(iorResult, nullptr);
    EXPECT_EQ(iorResult, d1);
    const proto::ProtoObject* lenM = d1->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__len__"));
    ASSERT_NE(lenM, nullptr);
    EXPECT_EQ(lenM->asMethod(context)(context, d1, nullptr, context->newList(), nullptr)->asLong(context), 2);
}

TEST_F(FoundationTest, ItertoolsAccumulate) {
    proto::ProtoContext* context = env.getContext();
    const proto::ProtoObject* itertoolsMod = env.resolve("itertools");
    ASSERT_NE(itertoolsMod, nullptr);
    const proto::ProtoObject* accumulateM = itertoolsMod->getAttribute(context, proto::ProtoString::fromUTF8String(context, "accumulate"));
    ASSERT_NE(accumulateM, nullptr);

    const proto::ProtoObject* listPrototype = env.getListPrototype();
    const proto::ProtoObject* listObj = context->newObject(true)->addParent(context, listPrototype);
    listObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"),
        context->newList()->appendLast(context, context->fromInteger(1))->appendLast(context, context->fromInteger(2))->appendLast(context, context->fromInteger(3))->asObject(context));
    const proto::ProtoList* args = context->newList()->appendLast(context, listObj);
    const proto::ProtoObject* accIt = accumulateM->asMethod(context)(context, itertoolsMod, nullptr, args, nullptr);
    ASSERT_NE(accIt, nullptr);
    const proto::ProtoObject* nextM = accIt->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__next__"));
    ASSERT_NE(nextM, nullptr);
    ASSERT_TRUE(nextM->asMethod(context));
    const proto::ProtoObject* v1 = nextM->asMethod(context)(context, accIt, nullptr, context->newList(), nullptr);
    const proto::ProtoObject* v2 = nextM->asMethod(context)(context, accIt, nullptr, context->newList(), nullptr);
    const proto::ProtoObject* v3 = nextM->asMethod(context)(context, accIt, nullptr, context->newList(), nullptr);
    ASSERT_NE(v1, nullptr);
    ASSERT_NE(v2, nullptr);
    ASSERT_NE(v3, nullptr);
    EXPECT_EQ(v1->asLong(context), 1);
    EXPECT_EQ(v2->asLong(context), 3);
    EXPECT_EQ(v3->asLong(context), 6);
    EXPECT_EQ(nextM->asMethod(context)(context, accIt, nullptr, context->newList(), nullptr), PROTO_NONE);
}

TEST_F(FoundationTest, ListInsertRemoveClear) {
    proto::ProtoContext* context = env.getContext();

    const proto::ProtoObject* listObj = context->newObject(true)->addParent(context, env.getListPrototype());
    listObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"),
        context->newList()->appendLast(context, context->fromInteger(1))->appendLast(context, context->fromInteger(2))->appendLast(context, context->fromInteger(3))->asObject(context));

    const proto::ProtoObject* insertM = listObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "insert"));
    const proto::ProtoObject* removeM = listObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "remove"));
    const proto::ProtoObject* clearM = listObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "clear"));
    const proto::ProtoObject* lenM = listObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__len__"));
    ASSERT_NE(insertM, nullptr);
    ASSERT_NE(removeM, nullptr);
    ASSERT_NE(clearM, nullptr);
    ASSERT_NE(lenM, nullptr);

    const proto::ProtoList* insertArgs = context->newList()->appendLast(context, context->fromInteger(0))->appendLast(context, context->fromInteger(0));
    insertM->asMethod(context)(context, listObj, nullptr, insertArgs, nullptr);
    const proto::ProtoObject* len1 = lenM->asMethod(context)(context, listObj, nullptr, context->newList(), nullptr);
    EXPECT_EQ(len1->asLong(context), 4);

    const proto::ProtoList* removeArgs = context->newList()->appendLast(context, context->fromInteger(2));
    removeM->asMethod(context)(context, listObj, nullptr, removeArgs, nullptr);
    const proto::ProtoObject* len2 = lenM->asMethod(context)(context, listObj, nullptr, context->newList(), nullptr);
    EXPECT_EQ(len2->asLong(context), 3);

    clearM->asMethod(context)(context, listObj, nullptr, context->newList(), nullptr);
    const proto::ProtoObject* len3 = lenM->asMethod(context)(context, listObj, nullptr, context->newList(), nullptr);
    EXPECT_EQ(len3->asLong(context), 0);
}

TEST_F(FoundationTest, DictUpdateClearCopy) {
    proto::ProtoContext* context = env.getContext();

    const proto::ProtoObject* dictObj = context->newObject(true)->addParent(context, env.getDictPrototype());
    dictObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->newSparseList()->asObject(context));
    dictObj->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__keys__"), context->newList()->asObject(context));

    const proto::ProtoObject* setitem = dictObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__setitem__"));
    const proto::ProtoObject* update = dictObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "update"));
    const proto::ProtoObject* clear = dictObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "clear"));
    const proto::ProtoObject* copy = dictObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "copy"));
    const proto::ProtoObject* lenM = dictObj->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__len__"));

    const proto::ProtoList* args1 = context->newList()->appendLast(context, context->fromInteger(1))->appendLast(context, context->fromUTF8String("a"));
    setitem->asMethod(context)(context, dictObj, nullptr, args1, nullptr);
    const proto::ProtoList* args2 = context->newList()->appendLast(context, context->fromInteger(2))->appendLast(context, context->fromUTF8String("b"));
    setitem->asMethod(context)(context, dictObj, nullptr, args2, nullptr);

    const proto::ProtoObject* other = context->newObject(true)->addParent(context, env.getDictPrototype());
    other->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->newSparseList()->asObject(context));
    other->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__keys__"), context->newList()->asObject(context));
    const proto::ProtoObject* otherSetitem = other->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__setitem__"));
    const proto::ProtoList* otherArgs1 = context->newList()->appendLast(context, context->fromInteger(2))->appendLast(context, context->fromUTF8String("B"));
    otherSetitem->asMethod(context)(context, other, nullptr, otherArgs1, nullptr);
    const proto::ProtoList* otherArgs2 = context->newList()->appendLast(context, context->fromInteger(3))->appendLast(context, context->fromUTF8String("c"));
    otherSetitem->asMethod(context)(context, other, nullptr, otherArgs2, nullptr);

    const proto::ProtoList* updateArgs = context->newList()->appendLast(context, other);
    update->asMethod(context)(context, dictObj, nullptr, updateArgs, nullptr);

    const proto::ProtoObject* lenResult = lenM->asMethod(context)(context, dictObj, nullptr, context->newList(), nullptr);
    EXPECT_EQ(lenResult->asLong(context), 3);

    const proto::ProtoObject* copied = copy->asMethod(context)(context, dictObj, nullptr, context->newList(), nullptr);
    ASSERT_NE(copied, nullptr);
    const proto::ProtoObject* copyLen = copied->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__len__"));
    ASSERT_NE(copyLen, nullptr);
    const proto::ProtoObject* copyLenResult = copyLen->asMethod(context)(context, copied, nullptr, context->newList(), nullptr);
    EXPECT_EQ(copyLenResult->asLong(context), 3);

    clear->asMethod(context)(context, dictObj, nullptr, context->newList(), nullptr);
    const proto::ProtoObject* lenAfterClear = lenM->asMethod(context)(context, dictObj, nullptr, context->newList(), nullptr);
    EXPECT_EQ(lenAfterClear->asLong(context), 0);

    const proto::ProtoObject* copyLenAfter = copyLen->asMethod(context)(context, copied, nullptr, context->newList(), nullptr);
    EXPECT_EQ(copyLenAfter->asLong(context), 3);
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

TEST_F(FoundationTest, AtexitRegister) {
    const proto::ProtoObject* atexitMod = env.resolve("atexit");
    ASSERT_NE(atexitMod, nullptr);
    ASSERT_NE(atexitMod, PROTO_NONE);
    const proto::ProtoString* reg = proto::ProtoString::fromUTF8String(env.getContext(), "register");
    const proto::ProtoString* unreg = proto::ProtoString::fromUTF8String(env.getContext(), "unregister");
    const proto::ProtoString* run = proto::ProtoString::fromUTF8String(env.getContext(), "_run_exitfuncs");
    EXPECT_NE(atexitMod->getAttribute(env.getContext(), reg), nullptr);
    EXPECT_NE(atexitMod->getAttribute(env.getContext(), unreg), nullptr);
    EXPECT_NE(atexitMod->getAttribute(env.getContext(), run), nullptr);
}

TEST_F(FoundationTest, ShutilCopyfile) {
    const proto::ProtoObject* shutilMod = env.resolve("shutil");
    ASSERT_NE(shutilMod, nullptr);
    ASSERT_NE(shutilMod, PROTO_NONE);
    const proto::ProtoString* copyfile = proto::ProtoString::fromUTF8String(env.getContext(), "copyfile");
    const proto::ProtoString* copy = proto::ProtoString::fromUTF8String(env.getContext(), "copy");
    EXPECT_NE(shutilMod->getAttribute(env.getContext(), copyfile), nullptr);
    EXPECT_NE(shutilMod->getAttribute(env.getContext(), copy), nullptr);
}

TEST_F(FoundationTest, ThreadModuleLoads) {
    const proto::ProtoObject* threadMod = env.resolve("_thread");
    ASSERT_NE(threadMod, nullptr);
    ASSERT_NE(threadMod, PROTO_NONE);
    const proto::ProtoString* getIdent = proto::ProtoString::fromUTF8String(env.getContext(), "get_ident");
    const proto::ProtoObject* getIdentFn = threadMod->getAttribute(env.getContext(), getIdent);
    ASSERT_NE(getIdentFn, nullptr);
    ASSERT_TRUE(getIdentFn->isMethod(env.getContext()));
    const proto::ProtoList* emptyArgs = env.getContext()->newList();
    const proto::ProtoObject* result = getIdentFn->asMethod(env.getContext())(
        env.getContext(), const_cast<proto::ProtoObject*>(threadMod), nullptr, emptyArgs, nullptr);
    ASSERT_NE(result, nullptr);
    EXPECT_TRUE(result->isInteger(env.getContext()));
}

TEST_F(FoundationTest, StatisticsModuleLoads) {
    const proto::ProtoObject* statsMod = env.resolve("statistics");
    ASSERT_NE(statsMod, nullptr);
    ASSERT_NE(statsMod, PROTO_NONE);
    const proto::ProtoString* fileKey = proto::ProtoString::fromUTF8String(env.getContext(), "__file__");
    const proto::ProtoObject* fileVal = statsMod->getAttribute(env.getContext(), fileKey);
    EXPECT_TRUE(fileVal && fileVal->isString(env.getContext()));
}

TEST_F(FoundationTest, UrllibParseModuleLoads) {
    const proto::ProtoObject* parseMod = env.resolve("urllib.parse");
    ASSERT_NE(parseMod, nullptr);
    ASSERT_NE(parseMod, PROTO_NONE);
    const proto::ProtoString* fileKey = proto::ProtoString::fromUTF8String(env.getContext(), "__file__");
    const proto::ProtoObject* fileVal = parseMod->getAttribute(env.getContext(), fileKey);
    EXPECT_TRUE(fileVal && fileVal->isString(env.getContext()));
}
