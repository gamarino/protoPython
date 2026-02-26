
#include <thread>
#include <iostream>
#include <protoPython/PythonEnvironment.h>
#include <protoCore.h>

using namespace protoPython;

#define STDLIB_PATH "./lib/python3.14"

int main() {
    std::cout << "Starting C++ repro..." << std::endl;
    PythonEnvironment env{STDLIB_PATH};
    auto context = env.getContext();

    // Create a dictionary
    const proto::ProtoObject* dict = context->newObject(true)->addParent(context, env.getDictPrototype());
    dict->setAttribute(context, proto::ProtoString::fromUTF8String(context, "__data__"), context->newSparseList()->asObject(context));
    
    // Add an item
    const proto::ProtoObject* setitem = dict->getAttribute(context, proto::ProtoString::fromUTF8String(context, "__setitem__"));
    const proto::ProtoList* setArgs = context->newList()
        ->appendLast(context, context->fromUTF8String("key"))
        ->appendLast(context, context->fromInteger(42));
    setitem->asMethod(context)(context, dict, nullptr, setArgs, nullptr);

    // Get items()
    const proto::ProtoObject* itemsMethod = dict->getAttribute(context, proto::ProtoString::fromUTF8String(context, "items"));
    const proto::ProtoObject* itemsObj = itemsMethod->asMethod(context)(context, dict, nullptr, nullptr, nullptr);
    
    std::cout << "itemsObj type: " << (itemsObj->asList(context) ? "List" : "Not List") << std::endl;

    // Get Iterator
    const proto::ProtoListIterator* it = itemsObj->asList(context)->getIterator(context);
    std::cout << "Got iterator" << std::endl;

    // Iterate
    while (it->hasNext(context)) {
        const proto::ProtoObject* item = it->next(context);
        std::cout << "Got item" << std::endl;
        it = it->advance(context);
    }

    std::cout << "Done." << std::endl;
    return 0;
}
